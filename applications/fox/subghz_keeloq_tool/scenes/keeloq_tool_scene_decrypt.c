#include "../keeloq_tool_i.h"
#include <lib/subghz/protocols/keeloq.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/subghz_keystore.h>
#include <flipper_format/flipper_format.h>
#include <storage/storage.h>
#include <furi.h>
#include <bt/bt_service/bt.h>

#define KL_DECRYPT_EVENT_DONE        (0xD2)
#define KL_DECRYPT_EVENT_CANDIDATE   (0xD3)
#define KL_DECRYPT_EVENT_START_LOCAL (0xD4)
#define KL_TOTAL_KEYS                0x100000000ULL

#define KL_MSG_BF_REQUEST  0x10
#define KL_MSG_BF_PROGRESS 0x11
#define KL_MSG_BF_RESULT   0x12
#define KL_MSG_BF_CANCEL   0x13

typedef struct {
    KeeloqTool* app;
    volatile bool cancel;
    uint32_t start_tick;
    bool success;
    FuriString* result;

    uint32_t fix;
    uint32_t hop;
    uint32_t serial;
    uint8_t btn;
    uint16_t disc;

    uint32_t hop2;

    uint32_t candidate_count;
    uint64_t recovered_mfkey;
    uint16_t recovered_type;
    uint32_t recovered_cnt;

    bool ble_offload;

    FuriThread* local_thread;
    FuriTimer* local_timer;
    volatile uint32_t keys_tested;
} KlDecryptCtx;

static void kl_ble_data_received(uint8_t* data, uint16_t size, void* context) {
    KlDecryptCtx* ctx = context;
    if(size < 1 || ctx->cancel) return;

    if(data[0] == KL_MSG_BF_PROGRESS && size >= 10) {
        uint32_t keys_tested, keys_per_sec;
        memcpy(&keys_tested, data + 2, 4);
        memcpy(&keys_per_sec, data + 6, 4);

        uint32_t elapsed_sec = (furi_get_tick() - ctx->start_tick) / 1000;
        uint32_t remaining = (keys_tested > 0) ? (0xFFFFFFFFU - keys_tested) : 0xFFFFFFFFU;
        uint32_t eta_sec = (keys_per_sec > 0) ? (remaining / keys_per_sec) : 0;
        uint8_t pct = (uint8_t)((uint64_t)keys_tested * 100 / 0xFFFFFFFFULL);

        subghz_view_keeloq_decrypt_update_stats(
            ctx->app->keeloq_decrypt, pct, keys_tested, keys_per_sec, elapsed_sec, eta_sec);

    } else if(data[0] == KL_MSG_BF_RESULT && size >= 26) {
        uint8_t found = data[1];

        if(found == 1) {
            uint64_t mfkey = 0;
            uint32_t cnt = 0;
            memcpy(&mfkey, data + 2, 8);
            memcpy(&cnt, data + 18, 4);
            uint16_t learn_type = (size >= 27) ? data[26] : 6;

            ctx->candidate_count++;
            ctx->recovered_mfkey = mfkey;
            ctx->recovered_type = learn_type;
            ctx->recovered_cnt = cnt;

            subghz_view_keeloq_decrypt_update_candidates(
                ctx->app->keeloq_decrypt, ctx->candidate_count);

            view_dispatcher_send_custom_event(
                ctx->app->view_dispatcher, KL_DECRYPT_EVENT_CANDIDATE);

        } else if(found == 2) {
            ctx->success = (ctx->candidate_count > 0);
            view_dispatcher_send_custom_event(
                ctx->app->view_dispatcher, KL_DECRYPT_EVENT_DONE);
        }
    }
}

static void kl_ble_cleanup(KlDecryptCtx* ctx) {
    if(!ctx->ble_offload) return;
    Bt* bt = furi_record_open(RECORD_BT);
    bt_set_custom_data_callback(bt, NULL, NULL);
    furi_record_close(RECORD_BT);
    ctx->ble_offload = false;
}

