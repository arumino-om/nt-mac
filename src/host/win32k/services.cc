#include <algorithm>
#include <chrono>
#include <cstring>
#include "host/runtime/process_env.h"
#include "host/win32k/cocoa.h"
#include "host/win32k/gdi.h"
#include "host/win32k/main_thread.h"
#include "host/win32k/services.h"
#include "host/win32k/state.h"

namespace ntmac::win32k {

namespace {

// Windows の定数
constexpr uint32_t kWsVisible = 0x10000000;
constexpr uint32_t kWmPaint = 0x000F;
constexpr uint32_t kWmQuit = 0x0012;
constexpr uint32_t kWmTimer = 0x0113;

constexpr int32_t kGwlpWndProc = -4, kGwlpHInstance = -6, kGwlpHwndParent = -8, kGwlpId = -12;
constexpr int32_t kGwlStyle = -16, kGwlExStyle = -20, kGwlpUserData = -21;
constexpr int32_t kGclpMenuName = -8, kGclpHbrBackground = -10, kGclpHCursor = -12, kGclpHIcon = -14,
                  kGclpHModule = -16, kGclCbWndExtra = -18, kGclCbClsExtra = -20, kGclpWndProc = -24,
                  kGclStyle = -26, kGcwAtom = -32, kGclpHIconSm = -34;


const void* current_thread() {
    return runtime::current_teb();
}

State& state() {
    return State::get();
}

Window* get_window(uint64_t handle) {
    return state().window(handle);
}

/** 描画面の上での，ウィンドウのクライアント領域の位置 */
void client_origin(Window* w, Window* top, int32_t& dx, int32_t& dy) {
    dx = w->client_rect.left - top->client_rect.left;
    dy = w->client_rect.top - top->client_rect.top;
}

uint32_t colorref_to_bgra(uint32_t c) {
    uint32_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

bool message_matches(const ntmachost_msg& m, const ntmachost_msg_args* a) {
    if (a->FilterWindow && m.Window != a->FilterWindow) return false;
    if (a->FilterMin == 0 && a->FilterMax == 0) return true;
    return m.Message >= a->FilterMin && m.Message <= a->FilterMax;
}

// ---- クラス ----

ntmac_ntstatus register_class(ntmachost_class_args* a) {
    auto cls = state().register_class(a->Info);
    if (!cls) return NTMAC_STATUS_INVALID_PARAMETER; // 同じ名前のクラスが既にある
    a->Atom = cls->atom;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_class_info(ntmachost_class_args* a) {
    auto cls = state().find_class(a->Info.ClassName);
    if (!cls) return NTMAC_STATUS_INVALID_PARAMETER;
    a->Info = cls->info;
    a->Atom = cls->atom;
    return NTMAC_STATUS_SUCCESS;
}

// ---- ウィンドウ ----

ntmac_ntstatus create_window(ntmachost_create_window_args* a) {
    auto cls = state().find_class(a->ClassName);
    if (!cls) return NTMAC_STATUS_INVALID_PARAMETER;
    auto w = state().create_window(*a, cls, current_thread());
    a->Window = w->handle;
    a->WndProc = w->wndproc;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus destroy_window(ntmachost_window_args* a) {
    if (!get_window(a->Window)) return NTMAC_STATUS_INVALID_HANDLE;
    state().destroy_window(a->Window);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_window_long(ntmachost_window_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    switch (a->Index) {
    case kGwlpWndProc:    a->Value = reinterpret_cast<int64_t>(w->wndproc); break;
    case kGwlpHInstance:  a->Value = static_cast<int64_t>(w->instance); break;
    case kGwlpHwndParent: a->Value = static_cast<int64_t>(w->parent); break;
    case kGwlpId:         a->Value = static_cast<int64_t>(w->menu_or_id); break;
    case kGwlStyle:       a->Value = w->style; break;
    case kGwlExStyle:     a->Value = w->ex_style; break;
    case kGwlpUserData:   a->Value = w->user_data; break;
    default: {
        if (a->Index < 0 || static_cast<size_t>(a->Index) + a->Size > w->extra.size()) return NTMAC_STATUS_INVALID_PARAMETER;
        a->Value = 0;
        std::memcpy(&a->Value, &w->extra[a->Index], a->Size);
        if (a->Size == 4) a->Value = static_cast<int32_t>(a->Value);
    }
    }
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus set_window_long(ntmachost_window_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    int64_t old = 0;
    switch (a->Index) {
    case kGwlpWndProc:   old = reinterpret_cast<int64_t>(w->wndproc); w->wndproc = reinterpret_cast<void*>(a->Value); break;
    case kGwlpHInstance: old = static_cast<int64_t>(w->instance); w->instance = static_cast<uint64_t>(a->Value); break;
    case kGwlpId:        old = static_cast<int64_t>(w->menu_or_id); w->menu_or_id = static_cast<uint64_t>(a->Value); break;
    case kGwlStyle:      old = w->style; w->style = static_cast<uint32_t>(a->Value); break;
    case kGwlExStyle:    old = w->ex_style; w->ex_style = static_cast<uint32_t>(a->Value); break;
    case kGwlpUserData:  old = w->user_data; w->user_data = a->Value; break;
    default: {
        if (a->Index < 0 || static_cast<size_t>(a->Index) + a->Size > w->extra.size()) return NTMAC_STATUS_INVALID_PARAMETER;
        std::memcpy(&old, &w->extra[a->Index], a->Size);
        std::memcpy(&w->extra[a->Index], &a->Value, a->Size);
    }
    }
    a->Value = old;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_class_long(ntmachost_window_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    const auto& info = w->cls->info;
    switch (a->Index) {
    case kGclpMenuName:      a->Value = reinterpret_cast<int64_t>(info.MenuName); break;
    case kGclpHbrBackground: a->Value = static_cast<int64_t>(info.Background); break;
    case kGclpHCursor:       a->Value = static_cast<int64_t>(info.Cursor); break;
    case kGclpHIcon:         a->Value = static_cast<int64_t>(info.Icon); break;
    case kGclpHModule:       a->Value = static_cast<int64_t>(info.Instance); break;
    case kGclCbWndExtra:     a->Value = info.WindowExtra; break;
    case kGclCbClsExtra:     a->Value = info.ClassExtra; break;
    case kGclpWndProc:       a->Value = reinterpret_cast<int64_t>(info.WndProc); break;
    case kGclStyle:          a->Value = info.Style; break;
    case kGcwAtom:           a->Value = w->cls->atom; break;
    case kGclpHIconSm:       a->Value = static_cast<int64_t>(info.IconSmall); break;
    default:                 return NTMAC_STATUS_INVALID_PARAMETER;
    }
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_window_text(ntmachost_text_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    if (a->Buffer && a->Length > 0) {
        size_t n = std::min<size_t>(w->text.size(), a->Length - 1);
        std::memcpy(a->Buffer, w->text.data(), n * sizeof(char16_t));
        a->Buffer[n] = 0;
        a->Length = static_cast<uint32_t>(n);
    } else {
        a->Length = static_cast<uint32_t>(w->text.size());
    }
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus set_window_text(ntmachost_text_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    w->text.clear();
    for (const uint16_t* p = a->Buffer; p && *p; p++) w->text.push_back(static_cast<char16_t>(*p));
    if (w->surface) cocoa::set_title(*w);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus show_window(ntmachost_window_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    bool was_visible = w->visible;
    bool show = a->Index != 0; // SW_HIDE 以外は表示する (最小化・最大化は未対応)
    a->Result = was_visible;
    if (show == was_visible) return NTMAC_STATUS_SUCCESS;

    w->visible = show;
    if (show) {
        w->style |= kWsVisible;
        state().invalidate(w, nullptr, true);
        if (w->surface) cocoa::show_window(*w);
        else if (auto top = state().top_level(w)) state().invalidate(top, nullptr, true); // 親ごと描き直す
    } else {
        w->style &= ~kWsVisible;
        if (w->surface) cocoa::hide_window(w->handle);
    }
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_window_rect(ntmachost_rect_args* a, bool client) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    a->Rect = client ? Rect{0, 0, w->client_rect.width(), w->client_rect.height()}.to_ntmac() : w->window_rect.to_ntmac();
    return NTMAC_STATUS_SUCCESS;
}

// ---- メッセージ ----

ntmac_ntstatus get_message(ntmachost_msg_args* a) {
    auto thread = current_thread();
    std::unique_lock lock(g_lock);
    for (;;) {
        auto& q = state().queue(thread);

        if (!q.sent.empty()) {
            a->Msg = q.sent.front();
            q.sent.pop_front();
            a->Result = NTMAC_MSG_SENT;
            return NTMAC_STATUS_SUCCESS;
        }

        for (auto it = q.posted.begin(); it != q.posted.end(); ++it) {
            if (!message_matches(*it, a)) continue;
            a->Msg = *it;
            if (a->Flags & NTMAC_GET_REMOVE) q.posted.erase(it);
            a->Result = NTMAC_MSG_POSTED;
            return NTMAC_STATUS_SUCCESS;
        }

        if (q.quit && !a->FilterWindow) {
            a->Msg = {};
            a->Msg.Message = kWmQuit;
            a->Msg.WParam = static_cast<uint64_t>(q.quit_code);
            if (a->Flags & NTMAC_GET_REMOVE) q.quit = false;
            a->Result = NTMAC_MSG_QUIT;
            return NTMAC_STATUS_SUCCESS;
        }

        // 他にメッセージが無ければ，更新が必要なウィンドウに WM_PAINT を作る (キューには入れない)．
        // 子ウィンドウは親の上に描くので，親 (作成が先のもの) から順に描く
        for (auto w : state().windows_of_thread(thread)) {
            if (!w->visible || w->update.empty()) continue;
            if (auto p = get_window(w->parent); p && (w->style & 0x40000000) && !p->update.empty()) continue;
            ntmachost_msg m{};
            m.Window = w->handle;
            m.Message = kWmPaint;
            m.Time = current_time_ms();
            if (!message_matches(m, a)) continue;
            a->Msg = m;
            a->Result = NTMAC_MSG_POSTED;
            return NTMAC_STATUS_SUCCESS;
        }

        // タイマーは最も優先度が低い
        uint64_t now = current_time_ms64();
        uint64_t next_due = UINT64_MAX;
        for (auto& t : q.timers) {
            ntmachost_msg m{};
            m.Window = t.window;
            m.Message = kWmTimer;
            m.WParam = t.id;
            m.LParam = reinterpret_cast<int64_t>(t.proc);
            m.Time = static_cast<uint32_t>(now);
            if (!message_matches(m, a)) continue;
            if (t.due <= now) {
                if (a->Flags & NTMAC_GET_REMOVE) t.due = now + t.interval;
                a->Msg = m;
                a->Result = NTMAC_MSG_POSTED;
                return NTMAC_STATUS_SUCCESS;
            }
            next_due = std::min(next_due, t.due);
        }

        if (!(a->Flags & NTMAC_GET_WAIT)) {
            a->Result = NTMAC_MSG_NONE;
            return NTMAC_STATUS_SUCCESS;
        }
        if (next_due == UINT64_MAX) {
            q.cv.wait(lock);
        } else {
            q.cv.wait_for(lock, std::chrono::milliseconds(next_due - now));
        }
    }
}

ntmac_ntstatus set_timer(ntmachost_timer_args* a) {
    if (a->Window && !get_window(a->Window)) return NTMAC_STATUS_INVALID_HANDLE;
    const void* thread = a->Window ? get_window(a->Window)->thread : current_thread();
    auto& q = state().queue(thread);
    if (!a->Window) a->Id = ++q.next_timer_id;
    uint32_t interval = std::max<uint32_t>(a->Interval, 10); // USER_TIMER_MINIMUM
    for (auto& t : q.timers) {
        if (t.window == a->Window && t.id == a->Id) {
            t.interval = interval;
            t.due = current_time_ms64() + interval;
            t.proc = a->Proc;
            q.cv.notify_all();
            return NTMAC_STATUS_SUCCESS;
        }
    }
    q.timers.push_back({a->Window, a->Id, interval, current_time_ms64() + interval, a->Proc});
    q.cv.notify_all();
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus kill_timer(ntmachost_timer_args* a) {
    const void* thread = a->Window ? (get_window(a->Window) ? get_window(a->Window)->thread : nullptr) : current_thread();
    if (!thread) return NTMAC_STATUS_INVALID_HANDLE;
    auto& timers = state().queue(thread).timers;
    auto n = std::erase_if(timers, [a](const Timer& t) { return t.window == a->Window && t.id == a->Id; });
    return n ? NTMAC_STATUS_SUCCESS : NTMAC_STATUS_INVALID_PARAMETER;
}

ntmac_ntstatus post_message(ntmachost_msg_args* a) {
    if (a->Msg.Window == 0) {
        // ウィンドウを指定しないメッセージは，呼び出したスレッドのキューに入れる (PostThreadMessage と同じ)
        auto& q = state().queue(current_thread());
        a->Msg.Time = current_time_ms();
        q.posted.push_back(a->Msg);
        q.cv.notify_all();
        return NTMAC_STATUS_SUCCESS;
    }
    auto w = get_window(a->Msg.Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    state().post(w, a->Msg.Message, a->Msg.WParam, a->Msg.LParam);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus post_quit_message(ntmachost_window_args* a) {
    auto& q = state().queue(current_thread());
    q.quit = true;
    q.quit_code = static_cast<int32_t>(a->Value);
    q.cv.notify_all();
    return NTMAC_STATUS_SUCCESS;
}

// ---- 描画 ----

ntmac_ntstatus invalidate_rect(ntmachost_rect_args* a, bool validate) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    Rect r = Rect::from(a->Rect);
    const Rect* rp = (a->Flags & NTMAC_RECT_WHOLE) ? nullptr : &r;
    if (validate) state().validate(w, rp);
    else state().invalidate(w, rp, a->Flags & NTMAC_RECT_ERASE);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_update_rect(ntmachost_rect_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    a->Rect = w->update.to_ntmac();
    a->Result = !w->update.empty();
    a->Flags = w->erase ? NTMAC_RECT_ERASE : 0;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus begin_paint(ntmachost_paint_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    Rect update = w->update;
    a->Paint = update.to_ntmac();
    a->Erase = w->erase;
    state().validate(w, nullptr);
    a->Dc = state().create_dc(w, update)->handle;
    return NTMAC_STATUS_SUCCESS;
}

void present_window_of_dc(uint64_t hdc) {
    auto d = state().dc(hdc);
    if (!d) return;
    auto w = get_window(d->window);
    auto top = state().top_level(w);
    if (top && top->visible) cocoa::present(*top);
}

ntmac_ntstatus end_paint(ntmachost_paint_args* a) {
    present_window_of_dc(a->Dc);
    state().release_dc(a->Dc);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_dc(ntmachost_window_args* a) {
    if (a->Window == 0) {
        // 画面の DC．描画はできないが，文字の寸法などを調べるのに使える
        static Window screen_window; // handle = 0
        a->Value = static_cast<int64_t>(state().create_dc(&screen_window, {})->handle);
        return NTMAC_STATUS_SUCCESS;
    }
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    a->Value = static_cast<int64_t>(state().create_dc(w, {0, 0, w->client_rect.width(), w->client_rect.height()})->handle);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus release_dc(ntmachost_window_args* a) {
    present_window_of_dc(static_cast<uint64_t>(a->Value));
    state().release_dc(static_cast<uint64_t>(a->Value));
    return NTMAC_STATUS_SUCCESS;
}

/** DC の描画先: トップレベルの描画面と，クライアント座標から描画面の座標へのずれ */
struct Target {
    DeviceContext* dc = nullptr;
    Surface* surface = nullptr;
    int32_t dx = 0, dy = 0;

    Rect to_surface(const Rect& r) const { return r.offset(dx, dy); }
    Rect clip() const { return dc->clip.offset(dx, dy); }
};

bool target_of(uint64_t hdc, Target& t) {
    t.dc = state().dc(hdc);
    if (!t.dc) return false;
    auto w = get_window(t.dc->window);
    auto top = state().top_level(w);
    if (!w || !top) return false;
    client_origin(w, top, t.dx, t.dy);
    t.surface = top->surface.get();
    return true;
}

ntmac_ntstatus fill_rect(ntmachost_fill_args* a) {
    Target t;
    if (!target_of(a->Dc, t)) return NTMAC_STATUS_INVALID_HANDLE;
    t.surface->fill(t.to_surface(Rect::from(a->Rect)).intersect(t.clip()), colorref_to_bgra(a->Color));
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus set_dc_state(ntmachost_dc_state_args* a) {
    auto d = state().dc(a->Dc);
    if (!d) return NTMAC_STATUS_INVALID_HANDLE;
    ntmachost_dc_state_args old = *a;
    old.TextColor = d->text_color;
    old.BkColor = d->bk_color;
    old.BkMode = d->bk_mode;
    old.TextAlign = d->text_align;
    if (a->Mask & NTMAC_DC_TEXT_COLOR) d->text_color = a->TextColor & 0xFFFFFF;
    if (a->Mask & NTMAC_DC_BK_COLOR) d->bk_color = a->BkColor & 0xFFFFFF;
    if (a->Mask & NTMAC_DC_BK_MODE) d->bk_mode = a->BkMode;
    if (a->Mask & NTMAC_DC_TEXT_ALIGN) d->text_align = a->TextAlign;
    *a = old;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus select_font(ntmachost_font_args* a) {
    auto d = state().dc(a->Dc);
    if (!d) return NTMAC_STATUS_INVALID_HANDLE;
    a->OldFont = d->font_handle;
    d->font_handle = a->Font;
    if (a->Font == 0) {
        d->font = FontSpec::system_font();
    } else {
        const auto& lf = a->LogFont;
        FontSpec f;
        for (int i = 0; i < 32 && lf.FaceName[i]; i++) f.face.push_back(static_cast<char16_t>(lf.FaceName[i]));
        f.height = lf.Height;
        f.weight = lf.Weight ? lf.Weight : 400;
        f.italic = lf.Italic;
        f.underline = lf.Underline;
        f.strike_out = lf.StrikeOut;
        d->font = f;
    }
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus text_out(ntmachost_text_out_args* a) {
    Target t;
    if (!target_of(a->Dc, t)) return NTMAC_STATUS_INVALID_HANDLE;
    const auto& d = *t.dc;
    Rect clip = t.clip();
    Rect r = t.to_surface(Rect::from(a->Rect));
    if (a->Options & NTMAC_TEXT_OPAQUE) t.surface->fill(r.intersect(clip), colorref_to_bgra(d.bk_color));
    if (a->Options & NTMAC_TEXT_CLIPPED) clip = clip.intersect(r);

    std::u16string_view text(reinterpret_cast<const char16_t*>(a->Text), a->Count);
    gdi::draw_text(*t.surface, clip, a->X + t.dx, a->Y + t.dy, d.text_align, d.font, text, d.text_color,
                   d.bk_mode == 2 && !(a->Options & NTMAC_TEXT_OPAQUE), d.bk_color);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_text_extent(ntmachost_text_out_args* a) {
    auto d = state().dc(a->Dc);
    if (!d) return NTMAC_STATUS_INVALID_HANDLE;
    std::u16string_view text(reinterpret_cast<const char16_t*>(a->Text), a->Count);
    gdi::text_extent(d->font, text, a->Width, a->Height);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_text_metrics(ntmachost_text_metrics_args* a) {
    auto d = state().dc(a->Dc);
    if (!d) return NTMAC_STATUS_INVALID_HANDLE;
    auto m = gdi::metrics(d->font);
    a->Height = m.height;
    a->Ascent = m.ascent;
    a->Descent = m.descent;
    a->InternalLeading = m.internal_leading;
    a->ExternalLeading = m.external_leading;
    a->AveCharWidth = m.ave_char_width;
    a->MaxCharWidth = m.max_char_width;
    a->Weight = d->font.weight;
    a->Italic = d->font.italic;
    a->Underlined = d->font.underline;
    a->CharSet = 128; // SHIFTJIS_CHARSET (日本語のフォントとして振る舞う)
    a->PitchAndFamily = 0x06;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus draw_image(ntmachost_image_args* a) {
    Target t;
    if (!target_of(a->Dc, t)) return NTMAC_STATUS_INVALID_HANDLE;
    Rect dst = t.to_surface({a->X, a->Y, a->X + a->Width, a->Y + a->Height});
    gdi::draw_image(*t.surface, t.clip(), dst, a->Pixels, a->ImageWidth, a->ImageHeight);
    return NTMAC_STATUS_SUCCESS;
}

// ---- メニュー ----

ntmac_ntstatus create_menu(ntmachost_menu_args* a) {
    a->Menu = state().create_menu(a->Flags & NTMAC_MENU_POPUP)->handle;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus append_menu(ntmachost_menu_args* a) {
    auto m = state().menu(a->Menu);
    if (!m) return NTMAC_STATUS_INVALID_HANDLE;
    MenuItem item;
    item.flags = a->Flags;
    item.id = a->Item;
    for (const uint16_t* p = a->Text; p && *p; p++) item.text.push_back(static_cast<char16_t>(*p));
    m->items.push_back(std::move(item));
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus destroy_menu(ntmachost_menu_args* a) {
    if (!state().menu(a->Menu)) return NTMAC_STATUS_INVALID_HANDLE;
    state().destroy_menu(a->Menu);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus set_menu(ntmachost_menu_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    w->menu_or_id = a->Menu;
    cocoa::update_menu_bar(w->handle);
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_menu(ntmachost_menu_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    a->Menu = state().menu(w->menu_or_id) ? w->menu_or_id : 0;
    return NTMAC_STATUS_SUCCESS;
}

// ---- その他 ----

ntmac_ntstatus get_key_state(ntmachost_window_args* a) {
    uint8_t s = state().key_state[a->Index & 0xFF];
    a->Result = static_cast<int16_t>(((s & 0x80) ? 0xFF80 : 0) | (s & 1));
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_related_window(ntmachost_window_args* a) {
    auto w = get_window(a->Window);
    if (!w) return NTMAC_STATUS_INVALID_HANDLE;
    a->Value = static_cast<int64_t>(state().related(w, a->Index));
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus set_focus(ntmachost_window_args* a) {
    if (a->Window && !get_window(a->Window)) return NTMAC_STATUS_INVALID_HANDLE;
    a->Value = static_cast<int64_t>(state().focus);
    state().focus = a->Window;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus set_capture(ntmachost_window_args* a) {
    if (a->Window && !get_window(a->Window)) return NTMAC_STATUS_INVALID_HANDLE;
    a->Value = static_cast<int64_t>(state().capture);
    state().capture = a->Window;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_screen_info(ntmachost_screen_args* a) {
    a->Screen = state().screen.screen.to_ntmac();
    a->WorkArea = state().screen.work_area.to_ntmac();
    a->Dpi = 96;
    return NTMAC_STATUS_SUCCESS;
}

template <class T>
T* as(void* p) {
    return static_cast<T*>(p);
}

}

ntmac_ntstatus dispatch(uint32_t code, void* args) {
    if (code == NTMACHOST_USER_GET_MESSAGE) return get_message(as<ntmachost_msg_args>(args)); // 待つ間はロックを外す
    if (code == NTMACHOST_USER_CREATE_WINDOW) cocoa::prepare(); // 画面の大きさが要る．メインスレッドを待つのでロックの外で

    Lock lock(g_lock);
    switch (code) {
    case NTMACHOST_USER_REGISTER_CLASS:   return register_class(as<ntmachost_class_args>(args));
    case NTMACHOST_USER_GET_CLASS_INFO:   return get_class_info(as<ntmachost_class_args>(args));
    case NTMACHOST_USER_CREATE_WINDOW:    return create_window(as<ntmachost_create_window_args>(args));
    case NTMACHOST_USER_DESTROY_WINDOW:   return destroy_window(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_IS_WINDOW:
        as<ntmachost_window_args>(args)->Result = get_window(as<ntmachost_window_args>(args)->Window) != nullptr;
        return NTMAC_STATUS_SUCCESS;
    case NTMACHOST_USER_GET_WINDOW_LONG:  return get_window_long(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_SET_WINDOW_LONG:  return set_window_long(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_GET_CLASS_LONG:   return get_class_long(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_GET_WINDOW_TEXT:  return get_window_text(as<ntmachost_text_args>(args));
    case NTMACHOST_USER_SET_WINDOW_TEXT:  return set_window_text(as<ntmachost_text_args>(args));
    case NTMACHOST_USER_SHOW_WINDOW:      return show_window(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_GET_WINDOW_RECT:  return get_window_rect(as<ntmachost_rect_args>(args), false);
    case NTMACHOST_USER_GET_CLIENT_RECT:  return get_window_rect(as<ntmachost_rect_args>(args), true);
    case NTMACHOST_USER_POST_MESSAGE:     return post_message(as<ntmachost_msg_args>(args));
    case NTMACHOST_USER_POST_QUIT_MESSAGE: return post_quit_message(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_INVALIDATE_RECT:  return invalidate_rect(as<ntmachost_rect_args>(args), false);
    case NTMACHOST_USER_VALIDATE_RECT:    return invalidate_rect(as<ntmachost_rect_args>(args), true);
    case NTMACHOST_USER_GET_UPDATE_RECT:  return get_update_rect(as<ntmachost_rect_args>(args));
    case NTMACHOST_USER_BEGIN_PAINT:      return begin_paint(as<ntmachost_paint_args>(args));
    case NTMACHOST_USER_END_PAINT:        return end_paint(as<ntmachost_paint_args>(args));
    case NTMACHOST_USER_GET_DC:           return get_dc(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_RELEASE_DC:       return release_dc(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_CREATE_MENU:      return create_menu(as<ntmachost_menu_args>(args));
    case NTMACHOST_USER_APPEND_MENU:      return append_menu(as<ntmachost_menu_args>(args));
    case NTMACHOST_USER_DESTROY_MENU:     return destroy_menu(as<ntmachost_menu_args>(args));
    case NTMACHOST_USER_SET_MENU:         return set_menu(as<ntmachost_menu_args>(args));
    case NTMACHOST_USER_GET_MENU:         return get_menu(as<ntmachost_menu_args>(args));
    case NTMACHOST_USER_GET_KEY_STATE:    return get_key_state(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_GET_SCREEN_INFO:  return get_screen_info(as<ntmachost_screen_args>(args));
    case NTMACHOST_USER_GET_RELATED_WINDOW: return get_related_window(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_SET_FOCUS:        return set_focus(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_GET_FOCUS:
        as<ntmachost_window_args>(args)->Value = static_cast<int64_t>(state().focus);
        return NTMAC_STATUS_SUCCESS;
    case NTMACHOST_USER_SET_CAPTURE:      return set_capture(as<ntmachost_window_args>(args));
    case NTMACHOST_USER_SET_TIMER:        return set_timer(as<ntmachost_timer_args>(args));
    case NTMACHOST_USER_KILL_TIMER:       return kill_timer(as<ntmachost_timer_args>(args));
    case NTMACHOST_USER_GET_CAPTURE:
        as<ntmachost_window_args>(args)->Value = static_cast<int64_t>(state().capture);
        return NTMAC_STATUS_SUCCESS;
    case NTMACHOST_GDI_FILL_RECT:         return fill_rect(as<ntmachost_fill_args>(args));
    case NTMACHOST_GDI_SET_DC_STATE:      return set_dc_state(as<ntmachost_dc_state_args>(args));
    case NTMACHOST_GDI_SELECT_FONT:       return select_font(as<ntmachost_font_args>(args));
    case NTMACHOST_GDI_TEXT_OUT:          return text_out(as<ntmachost_text_out_args>(args));
    case NTMACHOST_GDI_GET_TEXT_EXTENT:   return get_text_extent(as<ntmachost_text_out_args>(args));
    case NTMACHOST_GDI_GET_TEXT_METRICS:  return get_text_metrics(as<ntmachost_text_metrics_args>(args));
    case NTMACHOST_GDI_DRAW_IMAGE:        return draw_image(as<ntmachost_image_args>(args));
    default:                              return NTMAC_STATUS_INVALID_SYSTEM_SERVICE;
    }
}

}
