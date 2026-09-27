#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include "host/win32k/cocoa.h"
#include "host/win32k/main_thread.h"

// Cocoa (AppKit) との橋渡し．ここの Objective-C のコードはすべてメインスレッドで動く．
// NSWindow は HWND → NSWindow の表 (windows) で管理し，他のスレッドとはポインタを共有しない．

using namespace ntmac::win32k;
using WRect = ntmac::win32k::Rect; // MacTypes.h の Rect と区別する

namespace {

// Windows の定数
constexpr uint32_t kWsCaption = 0x00C00000, kWsSysMenu = 0x00080000, kWsThickFrame = 0x00040000;
constexpr uint32_t kWsMinimizeBox = 0x00020000, kWsMaximizeBox = 0x00010000, kWsDisabled = 0x08000000;
constexpr uint32_t kWmMove = 0x0003, kWmSize = 0x0005, kWmCommand = 0x0111, kWmSysCommand = 0x0112;
constexpr uint32_t kWmKeyDown = 0x0100, kWmKeyUp = 0x0101, kWmChar = 0x0102;
constexpr uint32_t kWmSysKeyDown = 0x0104, kWmSysKeyUp = 0x0105, kWmSysChar = 0x0106;
constexpr uint32_t kWmMouseMove = 0x0200, kWmLButtonDown = 0x0201, kWmLButtonUp = 0x0202, kWmLButtonDblClk = 0x0203;
constexpr uint32_t kWmRButtonDown = 0x0204, kWmRButtonUp = 0x0205, kWmMButtonDown = 0x0207, kWmMButtonUp = 0x0208;
constexpr uint32_t kWmMouseWheel = 0x020A;
constexpr uint32_t kScClose = 0xF060;
constexpr uint32_t kCsDblClks = 0x0008;
constexpr uint32_t kMfPopup = 0x10, kMfSeparator = 0x800, kMfGrayed = 0x1, kMfDisabled = 0x2, kMfChecked = 0x8;
constexpr uint8_t kVkShift = 0x10, kVkControl = 0x11, kVkMenu = 0x12, kVkCapital = 0x14, kVkLWin = 0x5B;

bool gui_active = false;    // ensure_gui() に成功したか (メインスレッドからのみ参照)

int64_t make_lparam(int32_t lo, int32_t hi) {
    return static_cast<int64_t>(static_cast<uint32_t>((lo & 0xFFFF) | ((hi & 0xFFFF) << 16)));
}

NSString* to_ns(const std::u16string& s) {
    return [NSString stringWithCharacters:reinterpret_cast<const unichar*>(s.data()) length:s.size()];
}

/** メニューの表示用の文字列: & (アクセスキー) を取り除き，タブ以降 (ショートカットの表示) を捨てる */
NSString* menu_title(const std::u16string& s) {
    std::u16string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == u'\t') break;
        if (s[i] == u'&') {
            if (i + 1 < s.size() && s[i + 1] == u'&') out.push_back(u'&'), i++;
            continue;
        }
        out.push_back(s[i]);
    }
    return to_ns(out);
}

/** Windows のスクリーン座標 (上が原点) と Cocoa の座標 (メイン画面の下が原点) の変換 */
CGFloat primary_screen_height() {
    return [NSScreen screens].count ? [NSScreen screens][0].frame.size.height : 900;
}

NSRect to_cocoa_rect(const WRect& r) {
    return NSMakeRect(r.left, primary_screen_height() - r.bottom, r.width(), r.height());
}

WRect from_cocoa_rect(NSRect r) {
    auto left = static_cast<int32_t>(std::lround(r.origin.x));
    auto top = static_cast<int32_t>(std::lround(primary_screen_height() - (r.origin.y + r.size.height)));
    return {left, top, left + static_cast<int32_t>(std::lround(r.size.width)),
            top + static_cast<int32_t>(std::lround(r.size.height))};
}

