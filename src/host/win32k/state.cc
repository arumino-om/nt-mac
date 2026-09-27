#include <mach/mach_time.h>
#include <algorithm>
#include "host/win32k/cocoa.h"
#include "host/win32k/state.h"

namespace ntmac::win32k {

std::recursive_mutex g_lock;

namespace {

// Windows の定数 (user32 と同じ値)
constexpr uint32_t kWsChild = 0x40000000;
constexpr uint32_t kWsCaption = 0x00C00000;
constexpr int32_t kCwUseDefault = static_cast<int32_t>(0x80000000);
constexpr int32_t kCaptionHeight = 28;          // NSWindow のタイトルバーの高さに合わせる (GetSystemMetrics と同じ値)

// ハンドルの種類 (上位ビット)．値は 32 ビットに収める (Windows の HWND と同じ)
constexpr uint32_t kTagWindow = 0x1;
constexpr uint32_t kTagMenu = 0x2;
constexpr uint32_t kTagDc = 0x3;

bool is_int_resource(const uint16_t* p) {
    return (reinterpret_cast<uintptr_t>(p) >> 16) == 0;
}

std::u16string from_pe(const uint16_t* s) {
    std::u16string out;
    if (!s) return out;
    for (; *s; s++) out.push_back(static_cast<char16_t>(*s));
    return out;
}

char16_t upcase(char16_t c) {
    return c >= u'a' && c <= u'z' ? c - (u'a' - u'A') : c;
}

bool equal_ignore_case(const std::u16string& a, const std::u16string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                              [](char16_t x, char16_t y) { return upcase(x) == upcase(y); });
}

}

uint64_t current_time_ms64() {
    static const mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t{};
        mach_timebase_info(&t);
        return t;
    }();
    return static_cast<uint64_t>(static_cast<unsigned __int128>(mach_absolute_time()) * tb.numer / tb.denom / 1000000);
}

uint32_t current_time_ms() {
    return static_cast<uint32_t>(current_time_ms64());
}

Rect Rect::intersect(const Rect& o) const {
    Rect r{std::max(left, o.left), std::max(top, o.top), std::min(right, o.right), std::min(bottom, o.bottom)};
    return r.empty() ? Rect{} : r;
}

Rect Rect::unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    return {std::min(left, o.left), std::min(top, o.top), std::max(right, o.right), std::max(bottom, o.bottom)};
}

void Surface::resize(int32_t w, int32_t h) {
    std::vector<uint32_t> next(static_cast<size_t>(std::max(w, 0)) * std::max(h, 0), 0xFFFFFFFF);
    for (int32_t y = 0; y < std::min(h, height); y++) {
        std::copy_n(&pixels[static_cast<size_t>(y) * width], std::min(w, width), &next[static_cast<size_t>(y) * w]);
    }
    pixels = std::move(next);
    width = w;
    height = h;
}

void Surface::fill(const Rect& r, uint32_t bgra) {
    Rect c = r.intersect({0, 0, width, height});
    for (int32_t y = c.top; y < c.bottom; y++) {
        std::fill_n(&pixels[static_cast<size_t>(y) * width + c.left], c.width(), bgra);
    }
}

State& State::get() {
    static State state;
    return state;
}

uint64_t State::next_handle(uint32_t tag) {
    return (static_cast<uint64_t>(tag) << 24) | (++handle_counter_ & 0xFFFFFF);
}

// ---- クラス ----

WindowClass* State::find_class(const uint16_t* name_or_atom) {
    if (is_int_resource(name_or_atom)) {
        auto atom = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(name_or_atom));
        for (auto& c : classes_) {
            if (c->atom == atom) return c.get();
        }
        return nullptr;
    }
    auto name = from_pe(name_or_atom);
    for (auto& c : classes_) {
        if (equal_ignore_case(c->name, name)) return c.get();
    }
    return nullptr;
}

WindowClass* State::register_class(const ntmachost_class_info& info) {
    if (is_int_resource(info.ClassName) || find_class(info.ClassName)) return nullptr;

    auto cls = std::make_unique<WindowClass>();
    cls->name = from_pe(info.ClassName);
    cls->atom = next_atom_++;
    cls->info = info;
    if (info.MenuName && !is_int_resource(info.MenuName)) {
        cls->menu_name = from_pe(info.MenuName);
        cls->menu_name_is_string = true;
    }
    cls->info.MenuName = cls->menu_name_is_string ? reinterpret_cast<const uint16_t*>(cls->menu_name.c_str())
                                                  : info.MenuName;
    cls->info.ClassName = reinterpret_cast<const uint16_t*>(cls->name.c_str());
    classes_.push_back(std::move(cls));
    return classes_.back().get();
}

// ---- ウィンドウ ----

Window* State::window(uint64_t handle) {
    auto it = windows_.find(handle);
    return it == windows_.end() ? nullptr : it->second.get();
}

