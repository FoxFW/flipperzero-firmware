#include "../subghz_i.h"

#define SUBGHZ_RAW_MATCH_SCAN_BUDGET_MS 60

typedef enum {
    SubGhzRawMatchModeList,
    SubGhzRawMatchModeScanning,
    SubGhzRawMatchModeHits,
} SubGhzRawMatchMode;

typedef enum {
    SubGhzRawMatchIndexTestAll = 0,
    SubGhzRawMatchIndexCandidate = 1,
    SubGhzRawMatchIndexHit = 32,
    SubGhzRawMatchIndexBackToList = 64,
} SubGhzRawMatchIndex;

static void subghz_scene_unknown_raw_match_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

static void subghz_scene_unknown_raw_match_build_list(SubGhz* subghz) {
    Submenu* submenu = subghz->submenu;
    SubGhzRawMatch* match = subghz->raw_match;
    char text[48];
    submenu_reset(submenu);
    snprintf(
        text,
        sizeof(text),
        "~%lu/%luus  %u edges",
        (unsigned long)subghz_raw_match_get_te_short(match),
        (unsigned long)subghz_raw_match_get_te_long(match),
        (unsigned)subghz_raw_match_get_edge_count(match));
    submenu_set_header(submenu, text);
    submenu_add_item(
        submenu,
        "Test all protocols",
        SubGhzRawMatchIndexTestAll,
        subghz_scene_unknown_raw_match_submenu_callback,
        subghz);
    for(size_t i = 0; i < subghz_raw_match_get_candidate_count(match); i++) {
        const SubGhzRawMatchCandidate* candidate = subghz_raw_match_get_candidate(match, i);
        snprintf(
            text,
            sizeof(text),
            "%s %u/%u",
            candidate->name,
            candidate->te_short,
            candidate->te_long);
        submenu_add_item(
            submenu,
            text,
            SubGhzRawMatchIndexCandidate + i,
            subghz_scene_unknown_raw_match_submenu_callback,
            subghz);
    }
}

static void subghz_scene_unknown_raw_match_hit_label(
    const SubGhzRawMatchHit* hit,
    char* text,
    size_t size) {
    snprintf(
        text,
        size,
        "%s x%u.%02u%s",
        hit->name,
        hit->scale_pct / 100,
        hit->scale_pct % 100,
        hit->inverted ? " inv" : "");
}

static void subghz_scene_unknown_raw_match_build_hits(SubGhz* subghz) {
    Submenu* submenu = subghz->submenu;
    SubGhzRawMatch* match = subghz->raw_match;
    char text[48];
    submenu_reset(submenu);
    snprintf(
        text,
        sizeof(text),
        "Decoded by %u protocol%s",
        (unsigned)subghz_raw_match_get_hit_count(match),
        subghz_raw_match_get_hit_count(match) == 1 ? "" : "s");
    submenu_set_header(submenu, text);
    for(size_t i = 0; i < subghz_raw_match_get_hit_count(match); i++) {
        subghz_scene_unknown_raw_match_hit_label(
            subghz_raw_match_get_hit(match, i), text, sizeof(text));
        submenu_add_item(
            submenu,
            text,
            SubGhzRawMatchIndexHit + i,
            subghz_scene_unknown_raw_match_submenu_callback,
            subghz);
    }
    submenu_add_item(
        submenu,
        "Closest by timing",
        SubGhzRawMatchIndexBackToList,
        subghz_scene_unknown_raw_match_submenu_callback,
        subghz);
}

static void subghz_scene_unknown_raw_match_show_menu(SubGhz* subghz) {
    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneUnknownRawMatch) ==
       SubGhzRawMatchModeHits) {
        subghz_scene_unknown_raw_match_build_hits(subghz);
    } else {
        subghz_scene_unknown_raw_match_build_list(subghz);
    }
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

static void subghz_scene_unknown_raw_match_progress(SubGhz* subghz) {
    SubGhzRawMatch* match = subghz->raw_match;
    static char text[64];
    snprintf(
        text,
        sizeof(text),
        "%u / %u\n%s\nFound: %u",
        (unsigned)subghz_raw_match_scan_position(match),
        (unsigned)subghz_raw_match_scan_total(match),
        subghz_raw_match_scan_current_name(match),
        (unsigned)subghz_raw_match_get_hit_count(match));
    popup_set_text(subghz->popup, text, 64, 22, AlignCenter, AlignTop);
}

static void subghz_scene_unknown_raw_match_no_decode(SubGhz* subghz, const char* text) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "No decode", 64, 3, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, NULL, "OK", NULL);
    dialog_message_show(subghz->dialogs, message);
    dialog_message_free(message);
}

