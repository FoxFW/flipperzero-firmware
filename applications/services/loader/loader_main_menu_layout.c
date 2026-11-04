#include "loader_main_menu_layout.h"
#include "loader_main_menu_pins.h"

#include <furi.h>
#include <storage/storage.h>
#include <applications.h>
#include <flipper_application/flipper_application.h>
#include <string.h>

bool main_menu_layout_entry_is_protected(const MainMenuLayoutEntry* entry) {
    if(entry->kind == MainMenuEntryKindApplications) return true;
    if(entry->kind == MainMenuEntryKindInternal &&
       strcmp(entry->identifier, MAIN_MENU_SUBGHZ_NAME) == 0)
        return true;
    /* "Settings" is protected too: the main menu is the only path to it, so
     * hiding it would strand the user (Fox Settings can still be reached via the
     * Apps list, but plain Settings cannot). "Apps", "Sub-GHz" and "Settings"
     * are the three unhideable entries. */
    if(entry->kind == MainMenuEntryKindSettings) return true;
    return false;
}

const char* main_menu_layout_kind_to_token(MainMenuEntryKind kind) {
    switch(kind) {
    case MainMenuEntryKindApplications:
        return "app";
    case MainMenuEntryKindInternal:
        return "int";
    case MainMenuEntryKindExternal:
        return "ext";
    case MainMenuEntryKindUser:
        return "usr";
    case MainMenuEntryKindFoxSettings:
        return "fox";
    case MainMenuEntryKindSettings:
        return "sys";
    default:
        return "int";
    }
}

bool main_menu_layout_kind_from_token(const char* token, MainMenuEntryKind* out) {
    if(strcmp(token, "app") == 0) {
        *out = MainMenuEntryKindApplications;
    } else if(strcmp(token, "int") == 0) {
        *out = MainMenuEntryKindInternal;
    } else if(strcmp(token, "ext") == 0) {
        *out = MainMenuEntryKindExternal;
    } else if(strcmp(token, "usr") == 0) {
        *out = MainMenuEntryKindUser;
    } else if(strcmp(token, "fox") == 0) {
        *out = MainMenuEntryKindFoxSettings;
    } else if(strcmp(token, "sys") == 0) {
        *out = MainMenuEntryKindSettings;
    } else {
        return false;
    }
    return true;
}

static void main_menu_layout_derive_label(
    Storage* storage,
    const char* path,
    char* out,
    size_t out_size) {
    FuriString* path_str = furi_string_alloc_set_str(path);
    FuriString* name_str = furi_string_alloc();
    uint8_t icon_buf[FAP_MANIFEST_MAX_ICON_SIZE];
    uint8_t* icon_ptr = icon_buf;

    bool loaded = flipper_application_load_name_and_icon(path_str, storage, &icon_ptr, name_str);
    if(loaded && !furi_string_empty(name_str)) {
        strlcpy(out, furi_string_get_cstr(name_str), out_size);
    } else {
        const char* slash = strrchr(path, '/');
        const char* base = slash ? slash + 1 : path;
        strlcpy(out, base, out_size);
        size_t len = strlen(out);
        if(len > 4 && strcmp(out + len - 4, ".fap") == 0) {
            out[len - 4] = '\0';
        }
    }

    furi_string_free(path_str);
    furi_string_free(name_str);
}

static void main_menu_layout_append(
    MainMenuLayout* layout,
    MainMenuEntryKind kind,
    bool enabled,
    const char* identifier,
    const char* label) {
    if(layout->count >= MAIN_MENU_LAYOUT_MAX) return;
    MainMenuLayoutEntry* e = &layout->entries[layout->count];
    e->kind = kind;
    e->enabled = enabled;
    e->identifier[0] = '\0';
    if(identifier) strlcpy(e->identifier, identifier, sizeof(e->identifier));
    e->label[0] = '\0';
    if(label) strlcpy(e->label, label, sizeof(e->label));
    if(main_menu_layout_entry_is_protected(e)) e->enabled = true;
    layout->count++;
}

