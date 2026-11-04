#include "../subghz_i.h"

static void subghz_scene_unknown_raw_match_result_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    SubGhz* subghz = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, result);
    }
}

void subghz_scene_unknown_raw_match_result_on_enter(void* context) {
    SubGhz* subghz = context;
    Widget* widget = subghz->widget;
    const SubGhzRawMatchHit* hit =
        subghz->raw_match ? subghz_raw_match_get_hit(subghz->raw_match, subghz->raw_match_hit) :
                            NULL;
    widget_reset(widget);
    if(!hit) {
        widget_add_string_element(
            widget, 64, 32, AlignCenter, AlignCenter, FontSecondary, "Result unavailable");
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
        return;
    }

    char line[48];
    if(hit->scale_pct == 100 && !hit->inverted) {
        snprintf(line, sizeof(line), "%s", "Timing as captured");
    } else {
        snprintf(
            line,
            sizeof(line),
            "Timing x%u.%02u%s",
            hit->scale_pct / 100,
            hit->scale_pct % 100,
            hit->inverted ? ", inverted" : "");
    }
    FuriString* text = furi_string_alloc();
    subghz_raw_match_get_hit_string(subghz->raw_match, subghz->raw_match_hit, text);

    widget_add_string_element(widget, 64, 0, AlignCenter, AlignTop, FontPrimary, hit->name);
    widget_add_string_element(widget, 64, 11, AlignCenter, AlignTop, FontSecondary, line);
    widget_add_text_scroll_element(widget, 0, 21, 128, 26, furi_string_get_cstr(text));
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Back", subghz_scene_unknown_raw_match_result_callback, subghz);
    widget_add_button_element(
        widget, GuiButtonTypeRight, "Keep", subghz_scene_unknown_raw_match_result_callback, subghz);
    furi_string_free(text);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_unknown_raw_match_result_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == GuiButtonTypeLeft) {
        scene_manager_previous_scene(subghz->scene_manager);
        return true;
    }
    if(event.event == GuiButtonTypeRight && subghz->raw_match) {
        const SubGhzRawMatchHit* hit =
            subghz_raw_match_get_hit(subghz->raw_match, subghz->raw_match_hit);
        uint8_t type = hit ? hit->type : 0;
        FlipperFormat* ff = subghz_raw_match_take_hit_ff(subghz->raw_match, subghz->raw_match_hit);
        if(ff) {
            if(!subghz_history_replace_item(subghz->history, subghz->idx_menu_chosen, ff, type)) {
                flipper_format_free(ff);
            }
        }
        subghz_raw_match_free(subghz->raw_match);
        subghz->raw_match = NULL;
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneUnknownRawMatch, 0);
        scene_manager_search_and_switch_to_previous_scene(
            subghz->scene_manager, SubGhzSceneReceiver);
        return true;
    }
    return false;
}

void subghz_scene_unknown_raw_match_result_on_exit(void* context) {
    SubGhz* subghz = context;
    widget_reset(subghz->widget);
}
