#include "../subghz_i.h"
#include "../helpers/subghz_saved_match.h"

#include <lib/subghz/blocks/custom_btn.h>
#include <string.h>
#include <flipper_format/flipper_format_i.h>

#include "applications/main/subghz/helpers/subghz_txrx_i.h"
#include <lib/subghz/blocks/generic.h>
#include <toolbox/name_generator.h>
#include <gui/modules/fox_theme.h>

#define TAG "SubGhzSceneReceiverInfo"

static char subghz_scene_receiver_info_psa_popup_text[96];

static void subghz_scene_receiver_info_psa_popup_callback(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(
        subghz->view_dispatcher, SubGhzCustomEventSceneReceiverInfoPsaDecryptGo);
}

void subghz_scene_receiver_info_callback(GuiButtonType result, InputType type, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;

    int info_mode =
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReceiverInfo);

    if(result == GuiButtonTypeCenter) {
        if(type == InputTypeShort) {
            if(info_mode == 1) {
                view_dispatcher_send_custom_event(
                    subghz->view_dispatcher, SubGhzCustomEventSceneReceiverInfoDecrypt);
            } else if(info_mode == 2) {
                view_dispatcher_send_custom_event(
                    subghz->view_dispatcher, SubGhzCustomEventSceneReceiverInfoSendRaw);
            } else {
                view_dispatcher_send_custom_event(
                    subghz->view_dispatcher, SubGhzCustomEventSceneReceiverInfoSend);
            }
        }
    } else if((result == GuiButtonTypeRight) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneReceiverInfoSave);
    } else if((result == GuiButtonTypeLeft) && (type == InputTypeShort)) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneReceiverInfoTxFull);
    }
}

static bool subghz_scene_receiver_info_update_parser(void* context) {
    SubGhz* subghz = context;

    if(subghz_txrx_load_decoder_by_name_protocol(
           subghz->txrx,
           subghz_history_get_protocol_name(subghz->history, subghz->idx_menu_chosen))) {

        subghz_protocol_decoder_base_deserialize(
            subghz_txrx_get_decoder(subghz->txrx),
            subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen));

        SubGhzRadioPreset* preset =
            subghz_history_get_radio_preset(subghz->history, subghz->idx_menu_chosen);

        subghz_txrx_set_tx_power(preset->data, preset->data_size, subghz->tx_power);

        subghz_txrx_set_preset(
            subghz->txrx,
            furi_string_get_cstr(preset->name),
            preset->frequency,
            preset->data,
            preset->data_size);

        return true;
    }
    return false;
}

static const char* subghz_scene_receiver_info_decrypt_label(SubGhz* subghz) {
    FlipperFormat* rx_ff =
        subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
    return subghz_recovery_action_label(subghz_recovery_primary_action(rx_ff));
}

