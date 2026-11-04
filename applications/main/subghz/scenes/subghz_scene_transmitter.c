#include "../subghz_i.h"
#include "../views/transmitter.h"

#include <lib/subghz/blocks/custom_btn.h>
#include <string.h>

#include <lib/subghz/devices/devices.c>
#include "applications/main/subghz/helpers/subghz_txrx_i.h"
#include "lib/subghz/blocks/generic.h"

#define TAG "SubGhzSceneTransmitter"

void subghz_scene_transmitter_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static bool txsc_btn_label(const char* s, char* d, size_t n) {
    const char* btn = strstr(s, "Btn:");
    const char* p   = btn ? strstr(btn, "[") : NULL;
    if(!p) { d[0]=0; return false; }
    p++; size_t i=0;
    while(p[i] && p[i]!=']' && i<n-1) { d[i]=p[i]; i++; }
    d[i]=0;
    return i>0;
}

bool subghz_scene_transmitter_update_data_show(void* context) {

    SubGhz* subghz = context;
    bool ret = false;
    SubGhzProtocolDecoderBase* decoder = subghz_txrx_get_decoder(subghz->txrx);

    if(!decoder) {
        subghz_view_transmitter_set_radio_device_type(
            subghz->subghz_transmitter, subghz_txrx_radio_device_get(subghz->txrx));
        return false;
    }

    FuriString* key_str        = furi_string_alloc();
    FuriString* frequency_str  = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();
    FlipperFormat* fff         = subghz_txrx_get_fff_data(subghz->txrx);

    uint8_t active_btn = subghz_custom_btn_get();

    subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_OK);
    if(subghz_protocol_decoder_base_deserialize(decoder, fff) == SubGhzProtocolStatusOk) {
        subghz_protocol_decoder_base_get_string(decoder, key_str);

        subghz_txrx_get_frequency_and_modulation(
            subghz->txrx, frequency_str, modulation_str, true);

        subghz_view_transmitter_add_data_to_show(
            subghz->subghz_transmitter,
            furi_string_get_cstr(key_str),
            furi_string_get_cstr(frequency_str),
            furi_string_get_cstr(modulation_str),
            subghz_txrx_protocol_is_transmittable(subghz->txrx, false));

        if(subghz_custom_btn_is_allowed()) {

            if(active_btn == SUBGHZ_CUSTOM_BTN_OK ||
               !subghz_view_transmitter_is_labels_ready(subghz->subghz_transmitter)) {

                char clabel[14] = {0};
                txsc_btn_label(furi_string_get_cstr(key_str), clabel, sizeof(clabel));
                if(!clabel[0]) snprintf(clabel, sizeof(clabel), "SEND");

                FuriString* proto_str = furi_string_alloc();
                flipper_format_rewind(fff);
                flipper_format_read_string(fff, "Protocol", proto_str);
                const char* pname = furi_string_get_cstr(proto_str);

                const char* ul = subghz_custom_btn_get_label_for_proto(pname, SUBGHZ_CUSTOM_BTN_UP);
                const char* dl = subghz_custom_btn_get_label_for_proto(pname, SUBGHZ_CUSTOM_BTN_DOWN);
                const char* ll = subghz_custom_btn_get_label_for_proto(pname, SUBGHZ_CUSTOM_BTN_LEFT);
                const char* rl = subghz_custom_btn_get_label_for_proto(pname, SUBGHZ_CUSTOM_BTN_RIGHT);

                furi_string_free(proto_str);

                bool u_active = (ul != NULL) && (strcmp(ul, clabel) != 0);
                bool d_active = (dl != NULL) && (strcmp(dl, clabel) != 0);
                bool l_active = (ll != NULL) && (strcmp(ll, clabel) != 0);
                bool r_active = (rl != NULL) && (strcmp(rl, clabel) != 0);

                if(!ul) ul = "^";
                if(!dl) dl = "v";
                if(!ll) ll = "<";
                if(!rl) rl = ">";

                bool u_vis = (strcmp(ul, clabel) != 0);
                bool d_vis = (strcmp(dl, clabel) != 0);
                bool l_vis = (strcmp(ll, clabel) != 0);
                bool r_vis = (strcmp(rl, clabel) != 0);

                subghz_view_transmitter_set_btn_labels(
                    subghz->subghz_transmitter,
                    clabel,
                    ul, u_vis, u_active,
                    dl, d_vis, d_active,
                    ll, l_vis, l_active,
                    rl, r_vis, r_active);
            }

        }

        ret = true;
    }
    furi_string_free(frequency_str);
    furi_string_free(modulation_str);
    furi_string_free(key_str);

    subghz_view_transmitter_set_radio_device_type(
        subghz->subghz_transmitter, subghz_txrx_radio_device_get(subghz->txrx));
    return ret;
}