static void main_menu_layout_parse_line(const char* line, MainMenuLayout* layout) {
    if(line[0] == '\0' || line[0] == '#') return;
    if(layout->count >= MAIN_MENU_LAYOUT_MAX) return;

    const char* s1 = strchr(line, '|');
    if(!s1) return;
    const char* s2 = strchr(s1 + 1, '|');
    if(!s2) return;
    const char* s3 = strchr(s2 + 1, '|');

    char kind_token[8];
    size_t klen = (size_t)(s1 - line);
    if(klen >= sizeof(kind_token)) klen = sizeof(kind_token) - 1;
    memcpy(kind_token, line, klen);
    kind_token[klen] = '\0';

    MainMenuEntryKind kind;
    if(!main_menu_layout_kind_from_token(kind_token, &kind)) return;

    bool enabled = (s1 + 1 < s2) && (s1[1] == '1');

    const char* id_begin = s2 + 1;
    const char* id_end = s3 ? s3 : (line + strlen(line));

    MainMenuLayoutEntry* e = &layout->entries[layout->count];
    e->kind = kind;
    e->enabled = enabled;

    size_t idlen = (size_t)(id_end - id_begin);
    if(idlen >= sizeof(e->identifier)) idlen = sizeof(e->identifier) - 1;
    memcpy(e->identifier, id_begin, idlen);
    e->identifier[idlen] = '\0';

    e->label[0] = '\0';
    if(s3) {
        strlcpy(e->label, s3 + 1, sizeof(e->label));
    }

    if(main_menu_layout_entry_is_protected(e)) e->enabled = true;
    layout->count++;
}

