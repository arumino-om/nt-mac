#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "shared/ntmachost_user.h"

// ウィンドウ管理の状態 (Windows の win32k に相当)．
//
// PE のスレッド (user32 からの呼び出し) と，メインスレッド (Cocoa のイベント) の両方から使う．
// すべて g_lock で保護する．g_lock を持ったままメインスレッドの完了を待ってはいけない (デッドロックする)．
// Cocoa への反映は run_on_main_async で非同期に行う．

namespace ntmac::win32k {

extern std::recursive_mutex g_lock;
using Lock = std::lock_guard<std::recursive_mutex>;

struct Rect {
    int32_t left = 0, top = 0, right = 0, bottom = 0;

    int32_t width() const { return right - left; }
    int32_t height() const { return bottom - top; }
    bool empty() const { return right <= left || bottom <= top; }
    Rect offset(int32_t dx, int32_t dy) const { return {left + dx, top + dy, right + dx, bottom + dy}; }
    Rect intersect(const Rect& o) const;
    Rect unite(const Rect& o) const;

    static Rect from(const ntmac_rect& r) { return {r.left, r.top, r.right, r.bottom}; }
    ntmac_rect to_ntmac() const { return {left, top, right, bottom}; }
};

struct WindowClass {
    std::u16string name;
    uint16_t atom = 0;
    ntmachost_class_info info{};
    std::u16string menu_name;       // info.MenuName が文字列の場合の保持先
    bool menu_name_is_string = false;
};

/** トップレベルウィンドウの描画面 (32bpp，BGRA)．子ウィンドウは親の描画面に描く */
struct Surface {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint32_t> pixels;

    void resize(int32_t w, int32_t h);
    void fill(const Rect& r, uint32_t bgra);
};

struct Window {
    uint64_t handle = 0;
    WindowClass* cls = nullptr;
    uint32_t style = 0;
    uint32_t ex_style = 0;
    uint64_t parent = 0;
    uint64_t instance = 0;
    uint64_t menu_or_id = 0;
    void* wndproc = nullptr;
    int64_t user_data = 0;
    std::vector<uint8_t> extra;
    std::u16string text;

    Rect window_rect;               // スクリーン座標
    Rect client_rect;               // スクリーン座標
    bool visible = false;

    Rect update;                    // 更新が必要な領域 (クライアント座標)
    bool erase = false;

    const void* thread = nullptr;   // 所有するスレッド (TEB)
    std::shared_ptr<Surface> surface; // トップレベルのみ (NSWindow は cocoa.mm がメインスレッドで管理する)
};

struct MenuItem {
    uint32_t flags = 0;
    uint64_t id = 0;                // MF_POPUP ならサブメニューのハンドル
    std::u16string text;
};

struct Menu {
    uint64_t handle = 0;
    bool popup = false;
    std::vector<MenuItem> items;
};

/** フォントの指定 (LOGFONTW の主な項目) */
struct FontSpec {
    std::u16string face;
    int32_t height = 16;            // LOGFONT と同じく，負なら文字の大きさ，正ならセルの高さ (ピクセル)
    int32_t weight = 700;
    bool italic = false;
    bool underline = false;
    bool strike_out = false;

    static FontSpec system_font() { return {u"System", 16, 700}; }
};

/** デバイスコンテキスト．今はウィンドウのクライアント領域に描くものだけ */
struct DeviceContext {
    uint64_t handle = 0;
    uint64_t window = 0;
    Rect clip;                      // クライアント座標

    uint32_t text_color = 0x000000; // COLORREF
    uint32_t bk_color = 0xFFFFFF;
    uint32_t bk_mode = 2;           // OPAQUE
    uint32_t text_align = 0;        // TA_LEFT | TA_TOP
    uint64_t font_handle = 0;       // PE 側のフォントのハンドル (0 はシステムフォント)
    FontSpec font = FontSpec::system_font();
};

struct Timer {
    uint64_t window = 0;
    uint64_t id = 0;
    uint32_t interval = 0;
    uint64_t due = 0;               // 次に WM_TIMER を出す時刻 (ミリ秒)
    void* proc = nullptr;
};

struct MessageQueue {
    std::deque<ntmachost_msg> posted;
    std::deque<ntmachost_msg> sent;  // ホストからの通知 (WM_SIZE など)．応答は不要
    bool quit = false;
    int32_t quit_code = 0;
    std::vector<Timer> timers;
    uint64_t next_timer_id = 0x7FFF0000;
    std::condition_variable_any cv;
};

struct ScreenInfo {
    Rect screen{0, 0, 1440, 900};
    Rect work_area{0, 25, 1440, 900};
};

class State {
public:
    static State& get();

    // クラス
    WindowClass* find_class(const uint16_t* name_or_atom);
    WindowClass* register_class(const ntmachost_class_info& info);

    // ウィンドウ
    Window* window(uint64_t handle);
    Window* create_window(const ntmachost_create_window_args& args, WindowClass* cls, const void* thread);
    void destroy_window(uint64_t handle);
    void set_client_rect(Window* w, const Rect& client);   // 大きさが変わったら描画面も変える
    Window* top_level(Window* w);
    /** トップレベルのクライアント座標 (x, y) にある，最も手前の表示中の子孫ウィンドウ (無ければ top) */
    Window* child_from_point(Window* top, int32_t x, int32_t y);
    std::vector<Window*> windows_of_thread(const void* thread);

    // 更新領域
    void invalidate(Window* w, const Rect* rect, bool erase);
    void validate(Window* w, const Rect* rect);

    // メッセージ
    MessageQueue& queue(const void* thread);
    void post(Window* w, uint32_t message, uint64_t wparam, int64_t lparam);
    void send_notify(Window* w, uint32_t message, uint64_t wparam, int64_t lparam);

    // メニュー
    Menu* menu(uint64_t handle);
    Menu* create_menu(bool popup);
    void destroy_menu(uint64_t handle);

    // DC
    DeviceContext* dc(uint64_t handle);
    DeviceContext* create_dc(Window* w, const Rect& clip);
    void release_dc(uint64_t handle);

    // キーボード
    uint8_t key_state[256] = {};    // bit7 = 押されている，bit0 = トグル

    /** w が ancestor 自身かその子孫か */
    bool is_descendant(Window* w, uint64_t ancestor);
    /** GetWindow (GW_*) と同じ．子の順序は作成順 */
    uint64_t related(Window* w, int32_t cmd);

    ScreenInfo screen;
    uint64_t focus = 0;
    uint64_t capture = 0;

private:
    uint64_t next_handle(uint32_t tag);

    std::vector<std::unique_ptr<WindowClass>> classes_;
    std::map<uint64_t, std::unique_ptr<Window>> windows_;
    std::map<uint64_t, std::unique_ptr<Menu>> menus_;
    std::map<uint64_t, std::unique_ptr<DeviceContext>> dcs_;
    std::map<const void*, std::unique_ptr<MessageQueue>> queues_;
    uint32_t handle_counter_ = 0;
    uint16_t next_atom_ = 0xC000;
    int cascade_ = 0;
};

uint32_t current_time_ms();
uint64_t current_time_ms64();

}