static void subghz_scene_unknown_raw_match_open_hit(SubGhz* subghz, uint8_t hit) {
    subghz->raw_match_hit = hit;
    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneUnknownRawMatchResult);
}

void subghz_scene_unknown_raw_match_on_enter(void* context) {
    SubGhz* subghz = context;
    if(!subghz->raw_match) {
        FlipperFormat* raw = subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
        SubGhzRadioPreset* preset =
            subghz_history_get_radio_preset(subghz->history, subghz->idx_menu_chosen);
        subghz->raw_match =
            subghz_raw_match_alloc(subghz_txrx_get_environment(subghz->txrx), raw, preset);
        subghz_history_release_cache(subghz->history);
    }
    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneUnknownRawMatch) ==
       SubGhzRawMatchModeScanning) {
        scene_manager_set_scene_state(
            subghz->scene_manager, SubGhzSceneUnknownRawMatch, SubGhzRawMatchModeList);
    }
    subghz_scene_unknown_raw_match_show_menu(subghz);
}

bool subghz_scene_unknown_raw_match_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    SubGhzRawMatch* match = subghz->raw_match;
    uint32_t mode = scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneUnknownRawMatch);

    if(event.type == SceneManagerEventTypeBack) {
        if(mode != SubGhzRawMatchModeList) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneUnknownRawMatch, SubGhzRawMatchModeList);
            subghz_scene_unknown_raw_match_show_menu(subghz);
            return true;
        }
        return false;
    }

    if(event.type == SceneManagerEventTypeTick) {
        if(mode != SubGhzRawMatchModeScanning) return false;
        bool done = subghz_raw_match_scan_step(match, SUBGHZ_RAW_MATCH_SCAN_BUDGET_MS);
        subghz_scene_unknown_raw_match_progress(subghz);
        if(done) {
            if(subghz_raw_match_get_hit_count(match)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneUnknownRawMatch, SubGhzRawMatchModeHits);
            } else {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneUnknownRawMatch, SubGhzRawMatchModeList);
                subghz_scene_unknown_raw_match_no_decode(
                    subghz, "No protocol decoded\nthis signal at any\ntiming tried.");
            }
            subghz_scene_unknown_raw_match_show_menu(subghz);
        }
        return true;
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == SubGhzRawMatchIndexTestAll) {
        subghz_raw_match_scan_begin(match);
        scene_manager_set_scene_state(
            subghz->scene_manager, SubGhzSceneUnknownRawMatch, SubGhzRawMatchModeScanning);
        popup_reset(subghz->popup);
        popup_set_header(subghz->popup, "Testing protocols", 64, 6, AlignCenter, AlignTop);
        subghz_scene_unknown_raw_match_progress(subghz);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPopup);
        return true;
    }
    if(event.event == SubGhzRawMatchIndexBackToList) {
        scene_manager_set_scene_state(
            subghz->scene_manager, SubGhzSceneUnknownRawMatch, SubGhzRawMatchModeList);
        subghz_scene_unknown_raw_match_show_menu(subghz);
        return true;
    }
    if(event.event >= SubGhzRawMatchIndexHit && event.event < SubGhzRawMatchIndexBackToList) {
        subghz_scene_unknown_raw_match_open_hit(subghz, event.event - SubGhzRawMatchIndexHit);
        return true;
    }
    if(event.event >= SubGhzRawMatchIndexCandidate && event.event < SubGhzRawMatchIndexHit) {
        const SubGhzRawMatchCandidate* candidate =
            subghz_raw_match_get_candidate(match, event.event - SubGhzRawMatchIndexCandidate);
        if(!candidate) return true;
        popup_reset(subghz->popup);
        popup_set_header(subghz->popup, "Trying", 64, 16, AlignCenter, AlignTop);
        popup_set_text(subghz->popup, candidate->name, 64, 32, AlignCenter, AlignTop);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPopup);
        int16_t hit = subghz_raw_match_try_protocol(match, candidate->name);
        if(hit >= 0) {
            subghz_scene_unknown_raw_match_open_hit(subghz, (uint8_t)hit);
        } else {
            subghz_scene_unknown_raw_match_no_decode(
                subghz, "This protocol did not\ndecode the signal at\nany timing tried.");
            subghz_scene_unknown_raw_match_show_menu(subghz);
        }
        return true;
    }
    return false;
}

void subghz_scene_unknown_raw_match_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
    popup_reset(subghz->popup);
}
