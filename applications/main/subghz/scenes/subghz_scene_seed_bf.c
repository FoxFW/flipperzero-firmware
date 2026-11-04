#include "../subghz_i.h"

#include <lib/subghz/protocols/renault_v1.h>

#define TAG "SubGhzSceneSeedBf"

#define SEED_BF_EVENT_DONE     (0xE0)
#define SEED_BF_EVENT_PROGRESS (0xE2)

typedef struct {
    SubGhz* subghz;
    FuriThread* thread;
    FuriTimer* timer;
    void* decoder;
    RenaultV1BfContext bf;
    uint32_t start_tick;
    bool done_handled;
} SeedBfCtx;

static int32_t seed_bf_thread(void* context) {
    SeedBfCtx* ctx = context;
    subghz_protocol_renault_v1_bf_run(&ctx->bf);
    view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, SEED_BF_EVENT_DONE);
    return 0;
}

static void seed_bf_timer_cb(void* context) {
    SeedBfCtx* ctx = context;
    view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, SEED_BF_EVENT_PROGRESS);
}

static void seed_bf_view_callback(SubGhzCustomEvent event, void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static void seed_bf_save_found(SeedBfCtx* ctx) {
    FlipperFormat* fff = subghz_txrx_get_fff_data(ctx->subghz->txrx);
    if(!fff) return;

    uint32_t seed = renault_v1_iv_to_u32(ctx->bf.iv);
    uint8_t seed_be[4] = {
        (uint8_t)(seed >> 24U),
        (uint8_t)(seed >> 16U),
        (uint8_t)(seed >> 8U),
        (uint8_t)seed,
    };
    flipper_format_rewind(fff);
    flipper_format_insert_or_update_hex(fff, "Seed", seed_be, 4U);

    uint8_t recovered = 1U;
    flipper_format_rewind(fff);
    flipper_format_insert_or_update_hex(fff, "Recovered", &recovered, 1U);

    subghz_save_protocol_to_file(
        ctx->subghz, fff, furi_string_get_cstr(ctx->subghz->file_path));
}

static void seed_bf_stop_worker(SeedBfCtx* ctx) {
    if(ctx->timer) {
        furi_timer_stop(ctx->timer);
        furi_timer_free(ctx->timer);
        ctx->timer = NULL;
    }
    if(ctx->thread) {
        ctx->bf.cancel_requested = true;
        furi_thread_join(ctx->thread);
        furi_thread_free(ctx->thread);
        ctx->thread = NULL;
    }
}

void subghz_scene_seed_bf_on_enter(void* context) {
    SubGhz* subghz = context;

    SeedBfCtx* ctx = malloc(sizeof(SeedBfCtx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->subghz = subghz;

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneSeedBf, (uint32_t)(uintptr_t)ctx);

    subghz_view_seed_bf_reset(subghz->subghz_seed_bf);
    subghz_view_seed_bf_set_callback(subghz->subghz_seed_bf, seed_bf_view_callback, subghz);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdSeedBf);

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    bool ok = false;
    if(fff) {
        ctx->decoder = subghz_protocol_decoder_renault_v1_alloc(NULL);
        if(ctx->decoder) {
            flipper_format_rewind(fff);
            if(subghz_protocol_decoder_renault_v1_deserialize(ctx->decoder, fff) ==
               SubGhzProtocolStatusOk) {
                subghz_protocol_renault_v1_bf_init(&ctx->bf, ctx->decoder);
                ok = true;
            }
        }
    }

    if(!ok) {
        if(ctx->decoder) {
            free(ctx->decoder);
            ctx->decoder = NULL;
        }
        subghz_view_seed_bf_set_result(
            subghz->subghz_seed_bf, false, "Missing or invalid\nKey fields.");
        return;
    }

    ctx->start_tick = furi_get_tick();
    subghz_view_seed_bf_update(subghz->subghz_seed_bf, 0, ctx->bf.total, 0);

    ctx->thread = furi_thread_alloc_ex("SeedBF", 2048, seed_bf_thread, ctx);
    furi_thread_set_priority(ctx->thread, FuriThreadPriorityLow);
    furi_thread_start(ctx->thread);

    ctx->timer = furi_timer_alloc(seed_bf_timer_cb, FuriTimerTypePeriodic, ctx);
    furi_timer_start(ctx->timer, 100);
}

bool subghz_scene_seed_bf_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    SeedBfCtx* ctx = (SeedBfCtx*)(uintptr_t)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneSeedBf);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SEED_BF_EVENT_PROGRESS) {
            if(!ctx->done_handled) {
                uint32_t elapsed = (furi_get_tick() - ctx->start_tick) / 1000U;
                subghz_view_seed_bf_update(
                    subghz->subghz_seed_bf, ctx->bf.progress, ctx->bf.total, elapsed);
            }
            return true;
        } else if(event.event == SEED_BF_EVENT_DONE) {
            if(ctx->done_handled) return true;
            ctx->done_handled = true;
            seed_bf_stop_worker(ctx);

            if(ctx->bf.state == RenaultV1BfStateFound) {
                seed_bf_save_found(ctx);
                uint32_t seed = renault_v1_iv_to_u32(ctx->bf.iv);
                char body[48];
                snprintf(body, sizeof(body), "Seed: %08lX\nSaved to file.", (unsigned long)seed);
                subghz_view_seed_bf_set_result(subghz->subghz_seed_bf, true, body);
            } else {
                subghz_view_seed_bf_set_result(
                    subghz->subghz_seed_bf,
                    false,
                    "Seed not found.\nThis fob may not be\nrecoverable from RF.");
            }
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            ctx->bf.cancel_requested = true;
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        ctx->bf.cancel_requested = true;
        scene_manager_previous_scene(subghz->scene_manager);
        return true;
    }
    return false;
}

void subghz_scene_seed_bf_on_exit(void* context) {
    SubGhz* subghz = context;
    SeedBfCtx* ctx = (SeedBfCtx*)(uintptr_t)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneSeedBf);

    if(ctx) {
        ctx->bf.cancel_requested = true;
        seed_bf_stop_worker(ctx);
        if(ctx->decoder) {
            free(ctx->decoder);
            ctx->decoder = NULL;
        }
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneSeedBf, 0);
    }
}
