#include "subghz_view_receiver_info_bar.h"

#include <gui/elements.h>
#include <gui/icon.h>
#include <furi.h>
#include <input/input.h>
#include <string.h>
#include <assets_icons.h>

/* play / right-triangle glyph: shared Common icon I_ButtonRightSmall_3x5 (via assets_icons.h) */

#define RIB_BTN_H      14
#define RIB_BTN_PAD_X  4
#define RIB_BTN_MARGIN 2
#define RIB_BTN_R      3
#define RIB_BTN_GAP    3
#define RIB_ICON_W     3
#define RIB_ICON_H     5
#define RIB_ICON_GAP   2

/* Horizontal marquee cadence for overflowing info lines (e.g. a long "CRC:..."
 * line). Mirrors the Princeton-style scroll used by subghz_view_protocol_groups.c
 * (elements_scrollable_text_line_str driven by a periodic counter). */
#define RIB_SCROLL_TIMER_PERIOD_MS 333

typedef struct {
    uint8_t cursor;
    uint8_t count;
    uint16_t last_idx;
    uint8_t scroll;
    uint8_t line_count;
    size_t scroll_x;
    char label[SUBGHZ_RIB_MAX_BTN][16];
    bool icon[SUBGHZ_RIB_MAX_BTN];
    uint32_t ev_press[SUBGHZ_RIB_MAX_BTN];
    uint32_t ev_release[SUBGHZ_RIB_MAX_BTN];
    char info[192];
    char freq[24];
    char mod[24];
} SubGhzReceiverInfoBarModel;

struct SubGhzReceiverInfoBar {
    View* view;
    FuriTimer* scroll_timer;
    bool scroll_running;
    SubGhzReceiverInfoBarCallback callback;
    void* context;
};