static bool kl_ble_start_offload(KlDecryptCtx* ctx) {
    Bt* bt = furi_record_open(RECORD_BT);
    if(!bt_is_connected(bt)) {
        furi_record_close(RECORD_BT);
        return false;
    }

    bt_set_custom_data_callback(bt, kl_ble_data_received, ctx);

    uint8_t req[18];
    req[0] = KL_MSG_BF_REQUEST;
    req[1] = ctx->app->keeloq_bf2.learn_type;
    memcpy(req + 2, &ctx->fix, 4);
    memcpy(req + 6, &ctx->hop, 4);
    memcpy(req + 10, &ctx->hop2, 4);
    memcpy(req + 14, &ctx->serial, 4);
    bt_custom_data_tx(bt, req, sizeof(req));

    furi_record_close(RECORD_BT);
    ctx->ble_offload = true;

    subghz_view_keeloq_decrypt_set_status(
        ctx->app->keeloq_decrypt, "[BT] Offloading...");
    return true;
}

static void kl_decrypt_view_callback(KeeloqToolCustomEvent event, void* context) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static bool kl_local_check(uint32_t decrypt, uint8_t btn, uint16_t end_serial) {
    uint8_t ser = (uint8_t)((decrypt >> 16) & 0xFF);
    return ((uint8_t)(decrypt >> 28) == btn) && ((ser == end_serial) || (ser == 0));
}

static uint64_t kl_local_derive(uint8_t type, uint32_t fix, uint32_t cand) {
    switch(type) {
    case 6:
        return subghz_protocol_keeloq_common_magic_serial_type1_learning(fix, cand);
    case 7:
        return subghz_protocol_keeloq_common_magic_serial_type2_learning(fix, cand);
    case 8:
        return subghz_protocol_keeloq_common_magic_serial_type3_learning(fix, cand);
    default:
        return (uint64_t)cand;
    }
}

static int32_t kl_local_bf_thread(void* context) {
    KlDecryptCtx* ctx = context;
    uint8_t lt = ctx->app->keeloq_bf2.learn_type;
    uint8_t types[3];
    uint8_t ntypes;
    if(lt == 0) {
        types[0] = 6;
        types[1] = 7;
        types[2] = 8;
        ntypes = 3;
    } else {
        types[0] = lt;
        ntypes = 1;
    }
    uint16_t end_serial = (uint16_t)(ctx->fix & 0xFF);
    bool found = false;
    for(uint64_t cand = 0; cand < KL_TOTAL_KEYS && !ctx->cancel && !found; cand++) {
        for(uint8_t ti = 0; ti < ntypes; ti++) {
            uint64_t key = kl_local_derive(types[ti], ctx->fix, (uint32_t)cand);
            uint32_t d1 = subghz_protocol_keeloq_common_decrypt(ctx->hop, key);
            if(!kl_local_check(d1, ctx->btn, end_serial)) continue;
            uint32_t d2 = subghz_protocol_keeloq_common_decrypt(ctx->hop2, key);
            if(!kl_local_check(d2, ctx->btn, end_serial)) continue;
            ctx->recovered_mfkey = key;
            ctx->recovered_type = types[ti];
            ctx->recovered_cnt = d1 & 0xFFFFU;
            ctx->candidate_count++;
            found = true;
            view_dispatcher_send_custom_event(
                ctx->app->view_dispatcher, KL_DECRYPT_EVENT_CANDIDATE);
            break;
        }
        ctx->keys_tested = (uint32_t)cand;
    }
    ctx->success = found;
    view_dispatcher_send_custom_event(ctx->app->view_dispatcher, KL_DECRYPT_EVENT_DONE);
    return 0;
}

static void kl_local_timer_cb(void* context) {
    KlDecryptCtx* ctx = context;
    uint32_t elapsed = (furi_get_tick() - ctx->start_tick) / 1000U;
    uint32_t kt = ctx->keys_tested;
    uint8_t pct = (uint8_t)((uint64_t)kt * 100U / 0xFFFFFFFFULL);
    uint32_t kps = (elapsed > 0U) ? (kt / elapsed) : 0U;
    uint32_t remaining = 0xFFFFFFFFU - kt;
    uint32_t eta = (kps > 0U) ? (remaining / kps) : 0U;
    subghz_view_keeloq_decrypt_update_stats(
        ctx->app->keeloq_decrypt, pct, kt, kps, elapsed, eta);
}