void subghz_scene_receiver_info_draw_widget(SubGhz* subghz) {
    if(subghz_scene_receiver_info_update_parser(subghz)) {
        FuriString* frequency_str = furi_string_alloc();
        FuriString* modulation_str = furi_string_alloc();
        FuriString* text = furi_string_alloc();

        subghz_txrx_get_frequency_and_modulation(
            subghz->txrx, frequency_str, modulation_str, false);
        widget_add_string_element(
            subghz->widget,
            78,
            0,
            AlignLeft,
            AlignTop,
            FontSecondary,
            furi_string_get_cstr(frequency_str));

        widget_add_string_element(
            subghz->widget,
            113,
            0,
            AlignLeft,
            AlignTop,
            FontSecondary,
            furi_string_get_cstr(modulation_str));
        subghz_protocol_decoder_base_get_string(subghz_txrx_get_decoder(subghz->txrx), text);
        widget_add_text_scroll_element(
            subghz->widget, 0, 0, 128, 47, furi_string_get_cstr(text));

        furi_string_free(frequency_str);
        furi_string_free(modulation_str);
        furi_string_free(text);

        if(subghz_txrx_protocol_is_serializable(subghz->txrx)) {
            FlipperFormat* rx_ff =
                subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
            FuriString* matched_name = furi_string_alloc();
            FuriString* matched_path = furi_string_alloc();
            bool has_saved_match =
                subghz_saved_match_signal(rx_ff, matched_name, matched_path);
            furi_string_free(matched_name);
            furi_string_free(matched_path);

            bool from_decode_raw = scene_manager_has_previous_scene(
                subghz->scene_manager, SubGhzSceneDecodeRAW);

            widget_add_button_element(
                subghz->widget,
                GuiButtonTypeRight,
                (has_saved_match && !from_decode_raw) ? "Update" : "Save",
                subghz_scene_receiver_info_callback,
                subghz);
        }

        FlipperFormat* info_rx_ff =
            subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
        bool info_needs_decrypt =
            (subghz_recovery_primary_action(info_rx_ff) != SubGhzRecoveryActionNone);
        bool info_transmittable = subghz_txrx_protocol_is_transmittable(subghz->txrx, false);
        bool info_raw_capable = info_transmittable &&
            (subghz_txrx_get_decoder(subghz->txrx)->protocol->flag &
             SubGhzProtocolFlag_RawReplay);
        bool wdg_from_decode_raw =
            scene_manager_has_previous_scene(subghz->scene_manager, SubGhzSceneDecodeRAW);
        bool info_raw_available = (!info_transmittable) && wdg_from_decode_raw &&
                                  !furi_string_empty(subghz->file_path);

        scene_manager_set_scene_state(
            subghz->scene_manager,
            SubGhzSceneReceiverInfo,
            info_needs_decrypt ? 1 : (info_raw_available ? 2 : 0));

        if(info_needs_decrypt) {
            if(info_raw_capable) {
                widget_add_button_element(
                    subghz->widget,
                    GuiButtonTypeLeft,
                    "Raw",
                    subghz_scene_receiver_info_callback,
                    subghz);
            }
            widget_add_button_element(
                subghz->widget,
                GuiButtonTypeCenter,
                subghz_scene_receiver_info_decrypt_label(subghz),
                subghz_scene_receiver_info_callback,
                subghz);
        } else if(info_transmittable) {
            widget_add_button_element(
                subghz->widget,
                GuiButtonTypeCenter,
                "Send",
                subghz_scene_receiver_info_callback,
                subghz);
            if(info_raw_capable) {
                widget_add_button_element(
                    subghz->widget,
                    GuiButtonTypeLeft,
                    "Raw",
                    subghz_scene_receiver_info_callback,
                    subghz);
            }
        } else if(info_raw_available) {
            widget_add_button_element(
                subghz->widget,
                GuiButtonTypeCenter,
                "Send",
                subghz_scene_receiver_info_callback,
                subghz);
        }
    } else {

        widget_add_string_element(
            subghz->widget, 13, 8, AlignLeft, AlignBottom, FontSecondary, "Error history parse.");
    }

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

static void subghz_scene_receiver_info_bar_callback(void* context, uint32_t event) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static bool subghz_scene_receiver_info_draw_bar(SubGhz* subghz) {
    if(!subghz_scene_receiver_info_update_parser(subghz)) {
        return false;
    }

    FuriString* frequency_str = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();
    FuriString* text = furi_string_alloc();
    subghz_txrx_get_frequency_and_modulation(subghz->txrx, frequency_str, modulation_str, false);
    subghz_protocol_decoder_base_get_string(subghz_txrx_get_decoder(subghz->txrx), text);

    bool is_serializable = subghz_txrx_protocol_is_serializable(subghz->txrx);
    FlipperFormat* info_rx_ff =
        subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
    bool info_needs_decrypt =
        (subghz_recovery_primary_action(info_rx_ff) != SubGhzRecoveryActionNone);
    bool info_transmittable = subghz_txrx_protocol_is_transmittable(subghz->txrx, false);
    bool info_raw_capable = info_transmittable &&
        (subghz_txrx_get_decoder(subghz->txrx)->protocol->flag & SubGhzProtocolFlag_RawReplay);
    bool from_decode_raw =
        scene_manager_has_previous_scene(subghz->scene_manager, SubGhzSceneDecodeRAW);
    /* Decode-only protocol (no encoder) reached by decoding a RAW file: let the
     * user replay the original RAW capture (file-based raw transmit). */
    bool info_raw_available =
        (!info_transmittable) && from_decode_raw && !furi_string_empty(subghz->file_path);

    scene_manager_set_scene_state(
        subghz->scene_manager,
        SubGhzSceneReceiverInfo,
        info_needs_decrypt ? 1 : (info_raw_available ? 2 : 0));

    bool has_saved_match = false;
    if(is_serializable) {
        FuriString* matched_name = furi_string_alloc();
        FuriString* matched_path = furi_string_alloc();
        has_saved_match = subghz_saved_match_signal(info_rx_ff, matched_name, matched_path);
        furi_string_free(matched_name);
        furi_string_free(matched_path);
    }

    subghz_receiver_info_bar_reset(subghz->receiver_info_bar);
    uint8_t idx = 0;
    uint8_t primary = 0;
    bool have_primary = false;

    if(info_raw_capable) {
        subghz_receiver_info_bar_add_button(
            subghz->receiver_info_bar, "Raw", false,
            SubGhzCustomEventSceneReceiverInfoTxFull, 0);
        idx++;
    }
    if(info_needs_decrypt) {
        subghz_receiver_info_bar_add_button(
            subghz->receiver_info_bar, subghz_scene_receiver_info_decrypt_label(subghz), false,
            SubGhzCustomEventSceneReceiverInfoDecrypt, 0);
        primary = idx;
        have_primary = true;
        idx++;
    } else if(info_transmittable) {
        subghz_receiver_info_bar_add_button(
            subghz->receiver_info_bar, "Send", false,
            SubGhzCustomEventSceneReceiverInfoSend, 0);
        primary = idx;
        have_primary = true;
        idx++;
    } else if(info_raw_available) {
        subghz_receiver_info_bar_add_button(
            subghz->receiver_info_bar, "Send", false,
            SubGhzCustomEventSceneReceiverInfoSendRaw, 0);
        primary = idx;
        have_primary = true;
        idx++;
    }
    if(is_serializable) {
        subghz_receiver_info_bar_add_button(
            subghz->receiver_info_bar,
            (has_saved_match && !from_decode_raw) ? "Update" : "Save", false,
            SubGhzCustomEventSceneReceiverInfoSave, 0);
        if(!have_primary) {
            primary = idx;
            have_primary = true;
        }
        idx++;
    }
    UNUSED(idx);

    subghz_receiver_info_bar_set_info(
        subghz->receiver_info_bar,
        furi_string_get_cstr(text),
        furi_string_get_cstr(frequency_str),
        furi_string_get_cstr(modulation_str));
    subghz_receiver_info_bar_set_focus(
        subghz->receiver_info_bar, subghz->idx_menu_chosen, primary);
    subghz_receiver_info_bar_set_callback(
        subghz->receiver_info_bar, subghz_scene_receiver_info_bar_callback, subghz);

    furi_string_free(frequency_str);
    furi_string_free(modulation_str);
    furi_string_free(text);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReceiverInfoBar);
    return true;
}

