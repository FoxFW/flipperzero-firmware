#include "fmh_box_list.h"

#include <gui/elements.h>
#include <furi.h>
#include <string.h>

#define FMH_BOX_X 4
#define FMH_BOX_W 120
#define FMH_TEXT_PAD 4
#define FMH_HEADER_MAX 40
#define FMH_SCROLL_PERIOD_MS 333

typedef struct {
    const FmhBoxListOption* options;
    uint8_t count;
    uint8_t cursor;
    size_t scroll_counter;
    char header[FMH_HEADER_MAX];
} FmhBoxListModel;

struct FmhBoxList {
    View* view;
    FuriTimer* scroll_timer;
    bool scroll_running;
    FmhBoxListCallback callback;
    void* context;
};

static uint8_t fmh_box_list_scroll_top(uint8_t cursor, uint8_t count) {
    if(count <= 2) return 0;
    uint8_t max_top = (uint8_t)(count - 2);
    return (cursor > max_top) ? max_top : cursor;
}

static void fmh_box_list_draw_cb(Canvas* canvas, void* _model) {
    FmhBoxListModel* m = _model;
    canvas_clear(canvas);

    bool has_header = (m->header[0] != '\0');
    uint8_t box_h = has_header ? 22 : 28;
    uint8_t slot_y[2];
    if(has_header) {
        slot_y[0] = 16;
        slot_y[1] = 40;
    } else {
        slot_y[0] = 2;
        slot_y[1] = 34;
    }

    if(has_header) {
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, m->header);
    }

    if(m->count == 0 || m->options == NULL) return;

    uint8_t top = fmh_box_list_scroll_top(m->cursor, m->count);

    for(uint8_t slot = 0; slot < 2; slot++) {
        uint8_t idx = (uint8_t)(top + slot);
        if(idx >= m->count) break;
        bool at_cursor = (idx == m->cursor);
        uint8_t y = slot_y[slot];
        const FmhBoxListOption* opt = &m->options[idx];

        canvas_set_color(canvas, ColorBlack);
        if(at_cursor) {
            canvas_draw_rbox(canvas, FMH_BOX_X, y, FMH_BOX_W, box_h, 4);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, FMH_BOX_X, y, FMH_BOX_W, box_h, 4);
        }

        uint8_t text_x = FMH_BOX_X + FMH_BOX_W / 2;
        uint8_t text_w = FMH_BOX_W - (FMH_TEXT_PAD * 2);
        size_t scroll = at_cursor ? m->scroll_counter : 0;
        uint8_t title_y = (uint8_t)(y + (has_header ? 10 : 11));
        uint8_t sub_y = (uint8_t)(y + (has_header ? 19 : 23));

        canvas_set_font(canvas, FontPrimary);
        elements_scrollable_text_line_str(
            canvas, text_x, title_y, text_w, opt->title, scroll, false, true);
        if(opt->subtitle && opt->subtitle[0]) {
            canvas_set_font(canvas, FontSecondary);
            elements_scrollable_text_line_str(
                canvas, text_x, sub_y, text_w, opt->subtitle, scroll, false, true);
        }

        canvas_set_color(canvas, ColorBlack);
    }

    if(m->count > 2) {
        elements_scrollbar(canvas, m->cursor, m->count);
    }
}

static bool fmh_box_list_input_cb(InputEvent* event, void* context) {
    FmhBoxList* instance = context;
    if(event->type != InputTypeShort) return false;

    bool consumed = false;
    bool fire = false;
    uint32_t ev_val = 0;

    with_view_model(
        instance->view,
        FmhBoxListModel * m,
        {
            if(m->count == 0) {
            } else if(event->key == InputKeyUp) {
                m->cursor = (m->cursor == 0) ? (uint8_t)(m->count - 1) : (uint8_t)(m->cursor - 1);
                m->scroll_counter = 0;
                consumed = true;
            } else if(event->key == InputKeyDown) {
                m->cursor = (uint8_t)((m->cursor + 1) % m->count);
                m->scroll_counter = 0;
                consumed = true;
            } else if(event->key == InputKeyOk) {
                fire = true;
                ev_val = m->cursor;
                consumed = true;
            }
        },
        true);

    if(fire && instance->callback) instance->callback(instance->context, ev_val);
    return consumed;
}

static void fmh_box_list_scroll_timer_cb(void* context) {
    FmhBoxList* instance = context;
    with_view_model(instance->view, FmhBoxListModel * m, { m->scroll_counter++; }, true);
}

FmhBoxList* fmh_box_list_alloc(void) {
    FmhBoxList* instance = malloc(sizeof(FmhBoxList));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FmhBoxListModel));
    view_set_draw_callback(instance->view, fmh_box_list_draw_cb);
    view_set_input_callback(instance->view, fmh_box_list_input_cb);

    with_view_model(
        instance->view,
        FmhBoxListModel * m,
        {
            m->options = NULL;
            m->count = 0;
            m->cursor = 0;
            m->scroll_counter = 0;
            m->header[0] = '\0';
        },
        false);

    instance->scroll_timer =
        furi_timer_alloc(fmh_box_list_scroll_timer_cb, FuriTimerTypePeriodic, instance);
    instance->scroll_running = false;

    return instance;
}

void fmh_box_list_free(FmhBoxList* instance) {
    furi_assert(instance);
    furi_timer_stop(instance->scroll_timer);
    furi_timer_free(instance->scroll_timer);
    view_free(instance->view);
    free(instance);
}

View* fmh_box_list_get_view(FmhBoxList* instance) {
    furi_assert(instance);
    return instance->view;
}

void fmh_box_list_set_callback(FmhBoxList* instance, FmhBoxListCallback callback, void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fmh_box_list_set_header(FmhBoxList* instance, const char* header) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FmhBoxListModel * m,
        {
            if(header) {
                strncpy(m->header, header, FMH_HEADER_MAX - 1);
                m->header[FMH_HEADER_MAX - 1] = '\0';
            } else {
                m->header[0] = '\0';
            }
        },
        true);
}

void fmh_box_list_set_options(
    FmhBoxList* instance,
    const FmhBoxListOption* options,
    uint8_t count) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FmhBoxListModel * m,
        {
            m->options = options;
            m->count = count;
            m->cursor = 0;
            m->scroll_counter = 0;
        },
        true);
}

void fmh_box_list_set_selected(FmhBoxList* instance, uint8_t index) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FmhBoxListModel * m,
        {
            if(m->count != 0 && index < m->count) {
                m->cursor = index;
            }
            m->scroll_counter = 0;
        },
        true);
}

void fmh_box_list_resume_scroll(FmhBoxList* instance) {
    furi_assert(instance);
    if(instance->scroll_running) return;
    instance->scroll_running = true;
    furi_timer_start(instance->scroll_timer, FMH_SCROLL_PERIOD_MS);
}

void fmh_box_list_pause_scroll(FmhBoxList* instance) {
    furi_assert(instance);
    if(!instance->scroll_running) return;
    instance->scroll_running = false;
    furi_timer_stop(instance->scroll_timer);
}
