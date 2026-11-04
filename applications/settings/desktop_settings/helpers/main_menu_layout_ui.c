#include "main_menu_layout_ui.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

#define MM_LAYOUT_DIR    EXT_PATH("apps_data/fox")
#define MM_LAYOUT_FILE   EXT_PATH("apps_data/fox/main_menu.layout")
#define MM_LAYOUT_HEADER "# FoxFW main menu layout v1"

bool mm_layout_entry_protected(const MmEntry* e) {
    if(e->kind == MmKindApplications) return true;
    if(e->kind == MmKindInternal && strcmp(e->identifier, MM_SUBGHZ_NAME) == 0) return true;
    /* "Settings" is protected too — the main menu is the only path to it. Keep
     * this in sync with main_menu_layout_entry_is_protected() in the firmware. */
    if(e->kind == MmKindSettings) return true;
    return false;
}

bool mm_layout_entry_deletable(const MmEntry* e) {
    return e->kind == MmKindUser;
}

const char* mm_layout_entry_display(const MmEntry* e) {
    if(e->label[0] != '\0') return e->label;
    switch(e->kind) {
    case MmKindApplications:
        return MM_APPLICATIONS_NAME;
    case MmKindFoxSettings:
        return "Fox Settings";
    case MmKindSettings:
        return "Settings";
    default:
        return e->identifier;
    }
}

static const char* mm_kind_to_token(MmKind kind) {
    switch(kind) {
    case MmKindApplications:
        return "app";
    case MmKindInternal:
        return "int";
    case MmKindExternal:
        return "ext";
    case MmKindUser:
        return "usr";
    case MmKindFoxSettings:
        return "fox";
    case MmKindSettings:
        return "sys";
    default:
        return "int";
    }
}

static bool mm_kind_from_token(const char* token, MmKind* out) {
    if(strcmp(token, "app") == 0) {
        *out = MmKindApplications;
    } else if(strcmp(token, "int") == 0) {
        *out = MmKindInternal;
    } else if(strcmp(token, "ext") == 0) {
        *out = MmKindExternal;
    } else if(strcmp(token, "usr") == 0) {
        *out = MmKindUser;
    } else if(strcmp(token, "fox") == 0) {
        *out = MmKindFoxSettings;
    } else if(strcmp(token, "sys") == 0) {
        *out = MmKindSettings;
    } else {
        return false;
    }
    return true;
}

static void mm_append(
    MmLayout* layout,
    MmKind kind,
    bool enabled,
    const char* identifier,
    const char* label) {
    if(layout->count >= MM_LAYOUT_MAX) return;
    MmEntry* e = &layout->entries[layout->count];
    e->kind = kind;
    e->enabled = enabled;
    e->identifier[0] = '\0';
    if(identifier) strlcpy(e->identifier, identifier, sizeof(e->identifier));
    e->label[0] = '\0';
    if(label) strlcpy(e->label, label, sizeof(e->label));
    if(mm_layout_entry_protected(e)) e->enabled = true;
    layout->count++;
}

static void mm_parse_line(const char* line, MmLayout* layout) {
    if(line[0] == '\0' || line[0] == '#') return;
    if(layout->count >= MM_LAYOUT_MAX) return;

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

    MmKind kind;
    if(!mm_kind_from_token(kind_token, &kind)) return;

    bool enabled = (s1 + 1 < s2) && (s1[1] == '1');

    const char* id_begin = s2 + 1;
    const char* id_end = s3 ? s3 : (line + strlen(line));

    MmEntry* e = &layout->entries[layout->count];
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

    if(mm_layout_entry_protected(e)) e->enabled = true;
    layout->count++;
}

static void mm_layout_synthesize_minimal(MmLayout* layout) {
    layout->count = 0;
    mm_append(layout, MmKindApplications, true, MM_APPLICATIONS_NAME, MM_APPLICATIONS_NAME);
    mm_append(layout, MmKindInternal, true, MM_SUBGHZ_NAME, MM_SUBGHZ_NAME);
    mm_append(layout, MmKindFoxSettings, true, "", "Fox Settings");
    mm_append(layout, MmKindSettings, true, "", "Settings");
}

void mm_layout_load(MmLayout* layout) {
    layout->count = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, MM_LAYOUT_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[MM_LAYOUT_ID_LEN + MM_LAYOUT_LABEL_LEN + 16];
        size_t line_len = 0;
        uint8_t byte;

        while(layout->count < MM_LAYOUT_MAX && storage_file_read(file, &byte, 1) == 1) {
            if(byte == '\n' || byte == '\r') {
                if(line_len > 0) {
                    line[line_len] = '\0';
                    mm_parse_line(line, layout);
                    line_len = 0;
                }
            } else if(line_len < sizeof(line) - 1) {
                line[line_len++] = (char)byte;
            }
        }
        if(line_len > 0 && layout->count < MM_LAYOUT_MAX) {
            line[line_len] = '\0';
            mm_parse_line(line, layout);
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(layout->count == 0) {
        mm_layout_synthesize_minimal(layout);
    }
}

void mm_layout_save(const MmLayout* layout) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, MM_LAYOUT_DIR);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, MM_LAYOUT_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, MM_LAYOUT_HEADER, strlen(MM_LAYOUT_HEADER));
        storage_file_write(file, "\n", 1);
        for(uint8_t i = 0; i < layout->count; i++) {
            const MmEntry* e = &layout->entries[i];
            const char* tok = mm_kind_to_token(e->kind);
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

void mm_layout_reset_file(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove(storage, MM_LAYOUT_FILE);
    furi_record_close(RECORD_STORAGE);
}

void mm_layout_move(MmLayout* layout, uint8_t index, int dir) {
    if(index >= layout->count) return;
    int target = (int)index + (dir < 0 ? -1 : 1);
    if(target < 0 || target >= (int)layout->count) return;

    MmEntry tmp = layout->entries[index];
    layout->entries[index] = layout->entries[target];
    layout->entries[target] = tmp;
}

void mm_layout_remove(MmLayout* layout, uint8_t index) {
    if(index >= layout->count) return;
    for(uint8_t i = index; i + 1 < layout->count; i++) {
        layout->entries[i] = layout->entries[i + 1];
    }
    layout->count--;
}

bool mm_layout_add_user(MmLayout* layout, const char* path, const char* label) {
    if(layout->count >= MM_LAYOUT_MAX) return false;
    for(uint8_t i = 0; i < layout->count; i++) {
        if(layout->entries[i].kind == MmKindUser &&
           strcmp(layout->entries[i].identifier, path) == 0) {
            return false; /* duplicate */
        }
    }

    /* Insert before the first Fox Settings / Settings entry so new user apps
     * land among the apps rather than after the settings block. */
    uint8_t insert_at = layout->count;
    for(uint8_t i = 0; i < layout->count; i++) {
        if(layout->entries[i].kind == MmKindFoxSettings ||
           layout->entries[i].kind == MmKindSettings) {
            insert_at = i;
            break;
        }
    }

    for(uint8_t i = layout->count; i > insert_at; i--) {
        layout->entries[i] = layout->entries[i - 1];
    }

    MmEntry* e = &layout->entries[insert_at];
    e->kind = MmKindUser;
    e->enabled = true;
    e->identifier[0] = '\0';
    strlcpy(e->identifier, path, sizeof(e->identifier));
    e->label[0] = '\0';
    if(label) strlcpy(e->label, label, sizeof(e->label));
    layout->count++;
    return true;
}