static void subghz_scene_receiver_info_redraw(SubGhz* subghz) {
    if(!(fox_theme_is_active() && subghz_scene_receiver_info_draw_bar(subghz))) {
        subghz_scene_receiver_info_draw_widget(subghz);
    }
}

void subghz_scene_receiver_info_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz_custom_btns_reset();

    subghz_receiver_info_bar_scroll_reset(subghz->receiver_info_bar);

    subghz_scene_receiver_info_redraw(subghz);

    subghz_receiver_info_bar_resume_scroll(subghz->receiver_info_bar);

    if(!subghz_history_get_text_space_left(subghz->history, NULL) &&
       !scene_manager_has_previous_scene(subghz->scene_manager, SubGhzSceneDecodeRAW)) {
        subghz->state_notifications = SubGhzNotificationStateRx;
    }
}

bool subghz_scene_receiver_info_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneReceiverInfoTxStart) {
            if(!subghz_scene_receiver_info_update_parser(subghz)) {
                return false;
            }

            subghz_txrx_hopper_pause(subghz->txrx);

            subghz->state_notifications = SubGhzNotificationStateTx;
            subghz_block_generic_global.endless_tx = true;
            if(!subghz_tx_start(
                   subghz,
                   subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen))) {
                subghz_txrx_rx_start(subghz->txrx);
                subghz_txrx_hopper_unpause(subghz->txrx);
                subghz->state_notifications = SubGhzNotificationStateRx;
                subghz_block_generic_global.endless_tx = false;
                return true;
            }
        } else if(event.event == SubGhzCustomEventSceneReceiverInfoTxStop) {

            subghz->state_notifications = SubGhzNotificationStateTxWait;
            subghz_block_generic_global.endless_tx = false;

            return true;
        } else if(event.event == SubGhzCustomEventSceneReceiverInfoSave) {

            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);

            subghz_txrx_stop(subghz->txrx);
            if(!subghz_scene_receiver_info_update_parser(subghz)) {
                return false;
            }

            if(subghz_txrx_protocol_is_serializable(subghz->txrx)) {
                FlipperFormat* rx_ff =
                    subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
                FuriString* matched_name = furi_string_alloc();
                FuriString* matched_path = furi_string_alloc();
                bool has_saved_match =
                    subghz_saved_match_signal(rx_ff, matched_name, matched_path);

                bool from_decode_raw = scene_manager_has_previous_scene(
                    subghz->scene_manager, SubGhzSceneDecodeRAW);

                if(has_saved_match && !from_decode_raw) {

                    bool updated = subghz_save_protocol_to_file(
                        subghz, rx_ff, furi_string_get_cstr(matched_path));
                    if(updated) {
                        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveSuccess);
                    } else {
                        furi_string_set(subghz->error_str, "Cannot update\nsaved file");
                        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
                    }
                } else {
                    subghz_file_name_clear(subghz);

                    subghz->save_datetime =
                        subghz_history_get_datetime(subghz->history, subghz->idx_menu_chosen);
                    subghz->save_datetime_set = true;
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
                }

                furi_string_free(matched_name);
                furi_string_free(matched_path);
            }
            return true;
        } else if(event.event == SubGhzCustomEventSceneReceiverInfoTxFull) {

            if(!subghz_scene_receiver_info_update_parser(subghz)) {
                return false;
            }

            FlipperFormat* fff_history =
                subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
            FlipperFormat* fff_data = subghz_txrx_get_fff_data(subghz->txrx);

            Stream* src = flipper_format_get_raw_stream(fff_history);
            Stream* dst = flipper_format_get_raw_stream(fff_data);

            stream_seek(src, 0, StreamOffsetFromStart);
            stream_clean(dst);
            stream_copy_full(src, dst);
            stream_seek(dst, 0, StreamOffsetFromStart);

            uint32_t txraw_full = 1;
            flipper_format_insert_or_update_uint32(fff_data, "TxRaw", &txraw_full, 1);
            flipper_format_rewind(fff_data);

            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
            return true;
        } else if(event.event == SubGhzCustomEventSceneReceiverInfoSend) {

            if(!subghz_scene_receiver_info_update_parser(subghz)) {
                return false;
            }

            FlipperFormat* send_history =
                subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
            FlipperFormat* send_data = subghz_txrx_get_fff_data(subghz->txrx);

            Stream* send_src = flipper_format_get_raw_stream(send_history);
            Stream* send_dst = flipper_format_get_raw_stream(send_data);

            stream_seek(send_src, 0, StreamOffsetFromStart);
            stream_clean(send_dst);
            stream_copy_full(send_src, send_dst);
            stream_seek(send_dst, 0, StreamOffsetFromStart);

            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
            return true;
        } else if(event.event == SubGhzCustomEventSceneReceiverInfoSendRaw) {

            /* Decode-only protocol (encoder == NULL): the decoded fields cannot be
             * re-encoded, so replay the original RAW capture through the file-based
             * raw transmit (Protocol "RAW"), the same path used for RAW .sub files.
             * subghz->file_path still points at the decoded RAW file (we got here
             * from the DecodeRAW flow). */
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
            subghz_txrx_stop(subghz->txrx);

            if(furi_string_empty(subghz->file_path)) {
                return false;
            }
            const char* rn_sendraw =
                subghz_history_get_protocol_name(subghz->history, subghz->idx_menu_chosen);
            if(subghz_key_load(subghz, furi_string_get_cstr(subghz->file_path), false)) {
                if(subghz_get_load_type_file(subghz) == SubGhzLoadTypeFileRaw) {
                    furi_string_set(
                        subghz->raw_send_title,
                        (rn_sendraw && rn_sendraw[0]) ? rn_sendraw : "RAW");
                    scene_manager_set_scene_state(
                        subghz->scene_manager, SubGhzSceneTransmitter, 1);
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
                }
            }
            return true;
        } else if(event.event == SubGhzCustomEventSceneReceiverInfoDecrypt) {

            if(!subghz_scene_receiver_info_update_parser(subghz)) {
                return false;
            }

            FlipperFormat* dec_history =
                subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
            FlipperFormat* dec_data = subghz_txrx_get_fff_data(subghz->txrx);

            Stream* dsrc = flipper_format_get_raw_stream(dec_history);
            Stream* ddst = flipper_format_get_raw_stream(dec_data);
            stream_seek(dsrc, 0, StreamOffsetFromStart);
            stream_clean(ddst);
            stream_copy_full(dsrc, ddst);
            stream_seek(ddst, 0, StreamOffsetFromStart);

            char dec_name_buf[SUBGHZ_MAX_LEN_NAME] = {0};
            const char* dec_pname =
                subghz_history_get_protocol_name(subghz->history, subghz->idx_menu_chosen);
            name_generator_make_auto_datetime(
                dec_name_buf, SUBGHZ_MAX_LEN_NAME, dec_pname, NULL);

            furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
            furi_string_cat_printf(
                subghz->file_path, "/%s%s", dec_name_buf, SUBGHZ_APP_FILENAME_EXTENSION);

            subghz_save_protocol_to_file(
                subghz, dec_data, furi_string_get_cstr(subghz->file_path));

            {
                SubGhzRecoveryAction act = subghz_recovery_primary_action(dec_history);
                if(act == SubGhzRecoveryActionHitag) {
                    subghz->state_notifications = SubGhzNotificationStateIDLE;
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFiatV1KeyMethod);
                    return true;
                } else if(act == SubGhzRecoveryActionSeedBf) {
                    subghz->state_notifications = SubGhzNotificationStateIDLE;
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSeedBf);
                    return true;
                } else if(act == SubGhzRecoveryActionKeeloq) {
                    subghz->state_notifications = SubGhzNotificationStateIDLE;
                    FuriString* kl_arg = furi_string_alloc_printf(
                        "core:menu:keeloq_bf|%s", furi_string_get_cstr(subghz->file_path));
                    subghz_scene_start_launch_and_exit(
                        subghz, SUBGHZ_KEELOQ_TOOL_FAP_PATH, furi_string_get_cstr(kl_arg), true);
                    furi_string_free(kl_arg);
                    return true;
                }
            }

            snprintf(
                subghz_scene_receiver_info_psa_popup_text,
                sizeof(subghz_scene_receiver_info_psa_popup_text),
                "File saved as:\n%s%s",
                dec_name_buf,
                SUBGHZ_APP_FILENAME_EXTENSION);

            subghz->state_notifications = SubGhzNotificationStateIDLE;

            popup_set_text(
                subghz->popup,
                subghz_scene_receiver_info_psa_popup_text,
                64,
                32,
                AlignCenter,
                AlignCenter);
            popup_set_timeout(subghz->popup, 2000);
            popup_set_context(subghz->popup, subghz);
            popup_set_callback(subghz->popup, subghz_scene_receiver_info_psa_popup_callback);
            popup_enable_timeout(subghz->popup);
            view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPopup);
            return true;
        } else if(event.event == SubGhzCustomEventSceneReceiverInfoPsaDecryptGo) {

            popup_reset(subghz->popup);
            scene_manager_next_scene(subghz->scene_manager, SubGhzScenePsaDecrypt);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF) {
            subghz_txrx_hopper_update(subghz->txrx, subghz->last_settings->hopping_threshold);
        }
        switch(subghz->state_notifications) {
        case SubGhzNotificationStateTx:
            notification_message(subghz->notifications, &sequence_blink_magenta_10);
            break;
        case SubGhzNotificationStateRx:
            notification_message(subghz->notifications, &sequence_blink_cyan_10);
            break;
        case SubGhzNotificationStateRxDone:
            notification_message(subghz->notifications, &sequence_blink_green_100);
            subghz->state_notifications = SubGhzNotificationStateRx;
            break;
        case SubGhzNotificationStateTxWait:

            if(!subghz_devices_is_async_complete_tx(subghz->txrx->radio_device)) {
                notification_message(subghz->notifications, &sequence_blink_magenta_10);
            } else {
                subghz_txrx_stop(subghz->txrx);

                widget_reset(subghz->widget);
                subghz_scene_receiver_info_redraw(subghz);

                subghz->state_notifications = SubGhzNotificationStateIDLE;

                if(!scene_manager_has_previous_scene(subghz->scene_manager, SubGhzSceneDecodeRAW)) {
                    subghz_txrx_rx_start(subghz->txrx);
                    subghz_txrx_hopper_unpause(subghz->txrx);
                    if(!subghz_history_get_text_space_left(subghz->history, NULL)) {
                        subghz->state_notifications = SubGhzNotificationStateRx;
                    }
                }
            }
            break;
        default:
            break;
        }
    }
    return false;
}

void subghz_scene_receiver_info_on_exit(void* context) {
    SubGhz* subghz = context;

    subghz_receiver_info_bar_pause_scroll(subghz->receiver_info_bar);
    widget_reset(subghz->widget);
    popup_reset(subghz->popup);
    subghz_txrx_reset_dynamic_and_custom_btns(subghz->txrx);
}
