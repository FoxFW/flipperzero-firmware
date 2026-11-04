#include "../subghz_i.h"
#include "../helpers/subghz_txrx_i.h"
#include <lib/subghz/protocols/fiat_v1.h>

#define FIAT_V1_RECOVER_EV_DONE (0xF1)
#define FIAT_V1_RECOVER_EV_TICK (0xF3)

/* Level model: try the 95 known keys in sets of 8 (last set = 7). Each set is the
 * known-good ~8 min on-device operation; after a set finishes without a key we
 * auto-advance to the next set on a short countdown, so leaving the Flipper alone
 * eventually walks every set (stopping the moment a key is found). This replaces the
 * old single 87-key "L2" sweep that sat at 0% so long it looked like a hang. */
#define FIAT_V1_KEYS_PER_LEVEL 8U
#define FIAT_V1_LEVEL_COUNT \
    ((FIAT_V1_KNOWN_KEY_COUNT + FIAT_V1_KEYS_PER_LEVEL - 1U) / FIAT_V1_KEYS_PER_LEVEL)
#define FIAT_V1_COUNTDOWN_SECS 3U

typedef enum {
    FiatPhaseSweep = 0, // a set's worker thread is running
    FiatPhaseCountdown, // between sets, counting down to auto-start the next
    FiatPhaseResult,    // result widget shown
} FiatRecoverPhase;

typedef struct {
    SubGhz* subghz;
    FuriThread* thread;
    volatile bool cancel;
    uint32_t start_tick;

    uint32_t uid;
    uint8_t button;
    uint16_t control;
    uint32_t hop;

    uint8_t phase;
    uint8_t level; // 0-based index of the set running / about to run
    uint8_t key_start;
    uint8_t key_end;
    uint32_t total;

    FuriTimer* countdown_timer;
    uint8_t countdown;

    bool success;
    uint8_t found_key[6];
    uint32_t found_epoch;
} FiatV1RecoverCtx;

static uint8_t fiat_level_start(uint8_t level) {
    return (uint8_t)(level * FIAT_V1_KEYS_PER_LEVEL);
}

static uint8_t fiat_level_end(uint8_t level) {
    uint16_t e = (uint16_t)((uint16_t)(level + 1U) * FIAT_V1_KEYS_PER_LEVEL);
    return (e > FIAT_V1_KNOWN_KEY_COUNT) ? (uint8_t)FIAT_V1_KNOWN_KEY_COUNT : (uint8_t)e;
}

static bool fiat_v1_recover_progress_cb(uint8_t progress, uint32_t combos_tested, void* context) {
    FiatV1RecoverCtx* ctx = context;
    if(ctx->cancel) return false;
    uint32_t elapsed_ms = furi_get_tick() - ctx->start_tick;
    uint32_t elapsed_sec = elapsed_ms / 1000;
    uint32_t combos_per_sec =
        (elapsed_ms > 0) ? (uint32_t)((uint64_t)combos_tested * 1000 / elapsed_ms) : 0;
    uint32_t total = ctx->total ? ctx->total : 1U;
    uint32_t remaining = (combos_tested < total) ? (total - combos_tested) : 0;
    uint32_t eta_sec = (combos_per_sec > 0) ? (remaining / combos_per_sec) : 0;
    subghz_view_fiat_v1_recover_update_stats(
        ctx->subghz->subghz_fiat_v1_recover,
        progress,
        combos_tested,
        combos_per_sec,
        elapsed_sec,
        eta_sec);
    return true;
}

static int32_t fiat_v1_recover_thread(void* context) {
    FiatV1RecoverCtx* ctx = context;
    ctx->success = fiat_v1_hitag2_recover_range(
        ctx->uid,
        ctx->button,
        ctx->control,
        ctx->hop,
        ctx->key_start,
        ctx->key_end,
        ctx->found_key,
        &ctx->found_epoch,
        fiat_v1_recover_progress_cb,
        ctx);
    view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, FIAT_V1_RECOVER_EV_DONE);
    return 0;
}

static void fiat_v1_recover_start_level(FiatV1RecoverCtx* ctx) {
    ctx->key_start = fiat_level_start(ctx->level);
    ctx->key_end = fiat_level_end(ctx->level);
    ctx->total = fiat_v1_hitag2_recovery_total_range(ctx->key_start, ctx->key_end);
    ctx->cancel = false;
    ctx->start_tick = furi_get_tick();
    ctx->phase = FiatPhaseSweep;
    subghz_view_fiat_v1_recover_reset(ctx->subghz->subghz_fiat_v1_recover);
    view_dispatcher_switch_to_view(ctx->subghz->view_dispatcher, SubGhzViewIdFiatV1Recover);
    ctx->thread = furi_thread_alloc_ex("FiatV1Recover", 2048, fiat_v1_recover_thread, ctx);
    furi_thread_start(ctx->thread);
}