static bool main_menu_layout_read_file(MainMenuLayout* layout, bool* existed) {
    layout->count = 0;
    bool any = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(existed) *existed = storage_file_exists(storage, MAIN_MENU_LAYOUT_FILE_NAME);
    File* file = storage_file_alloc(storage);

    if(storage_file_open(
           file, MAIN_MENU_LAYOUT_FILE_NAME, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[MAIN_MENU_LAYOUT_ID_LEN + MAIN_MENU_LAYOUT_LABEL_LEN + 16];
        size_t line_len = 0;
        uint8_t byte;

        while(layout->count < MAIN_MENU_LAYOUT_MAX && storage_file_read(file, &byte, 1) == 1) {
            if(byte == '\n' || byte == '\r') {
                if(line_len > 0) {
                    line[line_len] = '\0';
                    uint8_t before = layout->count;
                    main_menu_layout_parse_line(line, layout);
                    if(layout->count != before) any = true;
                    line_len = 0;
                }
            } else if(line_len < sizeof(line) - 1) {
                line[line_len++] = (char)byte;
            }
        }

        if(line_len > 0 && layout->count < MAIN_MENU_LAYOUT_MAX) {
            line[line_len] = '\0';
            uint8_t before = layout->count;
            main_menu_layout_parse_line(line, layout);
            if(layout->count != before) any = true;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return any;
}

void main_menu_layout_build_default(MainMenuLayout* layout) {
    layout->count = 0;

    /* Legacy internal-flash pins first (migration): they were user-pinned
     * external apps rendered at the top of the menu. Preserve that order. */
    MainMenuPins* pins = malloc(sizeof(MainMenuPins));
    if(pins) {
        main_menu_pins_load(pins);
        Storage* storage = furi_record_open(RECORD_STORAGE);
        for(uint8_t i = 0; i < pins->count; i++) {
            char label[MAIN_MENU_LAYOUT_LABEL_LEN];
            if(pins->names[i][0] != '\0') {
                strlcpy(label, pins->names[i], sizeof(label));
            } else {
                main_menu_layout_derive_label(storage, pins->paths[i], label, sizeof(label));
            }
            main_menu_layout_append(layout, MainMenuEntryKindUser, true, pins->paths[i], label);
        }
        furi_record_close(RECORD_STORAGE);
        free(pins);
    }

    /* "Apps" (the Applications list) — protected. */
    main_menu_layout_append(
        layout,
        MainMenuEntryKindApplications,
        true,
        MAIN_MENU_APPLICATIONS_NAME,
        MAIN_MENU_APPLICATIONS_NAME);

    /* Internal apps. */
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        main_menu_layout_append(
            layout, MainMenuEntryKindInternal, true, FLIPPER_APPS[i].name, FLIPPER_APPS[i].name);
    }

    /* External (built-in) apps. */
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        main_menu_layout_append(
            layout,
            MainMenuEntryKindExternal,
            true,
            FLIPPER_EXTERNAL_APPS[i].name,
            FLIPPER_EXTERNAL_APPS[i].name);
    }

    /* Fox Settings and Settings. */
    main_menu_layout_append(layout, MainMenuEntryKindFoxSettings, true, "", "Fox Settings");
    main_menu_layout_append(layout, MainMenuEntryKindSettings, true, "", "Settings");
}

void main_menu_layout_save(const MainMenuLayout* layout) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, MAIN_MENU_LAYOUT_DIR);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(
           file, MAIN_MENU_LAYOUT_FILE_NAME, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, MAIN_MENU_LAYOUT_HEADER, strlen(MAIN_MENU_LAYOUT_HEADER));
        storage_file_write(file, "\n", 1);
        for(uint8_t i = 0; i < layout->count; i++) {
            const MainMenuLayoutEntry* e = &layout->entries[i];
            const char* tok = main_menu_layout_kind_to_token(e->kind);
            storage_file_write(file, tok, strlen(tok));
            storage_file_write(file, "|", 1);
            storage_file_write(file, e->enabled ? "1" : "0", 1);
            storage_file_write(file, "|", 1);
            storage_file_write(file, e->identifier, strlen(e->identifier));
            storage_file_write(file, "|", 1);
            storage_file_write(file, e->label, strlen(e->label));
            storage_file_write(file, "\n", 1);
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

bool main_menu_layout_load(MainMenuLayout* layout) {
    bool existed = false;
    if(main_menu_layout_read_file(layout, &existed)) {
        return true; /* parsed at least one entry */
    }
    if(!existed) {
        /* Genuinely absent (first boot after update, or "Reset to Defaults"):
         * generate the current build's defaults, migrate legacy pins and persist
         * to the SD card. */
        main_menu_layout_build_default(layout);
        main_menu_layout_save(layout);
        return true;
    }
    /* The file exists but could not be read/parsed right now (a transient SD
     * error, or a corrupt/empty file). Do NOT overwrite it with defaults; report
     * failure so the caller keeps whatever menu it already has. */
    return false;
}

MainMenuLayoutFpStatus main_menu_layout_fingerprint(uint32_t* hash, uint32_t* len) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    MainMenuLayoutFpStatus status;
    if(!storage_file_exists(storage, MAIN_MENU_LAYOUT_FILE_NAME)) {
        status = MainMenuLayoutFpAbsent;
    } else {
        File* file = storage_file_alloc(storage);
        if(storage_file_open(
               file, MAIN_MENU_LAYOUT_FILE_NAME, FSAM_READ, FSOM_OPEN_EXISTING)) {
            uint32_t h = 2166136261u; /* FNV-1a 32-bit offset basis */
            uint32_t l = 0;
            uint8_t buf[64];
            size_t n;
            while((n = storage_file_read(file, buf, sizeof(buf))) > 0) {
                for(size_t i = 0; i < n; i++) {
                    h ^= buf[i];
                    h *= 16777619u; /* FNV-1a 32-bit prime */
                }
                l += (uint32_t)n;
            }
            if(hash) *hash = h;
            if(len) *len = l;
            status = MainMenuLayoutFpOk;
        } else {
            status = MainMenuLayoutFpError;
        }
        storage_file_close(file);
        storage_file_free(file);
    }

    furi_record_close(RECORD_STORAGE);
    return status;
}