/** macOS のキーコード (US 配列) → Windows の仮想キー */
uint8_t virtual_key(unsigned short code) {
    static const uint8_t table[128] = {
        'A', 'S', 'D', 'F', 'H', 'G', 'Z', 'X', 'C', 'V', 0, 'B', 'Q', 'W', 'E', 'R',            // 0x00
        'Y', 'T', '1', '2', '3', '4', '6', '5', 0xBB, '9', '7', 0xBD, '8', '0', 0xDD, 'O',     // 0x10
        'U', 0xDB, 'I', 'P', 0x0D, 'L', 'J', 0xDE, 'K', 0xBA, 0xDC, 0xBC, 0xBF, 'N', 'M', 0xBE, // 0x20
        0x09, 0x20, 0xC0, 0x08, 0, 0x1B, 0x5C, 0x5B, 0x10, 0x14, 0x12, 0x11, 0x10, 0x12, 0x11, 0, // 0x30
        0x86, 0x6E, 0, 0x6A, 0, 0x6B, 0, 0x0C, 0, 0, 0, 0x6F, 0x0D, 0, 0x6D, 0x87,             // 0x40
        0x88, 0xBB, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x85, 0x68, 0x69, 0xDC, 0xE2, 0x6C, // 0x50
        0x74, 0x75, 0x76, 0x72, 0x77, 0x78, 0xF2, 0x7A, 0x15, 0x7C, 0x81, 0x7D, 0, 0x79, 0x5D, 0x7B, // 0x60
        0, 0x7E, 0x2D, 0x24, 0x21, 0x2E, 0x73, 0x23, 0x71, 0x22, 0x70, 0x25, 0x27, 0x28, 0x26, 0,  // 0x70
    };
    return code < 128 ? table[code] : 0;
}

uint64_t mouse_key_flags(NSEvent* e) {
    NSUInteger buttons = [NSEvent pressedMouseButtons];
    NSEventModifierFlags mods = e.modifierFlags;
    uint64_t flags = 0;
    if (buttons & 1) flags |= 0x01;             // MK_LBUTTON
    if (buttons & 2) flags |= 0x02;             // MK_RBUTTON
    if (buttons & 4) flags |= 0x10;             // MK_MBUTTON
    if (mods & NSEventModifierFlagShift) flags |= 0x04;
    if (mods & NSEventModifierFlagControl) flags |= 0x08;
    return flags;
}

std::string dump_dir() {
    const char* d = std::getenv("NTMAC_DUMP_DIR");
    return d ? d : "";
}

void dump_png(CGImageRef image, uint64_t hwnd) {
    auto dir = dump_dir();
    if (dir.empty() || !image) return;
    char name[64];
    snprintf(name, sizeof(name), "/window-%06llx.png", static_cast<unsigned long long>(hwnd));
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:(dir + name).c_str()]];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url,
                                                                 (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    if (!dest) return;
    CGImageDestinationAddImage(dest, image, nullptr);
    CGImageDestinationFinalize(dest);
    CFRelease(dest);
}

}

// ---- クライアント領域のビュー ----

@interface NtmacView : NSView
@property(nonatomic) uint64_t hwnd;
@end

@implementation NtmacView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

/** マウスの位置にある (子) ウィンドウに，クライアント座標でメッセージを送る */
- (void)postMouse:(uint32_t)message event:(NSEvent*)e wparam:(uint64_t)extra {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    Lock lock(g_lock);
    auto& st = State::get();
    Window* top = st.window(self.hwnd);
    if (!top) return;
    auto x = static_cast<int32_t>(p.x), y = static_cast<int32_t>(p.y);
    Window* target = st.window(st.capture);
    if (!target || st.top_level(target) != top) target = st.child_from_point(top, x, y);
    if ((top->style | target->style) & kWsDisabled) return; // モーダルダイアログの表示中など
    x -= target->client_rect.left - top->client_rect.left;
    y -= target->client_rect.top - top->client_rect.top;
    st.post(target, message, mouse_key_flags(e) | extra, make_lparam(x, y));
}

- (void)mouseDown:(NSEvent*)e {
    uint32_t msg = kWmLButtonDown;
    if (e.clickCount == 2) {
        Lock lock(g_lock);
        auto w = State::get().window(self.hwnd);
        if (w && (w->cls->info.Style & kCsDblClks)) msg = kWmLButtonDblClk;
    }
    [self postMouse:msg event:e wparam:0];
}
- (void)mouseUp:(NSEvent*)e { [self postMouse:kWmLButtonUp event:e wparam:0]; }
- (void)rightMouseDown:(NSEvent*)e { [self postMouse:kWmRButtonDown event:e wparam:0]; }
- (void)rightMouseUp:(NSEvent*)e { [self postMouse:kWmRButtonUp event:e wparam:0]; }
- (void)otherMouseDown:(NSEvent*)e { [self postMouse:kWmMButtonDown event:e wparam:0]; }
- (void)otherMouseUp:(NSEvent*)e { [self postMouse:kWmMButtonUp event:e wparam:0]; }
- (void)mouseMoved:(NSEvent*)e { [self postMouse:kWmMouseMove event:e wparam:0]; }
- (void)mouseDragged:(NSEvent*)e { [self postMouse:kWmMouseMove event:e wparam:0]; }
- (void)rightMouseDragged:(NSEvent*)e { [self postMouse:kWmMouseMove event:e wparam:0]; }