static void subghz_receiver_info_bar_draw_cb(Canvas* canvas, void* _model) {
    SubGhzReceiverInfoBarModel* m = _model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    uint8_t line_h = (uint8_t)canvas_current_font_height(canvas);
    int32_t btn_top = 64 - RIB_BTN_MARGIN - RIB_BTN_H;
    int32_t info_bottom = btn_top - 1;
    uint8_t visible = (line_h > 0) ? (uint8_t)(info_bottom / line_h) : 1;
    if(visible == 0) visible = 1;

    /* Baseline offset for canvas_draw_str (helper uses AlignBottom); matches the
     * multiline text convention of top + font height (see gui/elements.c). */
    const CanvasFontParameters* rib_fp = canvas_get_font_params(canvas, FontSecondary);
    uint8_t ascent = rib_fp ? rib_fp->height : (uint8_t)((line_h > 2) ? (line_h - 2) : line_h);

    uint8_t nlines = m->line_count;
    uint8_t scroll = m->scroll;
    if(nlines > visible) {
        if(scroll > (uint8_t)(nlines - visible)) scroll = (uint8_t)(nlines - visible);
    } else {
        scroll = 0;
    }

    char buf[sizeof(m->info)];
    strncpy(buf, m->info, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* line = buf;
    uint8_t idx = 0;
    while(*line) {
        char* nl = strchr(line, '\n');
        if(nl) *nl = '\0';
        if(idx >= scroll && (uint8_t)(idx - scroll) < visible) {
            int32_t y = (int32_t)(idx - scroll) * line_h;
            uint8_t avail = (idx == 0 && m->freq[0]) ?
                                76 :
                                (uint8_t)((nlines > visible) ? 124 : 128);
            /* A line wider than its column scrolls horizontally (marquee) instead
             * of overrunning the heading / freq column; short lines draw verbatim. */
            elements_scrollable_text_line_str(
                canvas, 0, (uint8_t)(y + ascent), avail, line, m->scroll_x, false, false);
            if(idx == 0) {
                if(m->freq[0])
                    canvas_draw_str_aligned(canvas, 78, y, AlignLeft, AlignTop, m->freq);
                if(m->mod[0])
                    canvas_draw_str_aligned(canvas, 113, y, AlignLeft, AlignTop, m->mod);
            }
        }
        idx++;
        if(!nl) break;
        line = nl + 1;
    }

    if(nlines > visible) {
        elements_scrollbar_pos(
            canvas,
            127,
            line_h,
            (size_t)(info_bottom - line_h),
            (size_t)scroll,
            (size_t)(nlines - visible + 1));
    }

    if(m->count == 0) return;

    /* measure each pill, lay them out centered along the bottom */
    uint16_t bw[SUBGHZ_RIB_MAX_BTN];
    uint16_t total = 0;
    for(uint8_t i = 0; i < m->count; i++) {
        uint16_t tw = canvas_string_width(canvas, m->label[i]);
        uint16_t w = tw + RIB_BTN_PAD_X * 2 + (m->icon[i] ? (RIB_ICON_W + RIB_ICON_GAP) : 0);
        bw[i] = w;
        total += w;
    }
    total += RIB_BTN_GAP * (m->count - 1);

    int32_t x = (128 - (int32_t)total) / 2;
    if(x < RIB_BTN_MARGIN) x = RIB_BTN_MARGIN;
    int32_t y = 64 - RIB_BTN_MARGIN - RIB_BTN_H;

    for(uint8_t i = 0; i < m->count; i++) {
        bool sel = (i == m->cursor);
        canvas_set_color(canvas, ColorBlack);
        if(sel) {
            canvas_draw_rbox(canvas, x, y, bw[i], RIB_BTN_H, RIB_BTN_R);
        } else {
            canvas_draw_rframe(canvas, x, y, bw[i], RIB_BTN_H, RIB_BTN_R);
        }

        uint16_t tw = canvas_string_width(canvas, m->label[i]);
        uint16_t content_w = tw + (m->icon[i] ? (RIB_ICON_W + RIB_ICON_GAP) : 0);
        int32_t cx = x + ((int32_t)bw[i] - (int32_t)content_w) / 2;
        int32_t cy = y + RIB_BTN_H / 2;

        canvas_set_color(canvas, sel ? ColorWhite : ColorBlack);
        if(m->icon[i]) {
            canvas_draw_icon(canvas, cx, y + (RIB_BTN_H - RIB_ICON_H) / 2, &I_ButtonRightSmall_3x5);
            cx += RIB_ICON_W + RIB_ICON_GAP;
        }
        canvas_draw_str_aligned(canvas, cx, cy, AlignLeft, AlignCenter, m->label[i]);
        canvas_set_color(canvas, ColorBlack);

        x += (int32_t)bw[i] + RIB_BTN_GAP;
    }
}

static bool subghz_receiver_info_bar_input_cb(InputEvent* event, void* context) {
    SubGhzReceiverInfoBar* instance = context;
    bool consumed = false;

    if(event->key == InputKeyLeft && event->type == InputTypeShort) {
        with_view_model(
            instance->view,
            SubGhzReceiverInfoBarModel * m,
            {
                if(m->cursor > 0) m->cursor--;
            },
            true);
        consumed = true;
    } else if(event->key == InputKeyRight && event->type == InputTypeShort) {
        with_view_model(
            instance->view,
            SubGhzReceiverInfoBarModel * m,
            {
                if(m->count && (m->cursor + 1 < m->count)) m->cursor++;
            },
            true);
        consumed = true;
    } else if(
        event->key == InputKeyUp &&
        (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
        with_view_model(
            instance->view,
            SubGhzReceiverInfoBarModel * m,
            {
                if(m->scroll > 0) m->scroll--;
            },
            true);
        consumed = true;
    } else if(
        event->key == InputKeyDown &&
        (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
        with_view_model(
            instance->view,
            SubGhzReceiverInfoBarModel * m,
            {
                if(m->line_count && (uint8_t)(m->scroll + 1) < m->line_count) m->scroll++;
            },
            true);
        consumed = true;
    } else if(event->key == InputKeyOk) {
        uint32_t ep = 0;
        uint32_t er = 0;
        with_view_model(
            instance->view,
            SubGhzReceiverInfoBarModel * m,
            {
                if(m->count) {
                    ep = m->ev_press[m->cursor];
                    er = m->ev_release[m->cursor];
                }
            },
            false);
        if(er != 0) {
            /* press/hold button (Send): start on press, stop on release */
            if(event->type == InputTypePress) {
                if(instance->callback) instance->callback(instance->context, ep);
                consumed = true;
            } else if(event->type == InputTypeRelease) {
                if(instance->callback) instance->callback(instance->context, er);
                consumed = true;
            }
        } else {
            if(event->type == InputTypeShort && ep != 0) {
                if(instance->callback) instance->callback(instance->context, ep);
                consumed = true;
            }
        }
    }

    return consumed;
}

static void subghz_receiver_info_bar_scroll_timer_cb(void* context) {
    SubGhzReceiverInfoBar* instance = context;
    with_view_model(
        instance->view, SubGhzReceiverInfoBarModel * m, { m->scroll_x++; }, true);
}

SubGhzReceiverInfoBar* subghz_receiver_info_bar_alloc(void) {
    SubGhzReceiverInfoBar* instance = malloc(sizeof(SubGhzReceiverInfoBar));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(SubGhzReceiverInfoBarModel));
    view_set_draw_callback(instance->view, subghz_receiver_info_bar_draw_cb);
    view_set_input_callback(instance->view, subghz_receiver_info_bar_input_cb);

    with_view_model(
        instance->view,
        SubGhzReceiverInfoBarModel * m,
        {
            m->cursor = 0;
            m->count = 0;
            m->last_idx = 0xFFFF;
            m->scroll = 0;
            m->line_count = 0;
            m->scroll_x = 0;
            m->info[0] = '\0';
            m->freq[0] = '\0';
            m->mod[0] = '\0';
        },
        false);

    instance->scroll_timer = furi_timer_alloc(
        subghz_receiver_info_bar_scroll_timer_cb, FuriTimerTypePeriodic, instance);
    instance->scroll_running = false;

    return instance;
}

void subghz_receiver_info_bar_free(SubGhzReceiverInfoBar* instance) {
    furi_assert(instance);
    furi_timer_stop(instance->scroll_timer);
    furi_timer_free(instance->scroll_timer);
    view_free(instance->view);
    free(instance);
}

void subghz_receiver_info_bar_resume_scroll(SubGhzReceiverInfoBar* instance) {
    furi_assert(instance);
    if(instance->scroll_running) return;
    instance->scroll_running = true;
    furi_timer_start(instance->scroll_timer, RIB_SCROLL_TIMER_PERIOD_MS);
}

void subghz_receiver_info_bar_pause_scroll(SubGhzReceiverInfoBar* instance) {
    furi_assert(instance);
    if(!instance->scroll_running) return;
    instance->scroll_running = false;
    furi_timer_stop(instance->scroll_timer);
}

View* subghz_receiver_info_bar_get_view(SubGhzReceiverInfoBar* instance) {
    furi_assert(instance);
    return instance->view;
}

void subghz_receiver_info_bar_set_callback(
    SubGhzReceiverInfoBar* instance,
    SubGhzReceiverInfoBarCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void subghz_receiver_info_bar_reset(SubGhzReceiverInfoBar* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view, SubGhzReceiverInfoBarModel * m, { m->count = 0; }, false);
}

void subghz_receiver_info_bar_add_button(
    SubGhzReceiverInfoBar* instance,
    const char* label,
    bool icon,
    uint32_t event_press,
    uint32_t event_release) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReceiverInfoBarModel * m,
        {
            if(m->count < SUBGHZ_RIB_MAX_BTN) {
                strncpy(m->label[m->count], label, sizeof(m->label[0]) - 1);
                m->label[m->count][sizeof(m->label[0]) - 1] = '\0';
                m->icon[m->count] = icon;
                m->ev_press[m->count] = event_press;
                m->ev_release[m->count] = event_release;
                m->count++;
            }
        },
        false);
}

