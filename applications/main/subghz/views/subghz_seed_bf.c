#include "subghz_seed_bf.h"

#include <gui/elements.h>
#include <furi.h>

struct SubGhzViewSeedBf {
    View* view;
    SubGhzViewSeedBfCallback callback;
    void* context;
};

typedef struct {
    uint32_t tested;
    uint32_t total;
    uint32_t elapsed_sec;
    bool done;
    bool success;
    FuriString* result_str;
} SubGhzSeedBfModel;

static void subghz_view_seed_bf_draw(Canvas* canvas, void* _model) {
    SubGhzSeedBfModel* model = (SubGhzSeedBfModel*)_model;

    canvas_clear(canvas);

    if(!model->done) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Seed BruteForce");

        /* Fox progress bar: 122px frame, 116px inner fill. */
        canvas_draw_rframe(canvas, 3, 17, 122, 12, 2);
        uint32_t total = model->total ? model->total : 1U;
        uint8_t pct = (uint8_t)(((uint64_t)model->tested * 100U) / total);
        uint8_t fill = (uint8_t)((uint16_t)pct * 116 / 100);
        if(fill > 0) {
            canvas_draw_rbox(canvas, 5, 19, fill, 8, 1);
        }

        canvas_set_font(canvas, FontSecondary);

        char line[40];
        snprintf(
            line,
            sizeof(line),
            "%u%% - %lu / %lu keys",
            (unsigned)pct,
            (unsigned long)model->tested,
            (unsigned long)total);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, line);

        char el[24];
        uint32_t m = model->elapsed_sec / 60U;
        uint32_t s = model->elapsed_sec % 60U;
        if(m > 0) {
            snprintf(el, sizeof(el), "Elapsed: %lum %lus", (unsigned long)m, (unsigned long)s);
        } else {
            snprintf(el, sizeof(el), "Elapsed: %lus", (unsigned long)s);
        }
        canvas_draw_str_aligned(canvas, 64, 49, AlignCenter, AlignTop, el);

        canvas_draw_str_aligned(canvas, 126, 64, AlignRight, AlignBottom, "Hold BACK");
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, 64, 2, AlignCenter, AlignTop, model->success ? "Seed Found" : "Seed BF");
        canvas_set_font(canvas, FontSecondary);
        if(model->result_str) {
            elements_multiline_text_aligned(
                canvas, 64, 24, AlignCenter, AlignTop, furi_string_get_cstr(model->result_str));
        }
        canvas_draw_str_aligned(canvas, 126, 64, AlignRight, AlignBottom, "Back");
    }
}

static bool subghz_view_seed_bf_input(InputEvent* event, void* context) {
    SubGhzViewSeedBf* instance = (SubGhzViewSeedBf*)context;

    if(event->key == InputKeyBack) {
        if(instance->callback) {
            instance->callback(SubGhzCustomEventViewTransmitterBack, instance->context);
        }
        return true;
    }
    return false;
}

SubGhzViewSeedBf* subghz_view_seed_bf_alloc(void) {
    SubGhzViewSeedBf* instance = malloc(sizeof(SubGhzViewSeedBf));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SubGhzSeedBfModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, subghz_view_seed_bf_draw);
    view_set_input_callback(instance->view, subghz_view_seed_bf_input);

    with_view_model(
        instance->view,
        SubGhzSeedBfModel * model,
        {
            model->result_str = furi_string_alloc();
            model->tested = 0;
            model->total = 0;
            model->elapsed_sec = 0;
            model->done = false;
            model->success = false;
        },
        false);

    return instance;
}

void subghz_view_seed_bf_free(SubGhzViewSeedBf* instance) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzSeedBfModel * model,
        { furi_string_free(model->result_str); },
        false);
    view_free(instance->view);
    free(instance);
}

View* subghz_view_seed_bf_get_view(SubGhzViewSeedBf* instance) {
    furi_check(instance);
    return instance->view;
}

void subghz_view_seed_bf_set_callback(
    SubGhzViewSeedBf* instance,
    SubGhzViewSeedBfCallback callback,
    void* context) {
    furi_check(instance);
    instance->callback = callback;
    instance->context = context;
}

void subghz_view_seed_bf_update(
    SubGhzViewSeedBf* instance,
    uint32_t tested,
    uint32_t total,
    uint32_t elapsed_sec) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzSeedBfModel * model,
        {
            model->tested = tested;
            model->total = total;
            model->elapsed_sec = elapsed_sec;
        },
        true);
}

void subghz_view_seed_bf_set_result(SubGhzViewSeedBf* instance, bool success, const char* result) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzSeedBfModel * model,
        {
            model->done = true;
            model->success = success;
            furi_string_set_str(model->result_str, result);
        },
        true);
}

void subghz_view_seed_bf_reset(SubGhzViewSeedBf* instance) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzSeedBfModel * model,
        {
            model->tested = 0;
            model->total = 0;
            model->elapsed_sec = 0;
            model->done = false;
            model->success = false;
            furi_string_reset(model->result_str);
        },
        false);
}
