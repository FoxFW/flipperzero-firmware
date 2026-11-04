#include "../subghz_i.h"
#include "../helpers/subghz_txrx_i.h"
#include <lib/subghz/protocols/fiat_v1.h>
#include <furi.h>
#include <bt/bt_service/bt.h>

#define FIAT_V1_BT_EV_DONE (0xF2)

/* Dedicated BT/ESP32 offload: the whole 95-key Hitag2 recovery runs on a connected
 * companion (the Fox app over BLE), not on the Flipper. Requires a live BT link; with
 * none, nothing is started and a Retry screen is shown. Companion protocol matches
 * Fox_ESP32_FW/fox_fiat_bf.cpp (FoxFiatBf::onBleData), little-endian:
 *   REQUEST  [0]=0x20 [1..4]=uid [5]=button [6..7]=control [8..11]=hop [12]=key_start [13]=key_end (14 bytes)
 *   PROGRESS [0]=0x21 [1]=pct [2..5]=combos_tested                                     (>=6 bytes)
 *   RESULT   [0]=0x22 [1]=found [2..7]=key[6] [8..11]=epoch                            (>=12 bytes)
 *   CANCEL   [0]=0x23                                                                  (1 byte)
 * key_start=0/key_end=95 sweeps every known key on the companion. */
#define FIAT_V1_BT_MSG_REQUEST 0x20
#define FIAT_V1_BT_MSG_PROGRESS 0x21
#define FIAT_V1_BT_MSG_RESULT 0x22
#define FIAT_V1_BT_MSG_CANCEL 0x23

typedef enum {
    FiatBtPhaseConnect = 0, // not connected: Retry screen shown, nothing started
    FiatBtPhaseOffload,     // request sent, companion sweeping all keys
    FiatBtPhaseResult,
} FiatBtPhase;

typedef struct {
    SubGhz* subghz;
    volatile bool cancel;
    uint32_t start_tick;

    uint32_t uid;
    uint8_t button;
    uint16_t control;
    uint32_t hop;

    uint8_t phase;
    bool ble_offload;

    bool success;
    uint8_t found_key[6];
    uint32_t found_epoch;
} FiatV1RecoverBtCtx;

static void fiat_v1_bt_view_callback(SubGhzCustomEvent event, void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static void fiat_v1_bt_widget_callback(GuiButtonType result, InputType type, void* context) {
    SubGhz* subghz = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, result);
    }
}

static void fiat_v1_bt_ble_data_received(uint8_t* data, uint16_t size, void* context) {
    FiatV1RecoverBtCtx* ctx = context;
    if(size < 1 || ctx->cancel) return;

    if(data[0] == FIAT_V1_BT_MSG_PROGRESS && size >= 6) {
        uint8_t pct = data[1];
        uint32_t combos_tested;
        memcpy(&combos_tested, data + 2, 4);
        uint32_t total = fiat_v1_hitag2_recovery_total();
        if(!total) total = 1U;
        uint32_t elapsed_sec = (furi_get_tick() - ctx->start_tick) / 1000;
        uint32_t combos_per_sec = (elapsed_sec > 0) ? (combos_tested / elapsed_sec) : 0;
        uint32_t remaining = (combos_tested < total) ? (total - combos_tested) : 0;
        uint32_t eta_sec = (combos_per_sec > 0) ? (remaining / combos_per_sec) : 0;
        subghz_view_fiat_v1_recover_update_stats(
            ctx->subghz->subghz_fiat_v1_recover,
            pct,
            combos_tested,
            combos_per_sec,
            elapsed_sec,
            eta_sec);
    } else if(data[0] == FIAT_V1_BT_MSG_RESULT && size >= 12) {
        ctx->success = (data[1] != 0);
        if(ctx->success) {
            memcpy(ctx->found_key, data + 2, 6);
            memcpy(&ctx->found_epoch, data + 8, 4);
        }
        view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, FIAT_V1_BT_EV_DONE);
    }
}

static void fiat_v1_bt_ble_cleanup(FiatV1RecoverBtCtx* ctx) {
    if(!ctx->ble_offload) return;
    Bt* bt = furi_record_open(RECORD_BT);
    bt_set_custom_data_callback(bt, NULL, NULL);
    furi_record_close(RECORD_BT);
    ctx->ble_offload = false;
}

static void fiat_v1_bt_abort(FiatV1RecoverBtCtx* ctx) {
    ctx->cancel = true;
    if(ctx->ble_offload) {
        Bt* bt = furi_record_open(RECORD_BT);
        uint8_t cancel_msg = FIAT_V1_BT_MSG_CANCEL;
        bt_custom_data_tx(bt, &cancel_msg, 1);
        bt_set_custom_data_callback(bt, NULL, NULL);
        furi_record_close(RECORD_BT);
        ctx->ble_offload = false;
    }
}

static bool fiat_v1_bt_start_offload(FiatV1RecoverBtCtx* ctx) {
    Bt* bt = furi_record_open(RECORD_BT);
    if(!bt_is_connected(bt)) {
        furi_record_close(RECORD_BT);
        return false;
    }

    bt_set_custom_data_callback(bt, fiat_v1_bt_ble_data_received, ctx);

    uint8_t req[14];
    req[0] = FIAT_V1_BT_MSG_REQUEST;
    memcpy(req + 1, &ctx->uid, 4);
    req[5] = ctx->button;
    memcpy(req + 6, &ctx->control, 2);
    memcpy(req + 8, &ctx->hop, 4);
    req[12] = 0U;
    req[13] = (uint8_t)FIAT_V1_KNOWN_KEY_COUNT;
    bt_custom_data_tx(bt, req, sizeof(req));

    furi_record_close(RECORD_BT);

    ctx->ble_offload = true;
    ctx->cancel = false;
    ctx->start_tick = furi_get_tick();
    ctx->phase = FiatBtPhaseOffload;
    subghz_view_fiat_v1_recover_reset(ctx->subghz->subghz_fiat_v1_recover);
    view_dispatcher_switch_to_view(ctx->subghz->view_dispatcher, SubGhzViewIdFiatV1Recover);
    return true;
}

