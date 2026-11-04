#include "../keeloq_tool_i.h"
#include <lib/subghz/protocols/keeloq.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/types.h>
#include <flipper_format/flipper_format.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <toolbox/path.h>

enum {
    KlBf2IndexLoadSig1,
    KlBf2IndexLoadSig2,
    KlBf2IndexType,
    KlBf2IndexStartBf,
};

static const char* kl_bf2_type_labels[] = {
    "Type: Auto (6>7>8)",
    "Type: 6 (Serial 1)",
    "Type: 7 (Serial 2)",
    "Type: 8 (Serial 3)",
};
static const uint8_t kl_bf2_type_values[] = {0, 6, 7, 8};

static bool kl_bf2_read_key_fff(FlipperFormat* fff, uint32_t* out_fix, uint32_t* out_hop) {
    flipper_format_rewind(fff);
    uint8_t key_data[8] = {0};
    if(!flipper_format_read_hex(fff, "Key", key_data, 8)) return false;
    uint64_t raw = 0;
    for(uint8_t i = 0; i < 8; i++) {
        raw = (raw << 8) | key_data[i];
    }
    uint64_t reversed = subghz_protocol_blocks_reverse_key(raw, 64);
    *out_fix = (uint32_t)(reversed >> 32);
    *out_hop = (uint32_t)(reversed & 0xFFFFFFFF);
    return true;
}

static FlipperFormat* kl_bf2_open_signal(KeeloqTool* app) {
    FlipperFormat* fff = flipper_format_file_alloc(app->storage);
    if(!flipper_format_file_open_existing(fff, furi_string_get_cstr(app->signal_path))) {
        flipper_format_free(fff);
        return NULL;
    }
    return fff;
}

static bool kl_bf2_extract_key(KeeloqTool* app, uint32_t* out_fix, uint32_t* out_hop) {
    FlipperFormat* fff = kl_bf2_open_signal(app);
    if(!fff) return false;
    bool ok = kl_bf2_read_key_fff(fff, out_fix, out_hop);
    flipper_format_free(fff);
    return ok;
}

#define KL_BF2_MAX_SCAN_FILES 128U