Window* State::create_window(const ntmachost_create_window_args& args, WindowClass* cls, const void* thread) {
    auto w = std::make_unique<Window>();
    w->handle = next_handle(kTagWindow);
    w->cls = cls;
    w->style = args.Style;
    w->ex_style = args.ExStyle;
    w->parent = args.Parent;
    w->instance = args.Instance;
    w->menu_or_id = args.Menu;
    w->wndproc = cls->info.WndProc;
    w->extra.resize(static_cast<size_t>(std::max(cls->info.WindowExtra, 0)));
    w->text = from_pe(args.WindowName);
    w->thread = thread;

    int32_t x = args.X, y = args.Y, width = args.Width, height = args.Height;
    Window* parent = window(args.Parent);
    if ((args.Style & kWsChild) && parent) {
        // 子ウィンドウの座標は親のクライアント座標
        if (x == kCwUseDefault) x = y = 0;
        if (width == kCwUseDefault) width = height = 0;
        w->window_rect = Rect{x, y, x + width, y + height}.offset(parent->client_rect.left, parent->client_rect.top);
        w->client_rect = w->window_rect;
    } else {
        const Rect& work = screen.work_area;
        if (x == kCwUseDefault) {
            x = work.left + 40 + (cascade_ % 8) * 28;
            y = work.top + 40 + (cascade_ % 8) * 28;
            cascade_++;
        }
        if (width == kCwUseDefault) {
            width = work.width() * 3 / 4;
            height = work.height() * 3 / 4;
        }
        w->window_rect = {x, y, x + width, y + height};
        w->client_rect = w->window_rect;
        if ((args.Style & kWsCaption) == kWsCaption) w->client_rect.top += kCaptionHeight;
        w->surface = std::make_shared<Surface>();
        w->surface->resize(w->client_rect.width(), w->client_rect.height());
    }

    auto* raw = w.get();
    windows_[raw->handle] = std::move(w);
    return raw;
}

void State::destroy_window(uint64_t handle) {
    // 子ウィンドウも一緒に消す
    std::vector<uint64_t> children;
    for (auto& [h, w] : windows_) {
        if (w->parent == handle) children.push_back(h);
    }
    for (auto h : children) destroy_window(h);

    auto it = windows_.find(handle);
    if (it == windows_.end()) return;
    if (it->second->surface) cocoa::destroy_window(handle);
    if (focus == handle) focus = 0;
    if (capture == handle) capture = 0;
    auto& timers = queue(it->second->thread).timers;
    std::erase_if(timers, [handle](const Timer& t) { return t.window == handle; });
    windows_.erase(it);
}

void State::set_client_rect(Window* w, const Rect& client) {
    int32_t dx = client.left - w->client_rect.left, dy = client.top - w->client_rect.top;
    int32_t dw = client.width() - w->client_rect.width(), dh = client.height() - w->client_rect.height();
    w->window_rect = {w->window_rect.left + dx, w->window_rect.top + dy, w->window_rect.right + dx + dw,
                      w->window_rect.bottom + dy + dh};
    w->client_rect = client;
    if (w->surface && (w->surface->width != client.width() || w->surface->height != client.height())) {
        w->surface->resize(client.width(), client.height());
    }
}

Window* State::top_level(Window* w) {
    while (w && !w->surface) w = window(w->parent);
    return w;
}

Window* State::child_from_point(Window* top, int32_t x, int32_t y) {
    int32_t sx = x + top->client_rect.left, sy = y + top->client_rect.top; // スクリーン座標
    Window* found = top;
    for (bool descended = true; descended;) {
        descended = false;
        // 後から作られたウィンドウほど手前にあるとみなす (Z オーダーは未実装)
        for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
            Window* c = it->second.get();
            if (c->parent != found->handle || !c->visible) continue;
            const Rect& r = c->window_rect;
            if (sx >= r.left && sx < r.right && sy >= r.top && sy < r.bottom) {
                found = c;
                descended = true;
                break;
            }
        }
    }
    return found;
}

bool State::is_descendant(Window* w, uint64_t ancestor) {
    for (; w; w = window(w->parent)) {
        if (w->handle == ancestor) return true;
        if (!(w->style & 0x40000000)) break; // WS_CHILD でなければ parent は所有者
    }
    return false;
}