static void fiat_v1_bt_draw_connect_error(SubGhz* subghz) {
    widget_reset(subghz->widget);
    widget_add_text_box_element(
        subghz->widget,
        0,
        0,
        128,
        44,
        AlignCenter,
        AlignTop,
        "\e#No BT Companion\e#\nPair the Fox app over\nBluetooth, then Retry.",
        true);
    widget_add_button_element(
        subghz->widget, GuiButtonTypeCenter, "Retry", fiat_v1_bt_widget_callback, subghz);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

static void fiat_v1_bt_draw_result(SubGhz* subghz, FiatV1RecoverBtCtx* ctx) {
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
            subghz->widget, GuiButtonTypeLeft, "Apply", fiat_v1_bt_widget_callback, subghz);
        widget_add_button_element(
            subghz->widget, GuiButtonTypeRight, "CBF", fiat_v1_bt_widget_callback, subghz);
    } else {
        widget_add_text_box_element(
            subghz->widget,
            0,
            0,
            128,
            44,
            AlignCenter,
            AlignTop,
            "\e#No Key Found\e#\nCompanion reported\nno match.",
            true);
        widget_add_button_element(
            subghz->widget, GuiButtonTypeLeft, "Send Raw", fiat_v1_bt_widget_callback, subghz);
        widget_add_button_element(
            subghz->widget, GuiButtonTypeRight, "Manual", fiat_v1_bt_widget_callback, subghz);
    }
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

static void fiat_v1_bt_apply_and_save(SubGhz* subghz, FiatV1RecoverBtCtx* ctx) {
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    if(fff) {
        flipper_format_insert_or_update_hex(
            fff, FIAT_V1_HITAG2_KEY_FIELD, ctx->found_key, sizeof(ctx->found_key));
        flipper_format_insert_or_update_uint32(
            fff, FIAT_V1_HITAG2_EPOCH_FIELD, &ctx->found_epoch, 1U);
        subghz_save_protocol_to_file(subghz, fff, furi_string_get_cstr(subghz->file_path));
    }
}

static void fiat_v1_bt_teardown(SubGhz* subghz, FiatV1RecoverBtCtx* ctx) {
    fiat_v1_bt_abort(ctx);
    scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneFiatV1RecoverBt, 0);
    free(ctx);
}

void subghz_scene_fiat_v1_recover_bt_on_enter(void* context) {
    SubGhz* subghz = context;

    FiatV1RecoverBtCtx* ctx = malloc(sizeof(FiatV1RecoverBtCtx));
    furi_check(ctx);
    memset(ctx, 0, sizeof(FiatV1RecoverBtCtx));
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
        subghz->scene_manager, SubGhzSceneFiatV1RecoverBt, (uint32_t)(uintptr_t)ctx);

    subghz_view_fiat_v1_recover_set_callback(
        subghz->subghz_fiat_v1_recover, fiat_v1_bt_view_callback, subghz);

    if(!fiat_v1_bt_start_offload(ctx)) {
        ctx->phase = FiatBtPhaseConnect;
        fiat_v1_bt_draw_connect_error(subghz);
    }
}

bool subghz_scene_fiat_v1_recover_bt_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    FiatV1RecoverBtCtx* ctx = (FiatV1RecoverBtCtx*)(uintptr_t)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneFiatV1RecoverBt);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FIAT_V1_BT_EV_DONE) {
            fiat_v1_bt_ble_cleanup(ctx);
            if(ctx->success) {
                fiat_v1_bt_apply_and_save(subghz, ctx);
            }
            ctx->phase = FiatBtPhaseResult;
            fiat_v1_bt_draw_result(subghz, ctx);
            return true;

        } else if(event.event == GuiButtonTypeCenter && ctx->phase == FiatBtPhaseConnect) {
            if(!fiat_v1_bt_start_offload(ctx)) {
                fiat_v1_bt_draw_connect_error(subghz);
            }
            return true;

        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            fiat_v1_bt_teardown(subghz, ctx);
            scene_manager_previous_scene(subghz->scene_manager);
            return true;

        } else if(event.event == GuiButtonTypeLeft && ctx->phase == FiatBtPhaseResult) {
            fiat_v1_bt_teardown(subghz, ctx);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
            return true;

        } else if(event.event == GuiButtonTypeRight && ctx->phase == FiatBtPhaseResult) {
            bool was_success = ctx->success;
            fiat_v1_bt_teardown(subghz, ctx);
            scene_manager_next_scene(
                subghz->scene_manager,
                was_success ? SubGhzSceneCounterBf : SubGhzSceneFiatV1Key);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(ctx->phase == FiatBtPhaseOffload) {
            return true; // block Back while offloading; the view sends a cancel event
        }
        fiat_v1_bt_teardown(subghz, ctx);
        scene_manager_previous_scene(subghz->scene_manager);
        return true;
    }
    return false;
}

void subghz_scene_fiat_v1_recover_bt_on_exit(void* context) {
    SubGhz* subghz = context;
    FiatV1RecoverBtCtx* ctx = (FiatV1RecoverBtCtx*)(uintptr_t)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneFiatV1RecoverBt);

    if(ctx) {
        fiat_v1_bt_abort(ctx);
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneFiatV1RecoverBt, 0);
    }
    widget_reset(subghz->widget);
}