- (void)scrollWheel:(NSEvent*)e {
    auto delta = static_cast<int16_t>(std::lround(e.scrollingDeltaY * (e.hasPreciseScrollingDeltas ? 4 : 120)));
    if (delta == 0) return;
    NSPoint p = [NSEvent mouseLocation];
    Lock lock(g_lock);
    auto w = State::get().window(self.hwnd);
    if (!w) return;
    WRect r = from_cocoa_rect(NSMakeRect(p.x, p.y, 0, 0));
    State::get().post(w, kWmMouseWheel, (static_cast<uint64_t>(static_cast<uint16_t>(delta)) << 16) | mouse_key_flags(e),
                      make_lparam(r.left, r.top));
}

- (void)postKey:(NSEvent*)e down:(BOOL)down {
    uint8_t vk = virtual_key(e.keyCode);
    if (!vk) return;
    BOOL alt = (e.modifierFlags & NSEventModifierFlagOption) != 0;
    BOOL ctrl = (e.modifierFlags & NSEventModifierFlagControl) != 0;
    BOOL sys = alt && !ctrl; // Alt (Option) との組み合わせは WM_SYSKEY*

    Lock lock(g_lock);
    auto& st = State::get();
    auto w = st.window(st.focus ? st.focus : self.hwnd);
    if (!w) return;

    int64_t lparam = 1 | (static_cast<int64_t>(e.keyCode) << 16) | (alt ? (1LL << 29) : 0);
    if (!down) lparam |= (1LL << 30) | (1LL << 31);
    else if (e.isARepeat) lparam |= 1LL << 30;
    if (down && !e.isARepeat) st.key_state[vk] = 0x80 | ((st.key_state[vk] & 1) ^ 1); // bit0 はトグル状態
    else if (!down) st.key_state[vk] &= 1;
    st.post(w, down ? (sys ? kWmSysKeyDown : kWmKeyDown) : (sys ? kWmSysKeyUp : kWmKeyUp), vk, lparam);

    // 文字の入力 (本来は TranslateMessage が作るが，今はここで直接送る)．Command との組み合わせは文字にしない
    if (!down || (e.modifierFlags & NSEventModifierFlagCommand)) return;
    NSString* chars = sys ? e.charactersIgnoringModifiers : e.characters;
    for (NSUInteger i = 0; i < chars.length; i++) {
        unichar c = [chars characterAtIndex:i];
        if (c >= 0xF700 && c <= 0xF8FF) continue; // 矢印キーなどの機能キー
        if (c == 0x7F) c = 0x08;                  // Delete は Backspace
        st.post(w, sys ? kWmSysChar : kWmChar, c, lparam);
    }
}

- (void)keyDown:(NSEvent*)e { [self postKey:e down:YES]; }
- (void)keyUp:(NSEvent*)e { [self postKey:e down:NO]; }

- (void)flagsChanged:(NSEvent*)e {
    struct { NSEventModifierFlags flag; uint8_t vk; } mods[] = {
        {NSEventModifierFlagShift, kVkShift}, {NSEventModifierFlagControl, kVkControl},
        {NSEventModifierFlagOption, kVkMenu}, {NSEventModifierFlagCapsLock, kVkCapital},
        {NSEventModifierFlagCommand, kVkLWin},
    };
    Lock lock(g_lock);
    auto& st = State::get();
    auto w = st.window(st.focus ? st.focus : self.hwnd);
    if (!w) return;
    for (auto& m : mods) {
        bool down = (e.modifierFlags & m.flag) != 0;
        bool was = (st.key_state[m.vk] & 0x80) != 0;
        if (down == was) continue;
        st.key_state[m.vk] = down ? (0x80 | ((st.key_state[m.vk] & 1) ^ 1)) : (st.key_state[m.vk] & 1);
        bool sys = m.vk == kVkMenu;
        st.post(w, down ? (sys ? kWmSysKeyDown : kWmKeyDown) : (sys ? kWmSysKeyUp : kWmKeyUp), m.vk,
                1 | (down ? 0 : (3LL << 30)));
    }
}

@end

// ---- ウィンドウのデリゲート ----

@interface NtmacWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) uint64_t hwnd;
@end

@implementation NtmacWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
    // 閉じるボタンは WM_SYSCOMMAND (SC_CLOSE) としてアプリに任せる (DefWindowProc が WM_CLOSE → DestroyWindow)
    Lock lock(g_lock);
    if (auto w = State::get().window(self.hwnd); w && !(w->style & kWsDisabled)) {
        State::get().post(w, kWmSysCommand, kScClose, 0);
    }
    return NO;
}