uint64_t State::related(Window* w, int32_t cmd) {
    constexpr int32_t kFirst = 0, kLast = 1, kNext = 2, kPrev = 3, kOwner = 4, kChild = 5;
    auto siblings_of = [&](uint64_t parent) {
        std::vector<uint64_t> out;
        for (auto& [h, c] : windows_) {
            if (c->parent == parent && (c->style & 0x40000000)) out.push_back(h); // map はハンドル (作成) 順
        }
        return out;
    };
    if (cmd == kChild) {
        auto children = siblings_of(w->handle);
        return children.empty() ? 0 : children.front();
    }
    if (cmd == kOwner) return (w->style & 0x40000000) ? 0 : w->parent;
    if (!(w->style & 0x40000000)) return 0; // トップレベルの兄弟 (Z オーダー) は未実装
    auto siblings = siblings_of(w->parent);
    auto it = std::find(siblings.begin(), siblings.end(), w->handle);
    switch (cmd) {
    case kFirst: return siblings.front();
    case kLast:  return siblings.back();
    case kNext:  return (it != siblings.end() && it + 1 != siblings.end()) ? *(it + 1) : 0;
    case kPrev:  return (it != siblings.end() && it != siblings.begin()) ? *(it - 1) : 0;
    default:     return 0;
    }
}

std::vector<Window*> State::windows_of_thread(const void* thread) {
    std::vector<Window*> out;
    for (auto& [h, w] : windows_) {
        if (w->thread == thread) out.push_back(w.get());
    }
    return out;
}

// ---- 更新領域 ----

void State::invalidate(Window* w, const Rect* rect, bool erase) {
    Rect whole{0, 0, w->client_rect.width(), w->client_rect.height()};
    Rect r = rect ? rect->intersect(whole) : whole;
    if (r.empty()) return;
    w->update = w->update.unite(r);
    w->erase = w->erase || erase;
    queue(w->thread).cv.notify_all(); // WM_PAINT は GetMessage の中で作る

    // 子ウィンドウは親と同じ描画面に描くので，親を描き直すと子も上書きされる．重なる子も描き直す
    Rect screen_rect = r.offset(w->client_rect.left, w->client_rect.top);
    for (auto& [h, c] : windows_) {
        if (c->parent != w->handle || !(c->style & 0x40000000) || !c->visible) continue;
        Rect overlap = screen_rect.intersect(c->client_rect);
        if (overlap.empty()) continue;
        Rect local = overlap.offset(-c->client_rect.left, -c->client_rect.top);
        invalidate(c.get(), &local, true);
    }
}

void State::validate(Window* w, const Rect* rect) {
    if (!rect || (rect->left <= w->update.left && rect->top <= w->update.top && rect->right >= w->update.right &&
                  rect->bottom >= w->update.bottom)) {
        w->update = {};
        w->erase = false;
    }
    // 一部だけの有効化は，領域を矩形1つで持っているため無視する (その部分も描き直される)
}

// ---- メッセージ ----

MessageQueue& State::queue(const void* thread) {
    auto& q = queues_[thread];
    if (!q) q = std::make_unique<MessageQueue>();
    return *q;
}

void State::post(Window* w, uint32_t message, uint64_t wparam, int64_t lparam) {
    ntmachost_msg m{};
    m.Window = w->handle;
    m.Message = message;
    m.WParam = wparam;
    m.LParam = lparam;
    m.Time = current_time_ms();
    auto& q = queue(w->thread);
    q.posted.push_back(m);
    q.cv.notify_all();
}

void State::send_notify(Window* w, uint32_t message, uint64_t wparam, int64_t lparam) {
    ntmachost_msg m{};
    m.Window = w->handle;
    m.Message = message;
    m.WParam = wparam;
    m.LParam = lparam;
    m.Time = current_time_ms();
    auto& q = queue(w->thread);
    q.sent.push_back(m);
    q.cv.notify_all();
}

// ---- メニュー ----

Menu* State::menu(uint64_t handle) {
    auto it = menus_.find(handle);
    return it == menus_.end() ? nullptr : it->second.get();
}

Menu* State::create_menu(bool popup) {
    auto m = std::make_unique<Menu>();
    m->handle = next_handle(kTagMenu);
    m->popup = popup;
    auto* raw = m.get();
    menus_[raw->handle] = std::move(m);
    return raw;
}

void State::destroy_menu(uint64_t handle) {
    auto m = menu(handle);
    if (!m) return;
    std::vector<uint64_t> subs;
    for (auto& item : m->items) {
        if (item.flags & 0x10 /* MF_POPUP */) subs.push_back(item.id);
    }
    menus_.erase(handle);
    for (auto h : subs) destroy_menu(h);
}

// ---- DC ----

DeviceContext* State::dc(uint64_t handle) {
    auto it = dcs_.find(handle);
    return it == dcs_.end() ? nullptr : it->second.get();
}

DeviceContext* State::create_dc(Window* w, const Rect& clip) {
    auto d = std::make_unique<DeviceContext>();
    d->handle = next_handle(kTagDc);
    d->window = w->handle;
    d->clip = clip;
    auto* raw = d.get();
    dcs_[raw->handle] = std::move(d);
    return raw;
}

void State::release_dc(uint64_t handle) {
    dcs_.erase(handle);
}

}