static void kl_local_start(KlDecryptCtx* ctx) {
    ctx->start_tick = furi_get_tick();
    subghz_view_keeloq_decrypt_reset(ctx->app->keeloq_decrypt);
    subghz_view_keeloq_decrypt_set_callback(
        ctx->app->keeloq_decrypt, kl_decrypt_view_callback, ctx->app);
    view_dispatcher_switch_to_view(ctx->app->view_dispatcher, KeeloqToolViewKeeloqDecrypt);
    subghz_view_keeloq_decrypt_set_status(ctx->app->keeloq_decrypt, "On-device BF...");
    ctx->local_timer = furi_timer_alloc(kl_local_timer_cb, FuriTimerTypePeriodic, ctx);
    furi_timer_start(ctx->local_timer, 500);
    ctx->local_thread = furi_thread_alloc_ex("KLBF", 2048, kl_local_bf_thread, ctx);
    furi_thread_set_priority(ctx->local_thread, FuriThreadPriorityLow);
    furi_thread_start(ctx->local_thread);
}

static void kl_local_stop(KlDecryptCtx* ctx) {
    if(ctx->local_timer) {
        furi_timer_stop(ctx->local_timer);
        furi_timer_free(ctx->local_timer);
        ctx->local_timer = NULL;
    }
    if(ctx->local_thread) {
        ctx->cancel = true;
        furi_thread_join(ctx->local_thread);
        furi_thread_free(ctx->local_thread);
        ctx->local_thread = NULL;
    }
}

static void kl_local_popup_callback(void* context) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, KL_DECRYPT_EVENT_START_LOCAL);
}

void keeloq_tool_scene_decrypt_on_enter(void* context) {
    KeeloqTool* app = context;

    KlDecryptCtx* ctx = malloc(sizeof(KlDecryptCtx));
    memset(ctx, 0, sizeof(KlDecryptCtx));
    ctx->app = app;
    ctx->result = furi_string_alloc_set("No result");

    FlipperFormat* fff = flipper_format_file_alloc(app->storage);
    if(flipper_format_file_open_existing(fff, furi_string_get_cstr(app->signal_path))) {
        flipper_format_rewind(fff);
        uint8_t key_data[8] = {0};
        if(flipper_format_read_hex(fff, "Key", key_data, 8)) {
            uint64_t raw = 0;
            for(uint8_t i = 0; i < 8; i++) {
                raw = (raw << 8) | key_data[i];
            }
            uint64_t reversed = subghz_protocol_blocks_reverse_key(raw, 64);
            ctx->fix = (uint32_t)(reversed >> 32);
            ctx->hop = (uint32_t)(reversed & 0xFFFFFFFF);
        }
    }
    flipper_format_free(fff);

    ctx->serial = ctx->fix & 0x0FFFFFFF;
    ctx->btn = ctx->fix >> 28;
    ctx->disc = ctx->serial & 0x3FF;
    ctx->hop2 = app->keeloq_bf2.sig2_loaded ? app->keeloq_bf2.hop2 : 0;

    scene_manager_set_scene_state(
        app->scene_manager, KeeloqToolSceneDecrypt, (uint32_t)(uintptr_t)ctx);

    subghz_view_keeloq_decrypt_reset(app->keeloq_decrypt);
    subghz_view_keeloq_decrypt_set_callback(
        app->keeloq_decrypt, kl_decrypt_view_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewKeeloqDecrypt);

    ctx->start_tick = furi_get_tick();

    if(!kl_ble_start_offload(ctx)) {
        Popup* popup = app->popup;
        popup_reset(popup);
        popup_set_context(popup, app);
        popup_set_callback(popup, kl_local_popup_callback);
        popup_set_header(popup, "On-device KeeLoq BF", 64, 8, AlignCenter, AlignTop);
        popup_set_text(
            popup,
            "No ESP32/companion.\nSearches 2^32 keys,\ncan take DAYS.\nOK start, Back cancel.",
            64,
            22,
            AlignCenter,
            AlignTop);
        popup_disable_timeout(popup);
        view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewPopup);
    }
}