- (void)syncFrame:(NSWindow*)window {
    WRect client = from_cocoa_rect([window contentRectForFrameRect:window.frame]);
    Lock lock(g_lock);
    auto& st = State::get();
    auto w = st.window(self.hwnd);
    if (!w) return;
    bool resized = client.width() != w->client_rect.width() || client.height() != w->client_rect.height();
    bool moved = client.left != w->client_rect.left || client.top != w->client_rect.top;
    st.set_client_rect(w, client);
    if (moved) st.send_notify(w, kWmMove, 0, make_lparam(client.left, client.top));
    if (resized) {
        st.send_notify(w, kWmSize, 0 /* SIZE_RESTORED */, make_lparam(client.width(), client.height()));
        st.invalidate(w, nullptr, true);
    }
}

- (void)windowDidResize:(NSNotification*)n { [self syncFrame:n.object]; }
- (void)windowDidMove:(NSNotification*)n { [self syncFrame:n.object]; }

- (void)windowDidBecomeKey:(NSNotification*)n {
    {
        // フォーカスがこのウィンドウの子孫に無ければ，ウィンドウ自身にする
        Lock lock(g_lock);
        auto& st = State::get();
        if (!st.is_descendant(st.window(st.focus), self.hwnd)) st.focus = self.hwnd;
    }
    ntmac::win32k::cocoa::update_menu_bar(self.hwnd);
}

@end

// ---- メニュー ----

@interface NtmacMenuTarget : NSObject
@property(nonatomic) uint64_t hwnd;
- (void)command:(NSMenuItem*)item;
- (void)quit:(NSMenuItem*)item;
@end

@implementation NtmacMenuTarget

- (void)command:(NSMenuItem*)item {
    Lock lock(g_lock);
    if (auto w = State::get().window(self.hwnd)) {
        State::get().post(w, kWmCommand, static_cast<uint64_t>(item.tag) & 0xFFFF, 0);
    }
}

- (void)quit:(NSMenuItem*)item {
    Lock lock(g_lock);
    if (auto w = State::get().window(self.hwnd)) State::get().post(w, kWmSysCommand, kScClose, 0);
}

@end

namespace {

NSMutableDictionary<NSNumber*, NSWindow*>* windows() {
    static NSMutableDictionary<NSNumber*, NSWindow*>* dict = [NSMutableDictionary dictionary];
    return dict;
}

NSMutableDictionary<NSNumber*, id>* retained() { // デリゲートやメニューのターゲットを保持する
    static NSMutableDictionary<NSNumber*, id>* dict = [NSMutableDictionary dictionary];
    return dict;
}

NtmacMenuTarget* menu_target = nil;

NSMenu* build_menu(const Menu& m, NSString* title) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
    menu.autoenablesItems = NO;
    auto& st = State::get();
    for (const auto& item : m.items) {
        if (item.flags & kMfSeparator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:menu_title(item.text) action:nil keyEquivalent:@""];
        mi.enabled = !(item.flags & (kMfGrayed | kMfDisabled));
        if (item.flags & kMfChecked) mi.state = NSControlStateValueOn;
        if (item.flags & kMfPopup) {
            if (auto sub = st.menu(item.id)) mi.submenu = build_menu(*sub, mi.title);
        } else {
            mi.target = menu_target;
            mi.action = @selector(command:);
            mi.tag = static_cast<NSInteger>(item.id);
        }
        [menu addItem:mi];
    }
    return menu;
}

/** メインスレッドで呼ぶ．hwnd のメニューをメニューバーにする */
void rebuild_menu_bar(uint64_t hwnd) {
    if (!gui_active) return;
    NSString* app_name = [[NSProcessInfo processInfo] processName];
    NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];

    Lock lock(g_lock);
    auto& st = State::get();
    auto w = st.window(hwnd);
    if (!w) return;
    if (!menu_target) menu_target = [[NtmacMenuTarget alloc] init];
    menu_target.hwnd = hwnd;
    if (!w->text.empty()) app_name = to_ns(w->text);

    // アプリケーションメニュー (macOS の慣習として先頭に置く)
    NSMenuItem* app_item = [[NSMenuItem alloc] initWithTitle:app_name action:nil keyEquivalent:@""];
    NSMenu* app_menu = [[NSMenu alloc] initWithTitle:app_name];
    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:[@"終了: " stringByAppendingString:app_name]
                                                  action:@selector(quit:) keyEquivalent:@"q"];
    quit.target = menu_target;
    [app_menu addItem:quit];
    app_item.submenu = app_menu;
    [bar addItem:app_item];

    if (auto m = st.menu(w->menu_or_id)) {
        for (NSMenuItem* item in build_menu(*m, @"").itemArray.copy) {
            [item.menu removeItem:item];
            [bar addItem:item];
        }
    }
    NSApp.mainMenu = bar;
}

}

