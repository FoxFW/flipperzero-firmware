#include "dolphin_fox.h"

#include <stdlib.h>
#include <string.h>
#include <flipper_format/flipper_format.h>
#include <furi_hal_random.h>

typedef struct {
    char name[DF_NAME_MAX];
    uint8_t min_butthurt;
    uint8_t max_butthurt;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t weight;
} DfManifestRow;

uint8_t df_mood_butthurt(uint8_t mood) {
    switch(mood) {
    case 0:
        return 2;
    case 1:
        return 7;
    default:
        return 12;
    }
}

static void df_sort_by_name(DfAnimInfo* list, size_t count) {
    for(size_t i = 1; i < count; i++) {
        DfAnimInfo key = list[i];
        size_t j = i;
        while(j > 0 && strcmp(list[j - 1].name, key.name) > 0) {
            list[j] = list[j - 1];
            j--;
        }
        list[j] = key;
    }
}

static bool df_meta_exists(DfApp* app, const char* name) {
    FuriString* path = furi_string_alloc();
    furi_string_printf(path, DF_ANIM_DIR "/%s/meta.txt", name);
    File* file = storage_file_alloc(app->storage);
    bool ok =
        storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING);
    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    return ok;
}

static DfManifestRow* df_manifest_load(DfApp* app, size_t* out_count) {
    DfManifestRow* rows = NULL;
    size_t cap = 0;
    size_t cnt = 0;

    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    FuriString* str = furi_string_alloc();
    uint32_t u32 = 0;
    do {
        if(!flipper_format_file_open_existing(ff, DF_ANIM_DIR "/manifest.txt")) break;
        if(!flipper_format_read_header(ff, str, &u32)) break;
        flipper_format_set_strict_mode(ff, true);
        while(flipper_format_read_string(ff, "Name", str)) {
            DfManifestRow row;
            memset(&row, 0, sizeof(row));
            strncpy(row.name, furi_string_get_cstr(str), DF_NAME_MAX - 1);

            if(!flipper_format_read_uint32(ff, "Min butthurt", &u32, 1)) break;
            row.min_butthurt = (uint8_t)u32;
            if(!flipper_format_read_uint32(ff, "Max butthurt", &u32, 1)) break;
            row.max_butthurt = (uint8_t)u32;
            if(!flipper_format_read_uint32(ff, "Min level", &u32, 1)) break;
            row.min_level = (uint8_t)u32;
            if(!flipper_format_read_uint32(ff, "Max level", &u32, 1)) break;
            row.max_level = (uint8_t)u32;
            if(!flipper_format_read_uint32(ff, "Weight", &u32, 1)) break;
            row.weight = (uint8_t)u32;

            if(cnt == cap) {
                cap = cap ? cap * 2 : 8;
                rows = realloc(rows, cap * sizeof(DfManifestRow));
            }
            rows[cnt++] = row;
        }
    } while(0);
    furi_string_free(str);
    flipper_format_free(ff);

    *out_count = cnt;
    return rows;
}

void df_library_build(DfApp* app) {
    app->anims = NULL;
    app->anim_count = 0;

    size_t mcount = 0;
    DfManifestRow* manifest = df_manifest_load(app, &mcount);

    DfAnimInfo* list = NULL;
    size_t cap = 0;
    size_t cnt = 0;

    File* dir = storage_file_alloc(app->storage);
    if(storage_dir_open(dir, DF_ANIM_DIR)) {
        char name[DF_NAME_MAX];
        FileInfo info;
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(!file_info_is_dir(&info)) continue;
            if(name[0] == '.') continue;
            if(!df_meta_exists(app, name)) continue;

            DfAnimInfo a;
            memset(&a, 0, sizeof(a));
            strncpy(a.name, name, DF_NAME_MAX - 1);

            bool found = false;
            for(size_t i = 0; i < mcount; i++) {
                if(strcmp(manifest[i].name, name) == 0) {
                    a.min_butthurt = manifest[i].min_butthurt;
                    a.max_butthurt = manifest[i].max_butthurt;
                    a.min_level = manifest[i].min_level;
                    a.max_level = manifest[i].max_level;
                    a.weight = manifest[i].weight;
                    found = true;
                    break;
                }
            }

            if(!found) {
                a.min_butthurt = 0;
                a.max_butthurt = DF_BUTTHURT_MAX;
                a.min_level = 1;
                a.max_level = DF_ANIM_LEVEL_MAX;
                a.weight = 3;
                if((name[0] == 'L' || name[0] == 'l') && name[1] >= '1' && name[1] <= '9' &&
                   name[2] == '_') {
                    a.min_level = (uint8_t)(name[1] - '0');
                    if(a.min_level > DF_ANIM_LEVEL_MAX) a.min_level = DF_ANIM_LEVEL_MAX;
                    if(a.max_level < a.min_level) a.max_level = a.min_level;
                }
            }

            if(a.weight == 0) a.weight = 1;

            if(cnt == cap) {
                cap = cap ? cap * 2 : 16;
                list = realloc(list, cap * sizeof(DfAnimInfo));
            }
            list[cnt++] = a;
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);

    if(manifest) free(manifest);

    if(cnt > 1) {
        df_sort_by_name(list, cnt);
    }

    app->anims = list;
    app->anim_count = cnt;
}

void df_library_free(DfApp* app) {
    if(app->anims) {
        free(app->anims);
        app->anims = NULL;
    }
    app->anim_count = 0;
}

static bool df_eligible(const DfAnimInfo* a, uint8_t level, uint8_t butthurt) {
    return (level >= a->min_level) && (level <= a->max_level) && (butthurt >= a->min_butthurt) &&
           (butthurt <= a->max_butthurt);
}

int df_library_pick(DfApp* app) {
    if(app->anim_count == 0) return -1;

    uint8_t eff_level = app->level;
    if(eff_level > DF_ANIM_LEVEL_MAX) eff_level = DF_ANIM_LEVEL_MAX;
    uint8_t butthurt = df_mood_butthurt(app->mood);

    uint32_t total = 0;
    for(size_t i = 0; i < app->anim_count; i++) {
        if(df_eligible(&app->anims[i], eff_level, butthurt)) {
            total += app->anims[i].weight;
        }
    }

    if(total == 0) {
        return (int)(furi_hal_random_get() % app->anim_count);
    }

    uint32_t r = furi_hal_random_get() % total;
    for(size_t i = 0; i < app->anim_count; i++) {
        if(df_eligible(&app->anims[i], eff_level, butthurt)) {
            uint32_t w = app->anims[i].weight;
            if(r < w) return (int)i;
            r -= w;
        }
    }
    return 0;
}