void subghz_receiver_info_bar_set_info(
    SubGhzReceiverInfoBar* instance,
    const char* proto_multiline,
    const char* freq,
    const char* mod) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReceiverInfoBarModel * m,
        {
            snprintf(m->info, sizeof(m->info), "%s", proto_multiline ? proto_multiline : "");
            snprintf(m->freq, sizeof(m->freq), "%s", freq ? freq : "");
            snprintf(m->mod, sizeof(m->mod), "%s", mod ? mod : "");
            size_t il = strlen(m->info);
            while(il > 0 &&
                  (m->info[il - 1] == '\n' || m->info[il - 1] == '\r' ||
                   m->info[il - 1] == ' ')) {
                m->info[--il] = '\0';
            }
            uint8_t lc = (m->info[0]) ? 1 : 0;
            for(const char* c = m->info; *c; c++)
                if(*c == '\n') lc++;
            m->line_count = lc;
        },
        false);
}

void subghz_receiver_info_bar_set_focus(
    SubGhzReceiverInfoBar* instance,
    uint16_t signal_idx,
    uint8_t primary_idx) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReceiverInfoBarModel * m,
        {
            if(primary_idx >= m->count) primary_idx = m->count ? (m->count - 1) : 0;
            if(signal_idx != m->last_idx) {
                m->cursor = primary_idx;
                m->scroll = 0;
                m->scroll_x = 0;
                m->last_idx = signal_idx;
            } else if(m->cursor >= m->count) {
                m->cursor = primary_idx;
            }
        },
        true);
}

void subghz_receiver_info_bar_scroll_reset(SubGhzReceiverInfoBar* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReceiverInfoBarModel * m,
        {
            m->scroll = 0;
            m->scroll_x = 0;
        },
        true);
}