namespace ntmac::win32k::cocoa {

void show_window(const Window& w) {
    uint64_t hwnd = w.handle;
    WRect client = w.client_rect;
    uint32_t style = w.style;
    NSString* title = to_ns(w.text);
    run_on_main_async(^{
        if (!ensure_gui()) return;
        gui_active = true;
        NSWindow* window = windows()[@(hwnd)];
        if (!window) {
            NSWindowStyleMask mask = NSWindowStyleMaskBorderless;
            if ((style & kWsCaption) == kWsCaption) mask |= NSWindowStyleMaskTitled;
            if (style & kWsSysMenu) mask |= NSWindowStyleMaskClosable;
            if (style & kWsMinimizeBox) mask |= NSWindowStyleMaskMiniaturizable;
            if (style & (kWsThickFrame | kWsMaximizeBox)) mask |= NSWindowStyleMaskResizable;

            window = [[NSWindow alloc] initWithContentRect:to_cocoa_rect(client) styleMask:mask
                                                   backing:NSBackingStoreBuffered defer:NO];
            window.releasedWhenClosed = NO;
            window.acceptsMouseMovedEvents = YES;
            window.title = title;

            NtmacView* view = [[NtmacView alloc] initWithFrame:NSMakeRect(0, 0, client.width(), client.height())];
            view.hwnd = hwnd;
            view.wantsLayer = YES;
            view.layer.contentsGravity = kCAGravityTopLeft;
            view.layer.contentsScale = 1.0; // DPI 非対応のアプリとして 1 ピクセル = 1 ポイントで描く
            window.contentView = view;
            [window makeFirstResponder:view];

            NtmacWindowDelegate* delegate = [[NtmacWindowDelegate alloc] init];
            delegate.hwnd = hwnd;
            window.delegate = delegate;
            retained()[@(hwnd)] = delegate;
            windows()[@(hwnd)] = window;
        }
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    });
}

void hide_window(uint64_t hwnd) {
    run_on_main_async(^{
        [windows()[@(hwnd)] orderOut:nil];
    });
}

void destroy_window(uint64_t hwnd) {
    run_on_main_async(^{
        NSWindow* window = windows()[@(hwnd)];
        if (!window) return;
        window.delegate = nil;
        [window close];
        [windows() removeObjectForKey:@(hwnd)];
        [retained() removeObjectForKey:@(hwnd)];
    });
}

void set_title(const Window& w) {
    uint64_t hwnd = w.handle;
    NSString* title = to_ns(w.text);
    run_on_main_async(^{
        windows()[@(hwnd)].title = title;
    });
}

void present(const Window& w) {
    const auto& s = *w.surface;
    if (s.width <= 0 || s.height <= 0) return;

    // 描画面の内容を複製して画像にする (g_lock を持っている間に行う)
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(s.pixels.data()),
                                  static_cast<CFIndex>(s.pixels.size() * sizeof(uint32_t)));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef image = CGImageCreate(s.width, s.height, 8, 32, s.width * 4, space,
                                     static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) |
                                         static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst),
                                     provider, nullptr,
                                     false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    CFRelease(data);

    dump_png(image, w.handle);

    uint64_t hwnd = w.handle;
    run_on_main_async(^{
        NSWindow* window = windows()[@(hwnd)];
        if (window) window.contentView.layer.contents = (__bridge id)image;
        CGImageRelease(image);
    });
}

void update_menu_bar(uint64_t hwnd) {
    run_on_main_async(^{
        NSWindow* key = NSApp.keyWindow;
        if (key && key != windows()[@(hwnd)]) return; // 他のウィンドウがアクティブ
        rebuild_menu_bar(hwnd);
    });
}

void prepare() {
    if (!ensure_gui()) return;
    static std::once_flag once;
    std::call_once(once, [] {
        run_on_main_sync(^{
            gui_active = true;
            NSScreen* primary = [NSScreen screens].firstObject;
            if (!primary) return;
            WRect screen = from_cocoa_rect(primary.frame);
            WRect work = from_cocoa_rect(primary.visibleFrame);
            Lock lock(g_lock);
            State::get().screen = {screen, work};
        });
    });
}

}