static void fiat_v1_recover_stop_local(FiatV1RecoverCtx* ctx) {
    if(ctx->thread) {
        ctx->cancel = true;
        furi_thread_join(ctx->thread);
        furi_thread_free(ctx->thread);
        ctx->thread = NULL;
    }
}

static void fiat_v1_recover_stop_countdown(FiatV1RecoverCtx* ctx) {
    if(ctx->countdown_timer) {
        furi_timer_stop(ctx->countdown_timer);
        furi_timer_free(ctx->countdown_timer);
        ctx->countdown_timer = NULL;
    }
}

static void fiat_v1_recover_countdown_timer_cb(void* context) {
    FiatV1RecoverCtx* ctx = context;
    view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, FIAT_V1_RECOVER_EV_TICK);
}

static void fiat_v1_recover_view_callback(SubGhzCustomEvent event, void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static void fiat_v1_recover_widget_callback(GuiButtonType result, InputType type, void* context) {
    SubGhz* subghz = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, result);
    }
}

static void fiat_v1_recover_draw_countdown(SubGhz* subghz, FiatV1RecoverCtx* ctx) {
    char body[96];
    snprintf(
        body,
        sizeof(body),
        "\e#No Key Yet\e#\n%u/%u key sets done.\nAuto-trying the next.",
        (unsigned)ctx->level,
        (unsigned)FIAT_V1_LEVEL_COUNT);
    char btn[16];
    snprintf(btn, sizeof(btn), "Next (%u)", (unsigned)ctx->countdown);
    widget_reset(subghz->widget);
    widget_add_text_box_element(subghz->widget, 0, 0, 128, 44, AlignCenter, AlignTop, body, true);
    widget_add_button_element(
        subghz->widget, GuiButtonTypeCenter, btn, fiat_v1_recover_widget_callback, subghz);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

static void fiat_v1_recover_start_countdown(SubGhz* subghz, FiatV1RecoverCtx* ctx) {
    fiat_v1_recover_stop_countdown(ctx);
    ctx->phase = FiatPhaseCountdown;
    ctx->countdown = FIAT_V1_COUNTDOWN_SECS;
    fiat_v1_recover_draw_countdown(subghz, ctx);
    ctx->countdown_timer =
        furi_timer_alloc(fiat_v1_recover_countdown_timer_cb, FuriTimerTypePeriodic, ctx);
    furi_timer_start(ctx->countdown_timer, furi_ms_to_ticks(1000));
}

static void fiat_v1_recover_draw_result(SubGhz* subghz, FiatV1RecoverCtx* ctx) {
    widget_reset(subghz->widget);

    if(ctx->success) {
        FuriString* str = furi_string_alloc();
        furi_string_printf(
            str,
            "\e#Key Recovered!\e#\n"
            "%02X %02X %02X %02X %02X %02X\n"
            "Epoch: 0x%05lX",
            ctx->found_key[0],
            ctx->found_key[1],
            ctx->found_key[2],
            ctx->found_key[3],
            ctx->found_key[4],
            ctx->found_key[5],
            ctx->found_epoch);
        widget_add_text_box_element(
            subghz->widget, 0, 0, 128, 42, AlignCenter, AlignTop, furi_string_get_cstr(str), true);
        furi_string_free(str);
        widget_add_button_element(
            subghz->widget, GuiButtonTypeLeft, "Apply", fiat_v1_recover_widget_callback, subghz);
        widget_add_button_element(
            subghz->widget, GuiButtonTypeRight, "CBF", fiat_v1_recover_widget_callback, subghz);
    } else {
        widget_add_text_box_element(
            subghz->widget,
            0,
            0,
            128,
            44,
            AlignCenter,
            AlignTop,
            "\e#No Key Found\e#\nNot in the key set.\nFull recovery needs\na PC.",
            true);
        widget_add_button_element(
            subghz->widget,
            GuiButtonTypeLeft,
            "Send Raw",
            fiat_v1_recover_widget_callback,
            subghz);
        widget_add_button_element(
            subghz->widget,
            GuiButtonTypeRight,
            "Manual",
            fiat_v1_recover_widget_callback,
            subghz);
    }
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

static void fiat_v1_recover_apply_and_save(SubGhz* subghz, FiatV1RecoverCtx* ctx) {
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    if(fff) {
        flipper_format_insert_or_update_hex(
            fff, FIAT_V1_HITAG2_KEY_FIELD, ctx->found_key, sizeof(ctx->found_key));
        flipper_format_insert_or_update_uint32(
            fff, FIAT_V1_HITAG2_EPOCH_FIELD, &ctx->found_epoch, 1U);
        subghz_save_protocol_to_file(subghz, fff, furi_string_get_cstr(subghz->file_path));
    }
}

void subghz_scene_fiat_v1_recover_on_enter(void* context) {
    SubGhz* subghz = context;

    FiatV1RecoverCtx* ctx = malloc(sizeof(FiatV1RecoverCtx));
    furi_check(ctx);
    memset(ctx, 0, sizeof(FiatV1RecoverCtx));
    ctx->subghz = subghz;

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    uint32_t value = 0;
    flipper_format_rewind(fff);
    flipper_format_read_uint32(fff, "Serial", &value, 1);
    ctx->uid = value;
    flipper_format_rewind(fff);
    flipper_format_read_uint32(fff, "Btn", &value, 1);
    ctx->button = (uint8_t)value;
    flipper_format_rewind(fff);
    flipper_format_read_uint32(fff, "Cnt", &value, 1);
    ctx->control = (uint16_t)value;
    flipper_format_rewind(fff);
    flipper_format_read_uint32(fff, "Hop", &value, 1);
    ctx->hop = value;

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneFiatV1Recover, (uint32_t)(uintptr_t)ctx);

    subghz_view_fiat_v1_recover_set_callback(
        subghz->subghz_fiat_v1_recover, fiat_v1_recover_view_callback, subghz);

    ctx->level = 0U;
    fiat_v1_recover_start_level(ctx);
}

static void fiat_v1_recover_exit(SubGhz* subghz, FiatV1RecoverCtx* ctx) {
    fiat_v1_recover_stop_local(ctx);
    fiat_v1_recover_stop_countdown(ctx);
    scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneFiatV1Recover, 0);
    free(ctx);
}