// Auto-find sig2: scan sig1's folder for another KeeLoq .sub from the SAME remote
// (same 28-bit serial, different hop) and load it as sig2 with no prompt -- the same
// convenience Hitag2Hell has. Requires sig1 already loaded with serial/hop1 populated.
static void kl_bf2_scan_for_sig2(KeeloqTool* app) {
    if(!app->keeloq_bf2.sig1_loaded || app->keeloq_bf2.sig2_loaded) return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* dir_path = furi_string_alloc();
    FuriString* main_name = furi_string_alloc();
    path_extract_dirname(furi_string_get_cstr(app->keeloq_bf2.sig1_path), dir_path);
    path_extract_filename_no_ext(
        furi_string_get_cstr(app->keeloq_bf2.sig1_path), main_name);

    File* dir = storage_file_alloc(storage);
    if(storage_dir_open(dir, furi_string_get_cstr(dir_path))) {
        FileInfo info;
        char name_buf[128];
        uint32_t scanned = 0;
        while(!app->keeloq_bf2.sig2_loaded && scanned < KL_BF2_MAX_SCAN_FILES &&
              storage_dir_read(dir, &info, name_buf, sizeof(name_buf))) {
            scanned++;
            if(info.flags & FSF_DIRECTORY) continue;
            size_t nlen = strlen(name_buf);
            if(nlen < 5 || strcmp(name_buf + nlen - 4, ".sub") != 0) continue;

            FuriString* cand = furi_string_alloc_set_str(name_buf);
            path_extract_filename(cand, cand, true);
            bool same = furi_string_equal(cand, main_name);
            furi_string_free(cand);
            if(same) continue;

            FuriString* full = furi_string_alloc_printf(
                "%s/%s", furi_string_get_cstr(dir_path), name_buf);
            FlipperFormat* fff = flipper_format_file_alloc(storage);
            if(flipper_format_file_open_existing(fff, furi_string_get_cstr(full))) {
                FuriString* proto = furi_string_alloc();
                flipper_format_rewind(fff);
                bool is_kl = flipper_format_read_string(fff, "Protocol", proto) &&
                             furi_string_equal_str(proto, "KeeLoq");
                furi_string_free(proto);
                uint32_t fix2 = 0, hop2 = 0;
                if(is_kl && kl_bf2_read_key_fff(fff, &fix2, &hop2)) {
                    if((fix2 & 0x0FFFFFFF) == app->keeloq_bf2.serial &&
                       hop2 != app->keeloq_bf2.hop1) {
                        app->keeloq_bf2.hop2 = hop2;
                        app->keeloq_bf2.sig2_loaded = true;
                        furi_string_set(app->keeloq_bf2.sig2_path, full);
                    }
                }
            }
            flipper_format_free(fff);
            furi_string_free(full);
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
    furi_string_free(main_name);
    furi_string_free(dir_path);
    furi_record_close(RECORD_STORAGE);
}

static bool kl_bf2_is_keeloq(KeeloqTool* app) {
    FlipperFormat* fff = kl_bf2_open_signal(app);
    if(!fff) return false;
    flipper_format_rewind(fff);
    FuriString* proto = furi_string_alloc();
    bool ok = flipper_format_read_string(fff, "Protocol", proto) &&
              furi_string_equal_str(proto, "KeeLoq");
    furi_string_free(proto);
    flipper_format_free(fff);
    return ok;
}

static void kl_bf2_submenu_callback(void* context, uint32_t index) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static bool kl_bf2_load_signal(KeeloqTool* app, FuriString* out_path) {
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(
        &browser_options, SUBGHZ_APP_FILENAME_EXTENSION, NULL);
    browser_options.base_path = SUBGHZ_APP_FOLDER;

    FuriString* selected = furi_string_alloc();
    furi_string_set(selected, SUBGHZ_APP_FOLDER);

    bool res = dialog_file_browser_show(app->dialogs, selected, selected, &browser_options);

    if(res) {
        furi_string_set(app->signal_path, selected);
        furi_string_set(out_path, selected);
    }

    furi_string_free(selected);
    return res;
}

static void kl_bf2_rebuild_menu(KeeloqTool* app) {
    submenu_reset(app->submenu);

    char label1[64];
    char label2[64];

    if(app->keeloq_bf2.sig1_loaded) {
        FuriString* name = furi_string_alloc();
        path_extract_filename(app->keeloq_bf2.sig1_path, name, true);
        snprintf(label1, sizeof(label1), "Sig 1: %s", furi_string_get_cstr(name));
        furi_string_free(name);
    } else {
        snprintf(label1, sizeof(label1), "Load Signal 1");
    }

    if(app->keeloq_bf2.sig2_loaded) {
        FuriString* name = furi_string_alloc();
        path_extract_filename(app->keeloq_bf2.sig2_path, name, true);
        snprintf(label2, sizeof(label2), "Sig 2: %s", furi_string_get_cstr(name));
        furi_string_free(name);
    } else {
        snprintf(label2, sizeof(label2), "Load Signal 2");
    }

    submenu_add_item(
        app->submenu, label1, KlBf2IndexLoadSig1,
        kl_bf2_submenu_callback, app);
    submenu_add_item(
        app->submenu, label2, KlBf2IndexLoadSig2,
        kl_bf2_submenu_callback, app);

    int type_idx = 0;
    for(int i = 0; i < 4; i++) {
        if(kl_bf2_type_values[i] == app->keeloq_bf2.learn_type) {
            type_idx = i;
            break;
        }
    }
    submenu_add_item(
        app->submenu, kl_bf2_type_labels[type_idx], KlBf2IndexType,
        kl_bf2_submenu_callback, app);

    if(app->keeloq_bf2.sig1_loaded && app->keeloq_bf2.sig2_loaded) {
        submenu_add_item(
            app->submenu, "Start BF", KlBf2IndexStartBf,
            kl_bf2_submenu_callback, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewSubmenu);
}

void keeloq_tool_scene_bf2_on_enter(void* context) {
    KeeloqTool* app = context;

    bool prefilled =
        scene_manager_get_scene_state(app->scene_manager, KeeloqToolSceneBf2) == 1;
    scene_manager_set_scene_state(app->scene_manager, KeeloqToolSceneBf2, 0);

    if(!prefilled) {
        app->keeloq_bf2.sig1_loaded = false;
    } else {
        /* Entered from the saved menu: sig1_path/sig1_loaded were set there, but the loaded
         * file's serial/fix/hop1 were NOT derived. Do it now from the currently-loaded signal
         * (which IS sig1) so the sig2 same-remote check compares against a real serial instead
         * of a stale one -- otherwise every sig2 falsely reports "Serial mismatch". */
        uint32_t fix = 0, hop = 0;
        if(kl_bf2_extract_key(app, &fix, &hop)) {
            app->keeloq_bf2.fix = fix;
            app->keeloq_bf2.hop1 = hop;
            app->keeloq_bf2.serial = fix & 0x0FFFFFFF;
            // Auto-load a same-remote 2nd capture from the folder, like Hitag2Hell.
            kl_bf2_scan_for_sig2(app);
        }
    }
    app->keeloq_bf2.sig2_loaded = false;
    app->keeloq_bf2.learn_type = 0;

    kl_bf2_rebuild_menu(app);
}

bool keeloq_tool_scene_bf2_on_event(void* context, SceneManagerEvent event) {
    KeeloqTool* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KlBf2IndexLoadSig1) {
            FuriString* path = furi_string_alloc();
            if(kl_bf2_load_signal(app, path)) {
                if(!kl_bf2_is_keeloq(app)) {
                    dialog_message_show_storage_error(
                        app->dialogs, "Not a KeeLoq\nprotocol file");
                    furi_string_free(path);
                    kl_bf2_rebuild_menu(app);
                    return true;
                }

                uint32_t fix, hop;
                if(!kl_bf2_extract_key(app, &fix, &hop)) {
                    dialog_message_show_storage_error(
                        app->dialogs, "Cannot read Key\nfrom file");
                    furi_string_free(path);
                    kl_bf2_rebuild_menu(app);
                    return true;
                }

                app->keeloq_bf2.fix = fix;
                app->keeloq_bf2.hop1 = hop;
                app->keeloq_bf2.serial = fix & 0x0FFFFFFF;
                app->keeloq_bf2.sig1_loaded = true;
                furi_string_set(app->keeloq_bf2.sig1_path, path);

                app->keeloq_bf2.sig2_loaded = false;
            }
            furi_string_free(path);
            kl_bf2_rebuild_menu(app);
            return true;

        } else if(event.event == KlBf2IndexLoadSig2) {
            if(!app->keeloq_bf2.sig1_loaded) {
                dialog_message_show_storage_error(
                    app->dialogs, "Load Signal 1 first");
                kl_bf2_rebuild_menu(app);
                return true;
            }

            FuriString* path = furi_string_alloc();
            if(kl_bf2_load_signal(app, path)) {
                if(!kl_bf2_is_keeloq(app)) {
                    dialog_message_show_storage_error(
                        app->dialogs, "Not a KeeLoq\nprotocol file");
                    furi_string_free(path);
                    kl_bf2_rebuild_menu(app);
                    return true;
                }

                uint32_t fix2, hop2;
                if(!kl_bf2_extract_key(app, &fix2, &hop2)) {
                    dialog_message_show_storage_error(
                        app->dialogs, "Cannot read Key\nfrom file");
                    furi_string_free(path);
                    kl_bf2_rebuild_menu(app);
                    return true;
                }

                uint32_t serial2 = fix2 & 0x0FFFFFFF;
                if(serial2 != app->keeloq_bf2.serial) {
                    dialog_message_show_storage_error(
                        app->dialogs, "Serial mismatch!\nMust be same remote");
                    furi_string_free(path);
                    kl_bf2_rebuild_menu(app);
                    return true;
                }

                if(hop2 == app->keeloq_bf2.hop1) {
                    dialog_message_show_storage_error(
                        app->dialogs, "Same hop code!\nUse a different\ncapture");
                    furi_string_free(path);
                    kl_bf2_rebuild_menu(app);
                    return true;
                }

                app->keeloq_bf2.hop2 = hop2;
                app->keeloq_bf2.sig2_loaded = true;
                furi_string_set(app->keeloq_bf2.sig2_path, path);
            }
            furi_string_free(path);
            kl_bf2_rebuild_menu(app);
            return true;

        } else if(event.event == KlBf2IndexType) {
            uint8_t cur = app->keeloq_bf2.learn_type;
            if(cur == 0) cur = 6;
            else if(cur == 6) cur = 7;
            else if(cur == 7) cur = 8;
            else cur = 0;
            app->keeloq_bf2.learn_type = cur;
            kl_bf2_rebuild_menu(app);
            return true;

        } else if(event.event == KlBf2IndexStartBf) {
            if(!app->keeloq_bf2.sig1_loaded || !app->keeloq_bf2.sig2_loaded) {
                return true;
            }

            furi_string_set(app->signal_path, app->keeloq_bf2.sig1_path);
            furi_string_set(app->file_path, app->keeloq_bf2.sig1_path);

            scene_manager_next_scene(app->scene_manager, KeeloqToolSceneDecrypt);
            return true;
        }
    }
    return false;
}

void keeloq_tool_scene_bf2_on_exit(void* context) {
    KeeloqTool* app = context;
    submenu_reset(app->submenu);
}