void subghz_scene_transmitter_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz_custom_btns_reset();

    subghz_view_transmitter_reset_labels(subghz->subghz_transmitter);

    char raw_title_override[36] = {0};
    if(!furi_string_empty(subghz->raw_send_title)) {
        strlcpy(
            raw_title_override, furi_string_get_cstr(subghz->raw_send_title),
            sizeof(raw_title_override));
    }
    furi_string_reset(subghz->raw_send_title);

    bool raw_send_mode =
        (scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneTransmitter) == 1);

    if(raw_send_mode) {
        /* RAW replay of a decode-only capture: there is no decoder/encoder to
         * deserialize, so skip update_data_show (which would report "not found").
         * Show the centre Send d-pad with inert arrows (show_button path, no
         * custom buttons) and let OK fire subghz_tx_start on the RAW fff_data. */
        subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_OK);
        FuriString* frequency_str = furi_string_alloc();
        FuriString* modulation_str = furi_string_alloc();
        subghz_txrx_get_frequency_and_modulation(
            subghz->txrx, frequency_str, modulation_str, true);
        FuriString* raw_title = furi_string_alloc();
        if(raw_title_override[0]) {
            furi_string_set(raw_title, raw_title_override);
        } else {
            FlipperFormat* raw_fff = subghz_txrx_get_fff_data(subghz->txrx);
            flipper_format_rewind(raw_fff);
            if(!flipper_format_read_string(raw_fff, "Protocol", raw_title) ||
               furi_string_empty(raw_title)) {
                furi_string_set(raw_title, "RAW");
            }
        }
        /* Append "RAW" after a real protocol name so the user knows this is a raw
         * replay, not an encoded send (new: SEND on un-decodable signals). A pure
         * RAW/BinRAW capture stays just "RAW". */
        if(furi_string_cmp_str(raw_title, "RAW") != 0 &&
           furi_string_cmp_str(raw_title, "BinRAW") != 0) {
            furi_string_cat_str(raw_title, " RAW");
        }
        subghz_view_transmitter_add_data_to_show(
            subghz->subghz_transmitter,
            furi_string_get_cstr(raw_title),
            furi_string_get_cstr(frequency_str),
            furi_string_get_cstr(modulation_str),
            true);
        furi_string_free(raw_title);
        subghz_view_transmitter_set_radio_device_type(
            subghz->subghz_transmitter, subghz_txrx_radio_device_get(subghz->txrx));
        furi_string_free(frequency_str);
        furi_string_free(modulation_str);
    } else if(!subghz_scene_transmitter_update_data_show(subghz)) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventViewTransmitterError);
    }

    subghz_view_transmitter_set_callback(
        subghz->subghz_transmitter, subghz_scene_transmitter_callback, subghz);

    subghz->state_notifications = SubGhzNotificationStateIDLE;
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdTransmitter);
}

bool subghz_scene_transmitter_on_event(void* context, SceneManagerEvent event) {

    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventViewTransmitterSendStart) {

            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_block_generic_global.endless_tx = true;
            if(subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx))) {
                subghz->state_notifications = SubGhzNotificationStateTx;
                if(scene_manager_get_scene_state(
                       subghz->scene_manager, SubGhzSceneTransmitter) != 1) {
                    subghz_scene_transmitter_update_data_show(subghz);
                }
            }
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterSendStop) {

            subghz_block_generic_global.endless_tx = false;
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            if(scene_manager_has_previous_scene(
                   subghz->scene_manager, SubGhzSceneReceiverInfo)) {
                scene_manager_search_and_switch_to_previous_scene(
                    subghz->scene_manager, SubGhzSceneReceiverInfo);
            } else {
                scene_manager_search_and_switch_to_previous_scene(
                    subghz->scene_manager, SubGhzSceneStart);
            }
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterPageChange) {

            subghz_scene_transmitter_update_data_show(subghz);
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterError) {
            furi_string_set(subghz->error_str, "Protocol not\nfound!");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(subghz->state_notifications == SubGhzNotificationStateTx) {

            if(!subghz_devices_is_async_complete_tx(subghz->txrx->radio_device)) {
                subghz_view_transmitter_set_send_progress(
                    subghz->subghz_transmitter,
                    subghz_txrx_get_raw_tx_progress(subghz->txrx));
                notification_message(subghz->notifications, &sequence_blink_magenta_10);
                return true;

            } else {
                subghz->state_notifications = SubGhzNotificationStateIDLE;
                subghz_view_transmitter_set_send_progress(subghz->subghz_transmitter, 255);
                subghz_txrx_stop(subghz->txrx);
                if(subghz_custom_btn_get() != SUBGHZ_CUSTOM_BTN_OK) {
                    subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_OK);
                    int32_t tmp_counter = furi_hal_subghz_get_rolling_counter_mult();
                    furi_hal_subghz_set_rolling_counter_mult(0);

                    subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx));
                    subghz_txrx_stop(subghz->txrx);

                    subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx));
                    subghz_txrx_stop(subghz->txrx);
                    furi_hal_subghz_set_rolling_counter_mult(tmp_counter);
                }
            }
        }
        return true;
    }
    return false;
}

void subghz_scene_transmitter_on_exit(void* context) {
    SubGhz* subghz = context;
    subghz->state_notifications = SubGhzNotificationStateIDLE;
    subghz_block_generic_global.endless_tx = false;
    subghz_txrx_stop(subghz->txrx);

    /* Clear the RAW-send marker so a later normal entry isn't treated as RAW. */
    scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneTransmitter, 0);

    subghz_txrx_reset_dynamic_and_custom_btns(subghz->txrx);
}