bool subghz_scene_fiat_v1_recover_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    FiatV1RecoverCtx* ctx = (FiatV1RecoverCtx*)(uintptr_t)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneFiatV1Recover);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FIAT_V1_RECOVER_EV_DONE) {
            if(ctx->thread) {
                furi_thread_join(ctx->thread);
                furi_thread_free(ctx->thread);
                ctx->thread = NULL;
            }

            if(ctx->success) {
                fiat_v1_recover_apply_and_save(subghz, ctx);
                ctx->phase = FiatPhaseResult;
                fiat_v1_recover_draw_result(subghz, ctx);
                return true;
            }

            if((uint8_t)(ctx->level + 1U) < (uint8_t)FIAT_V1_LEVEL_COUNT) {
                ctx->level = (uint8_t)(ctx->level + 1U);
                fiat_v1_recover_start_countdown(subghz, ctx);
            } else {
                ctx->phase = FiatPhaseResult;
                fiat_v1_recover_draw_result(subghz, ctx);
            }
            return true;

        } else if(event.event == FIAT_V1_RECOVER_EV_TICK) {
            if(ctx->phase != FiatPhaseCountdown) return true;
            if(ctx->countdown > 1U) {
                ctx->countdown--;
                fiat_v1_recover_draw_countdown(subghz, ctx);
            } else {
                fiat_v1_recover_stop_countdown(ctx);
                fiat_v1_recover_start_level(ctx);
            }
            return true;

        } else if(event.event == GuiButtonTypeCenter && ctx->phase == FiatPhaseCountdown) {
            fiat_v1_recover_stop_countdown(ctx);
            fiat_v1_recover_start_level(ctx);
            return true;

        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            fiat_v1_recover_exit(subghz, ctx);
            scene_manager_previous_scene(subghz->scene_manager);
            return true;

        } else if(event.event == GuiButtonTypeLeft && ctx->phase == FiatPhaseResult) {
            fiat_v1_recover_exit(subghz, ctx);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
            return true;

        } else if(event.event == GuiButtonTypeRight && ctx->phase == FiatPhaseResult) {
            bool was_success = ctx->success;
            fiat_v1_recover_exit(subghz, ctx);
            scene_manager_next_scene(
                subghz->scene_manager,
                was_success ? SubGhzSceneCounterBf : SubGhzSceneFiatV1Key);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(ctx->phase == FiatPhaseSweep) {
            return true; // block Back while a set is actively sweeping
        }
        fiat_v1_recover_exit(subghz, ctx);
        scene_manager_previous_scene(subghz->scene_manager);
        return true;
    }
    return false;
}

void subghz_scene_fiat_v1_recover_on_exit(void* context) {
    SubGhz* subghz = context;
    FiatV1RecoverCtx* ctx = (FiatV1RecoverCtx*)(uintptr_t)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneFiatV1Recover);

    if(ctx) {
        fiat_v1_recover_stop_local(ctx);
        fiat_v1_recover_stop_countdown(ctx);
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneFiatV1Recover, 0);
    }
    widget_reset(subghz->widget);
}