bool keeloq_tool_scene_decrypt_on_event(void* context, SceneManagerEvent event) {
    KeeloqTool* app = context;
    KlDecryptCtx* ctx = (KlDecryptCtx*)(uintptr_t)scene_manager_get_scene_state(
        app->scene_manager, KeeloqToolSceneDecrypt);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KL_DECRYPT_EVENT_CANDIDATE) {
            bool own_manager = !app->keeloq_keys_manager;
            if(own_manager) {
                app->keeloq_keys_manager = subghz_keeloq_keys_alloc();
            }
            char key_name[24];
            snprintf(key_name, sizeof(key_name), "BF_%07lX", ctx->serial);
            subghz_keeloq_keys_add(
                app->keeloq_keys_manager,
                ctx->recovered_mfkey,
                KEELOQ_LEARNING_SIMPLE,
                key_name);
            subghz_keeloq_keys_save(app->keeloq_keys_manager);
            if(own_manager) {
                subghz_keeloq_keys_free(app->keeloq_keys_manager);
                app->keeloq_keys_manager = NULL;
            }
            return true;

        } else if(event.event == KL_DECRYPT_EVENT_START_LOCAL) {
            popup_reset(app->popup);
            kl_local_start(ctx);
            return true;

        } else if(event.event == KL_DECRYPT_EVENT_DONE) {
            if(ctx->local_timer) {
                furi_timer_stop(ctx->local_timer);
                furi_timer_free(ctx->local_timer);
                ctx->local_timer = NULL;
            }
            kl_ble_cleanup(ctx);
            app->keeloq_bf2.sig1_loaded = false;
            app->keeloq_bf2.sig2_loaded = false;

            if(ctx->success) {
                furi_string_printf(
                    ctx->result,
                    "Found %lu candidate(s)\n"
                    "Last: %08lX%08lX\n"
                    "Type:%u Cnt:%04lX\n"
                    "Saved to user keys",
                    ctx->candidate_count,
                    (uint32_t)(ctx->recovered_mfkey >> 32),
                    (uint32_t)(ctx->recovered_mfkey & 0xFFFFFFFF),
                    ctx->recovered_type,
                    ctx->recovered_cnt);

                const char* save_path = NULL;
                if(!furi_string_empty(app->file_path)) {
                    save_path = furi_string_get_cstr(app->file_path);
                } else if(!furi_string_empty(app->keeloq_bf2.sig1_path)) {
                    save_path = furi_string_get_cstr(app->keeloq_bf2.sig1_path);
                }
                if(save_path) {
                    char mf_str[20];
                    snprintf(mf_str, sizeof(mf_str), "BF_%07lX", ctx->serial);
                    uint32_t cnt_val = ctx->recovered_cnt;
                    FlipperFormat* fff = flipper_format_file_alloc(app->storage);
                    if(flipper_format_file_open_existing(fff, save_path)) {
                        flipper_format_insert_or_update_string_cstr(fff, "Manufacture", mf_str);
                        flipper_format_rewind(fff);
                        flipper_format_insert_or_update_uint32(fff, "Cnt", &cnt_val, 1);
                        if(ctx->hop2 != 0) {
                            flipper_format_rewind(fff);
                            flipper_format_insert_or_update_uint32(fff, "Hop2", &ctx->hop2, 1);
                        }
                    }
                    flipper_format_free(fff);
                    furi_string_set_str(app->file_path, save_path);
                }

                subghz_view_keeloq_decrypt_set_result(
                    app->keeloq_decrypt, true, furi_string_get_cstr(ctx->result));
            } else if(!ctx->cancel) {
                subghz_view_keeloq_decrypt_set_result(
                    app->keeloq_decrypt, false,
                    "Key NOT found.\nNo matching key in\n2^32 search space.");
            } else {
                subghz_view_keeloq_decrypt_set_result(
                    app->keeloq_decrypt, false, "Cancelled.");
            }
            return true;

        } else if(event.event == KeeloqToolCustomEventViewBack) {
            if(ctx->ble_offload) {
                Bt* bt = furi_record_open(RECORD_BT);
                uint8_t cancel_msg = KL_MSG_BF_CANCEL;
                bt_custom_data_tx(bt, &cancel_msg, 1);
                furi_record_close(RECORD_BT);
            }
            ctx->cancel = true;
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
    }
    return false;
}

void keeloq_tool_scene_decrypt_on_exit(void* context) {
    KeeloqTool* app = context;
    KlDecryptCtx* ctx = (KlDecryptCtx*)(uintptr_t)scene_manager_get_scene_state(
        app->scene_manager, KeeloqToolSceneDecrypt);

    if(ctx) {
        kl_ble_cleanup(ctx);
        ctx->cancel = true;
        kl_local_stop(ctx);
        furi_string_free(ctx->result);
        free(ctx);
        scene_manager_set_scene_state(app->scene_manager, KeeloqToolSceneDecrypt, 0);
    }
}
