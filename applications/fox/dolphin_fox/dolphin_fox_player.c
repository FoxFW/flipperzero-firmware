#include "dolphin_fox.h"

#include <stdlib.h>
#include <flipper_format/flipper_format.h>

void df_player_unload(DfApp* app) {
    if(app->frames) {
        for(size_t i = 0; i < app->frame_slots; i++) {
            if(app->frames[i]) free(app->frames[i]);
        }
        free(app->frames);
        app->frames = NULL;
    }
    app->frame_slots = 0;
    if(app->seq) {
        free(app->seq);
        app->seq = NULL;
    }
    app->seq_len = 0;
    app->pos = 0;
    app->width = 0;
    app->height = 0;
    app->frame_period_ms = 0;
}

bool df_player_load(DfApp* app, int index) {
    if(index < 0 || (size_t)index >= app->anim_count) return false;
    const char* aname = app->anims[index].name;

    bool success = false;
    uint32_t* order = NULL;

    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    FuriString* path = furi_string_alloc();
    FuriString* type = furi_string_alloc();
    uint32_t u32 = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t passive = 0;
    uint32_t active = 0;
    uint32_t rate = 0;
    uint32_t count = 0;

    do {
        furi_string_printf(path, DF_ANIM_DIR "/%s/meta.txt", aname);
        if(!flipper_format_file_open_existing(ff, furi_string_get_cstr(path))) break;
        flipper_format_set_strict_mode(ff, false);
        if(!flipper_format_read_header(ff, type, &u32)) break;
        if(furi_string_cmp_str(type, "Flipper Animation")) break;

        if(!flipper_format_read_uint32(ff, "Width", &width, 1)) break;
        if(!flipper_format_read_uint32(ff, "Height", &height, 1)) break;
        if(!flipper_format_read_uint32(ff, "Passive frames", &passive, 1)) break;
        if(!flipper_format_read_uint32(ff, "Active frames", &active, 1)) break;
        if(passive == 0 || width == 0 || width > 128 || height == 0 || height > 64) break;

        if(!flipper_format_get_value_count(ff, "Frames order", &count)) break;
        if(count == 0 || count != passive + active) break;
        order = malloc(sizeof(uint32_t) * count);
        if(!flipper_format_read_uint32(ff, "Frames order", order, count)) break;
        if(!flipper_format_read_uint32(ff, "Frame rate", &rate, 1)) break;

        app->seq_len = passive;
        app->seq = malloc(passive);
        uint8_t max_index = 0;
        for(uint32_t i = 0; i < passive; i++) {
            uint8_t fi = (uint8_t)order[i];
            app->seq[i] = fi;
            if(fi > max_index) max_index = fi;
        }
        app->frame_slots = (size_t)max_index + 1;
        app->frames = calloc(app->frame_slots, sizeof(uint8_t*));

        bool frames_ok = true;
        for(uint32_t i = 0; i < passive; i++) {
            uint8_t fi = app->seq[i];
            if(app->frames[fi]) continue;

            FuriString* fp = furi_string_alloc();
            furi_string_printf(fp, DF_ANIM_DIR "/%s/frame_%u.bm", aname, (unsigned)fi);
            File* file = storage_file_alloc(app->storage);
            if(storage_file_open(file, furi_string_get_cstr(fp), FSAM_READ, FSOM_OPEN_EXISTING)) {
                uint64_t sz = storage_file_size(file);
                if(sz > 0 && sz < 8192) {
                    uint8_t* buf = malloc((size_t)sz);
                    if(storage_file_read(file, buf, sz) == sz) {
                        app->frames[fi] = buf;
                    } else {
                        free(buf);
                        frames_ok = false;
                    }
                } else {
                    frames_ok = false;
                }
            } else {
                frames_ok = false;
            }
            storage_file_close(file);
            storage_file_free(file);
            furi_string_free(fp);
            if(!frames_ok) break;
        }

        if(!frames_ok) break;

        app->width = (uint8_t)width;
        app->height = (uint8_t)height;
        app->frame_period_ms = (rate > 0) ? (1000 / rate) : 500;
        app->pos = 0;
        success = true;
    } while(0);

    if(order) free(order);
    furi_string_free(path);
    furi_string_free(type);
    flipper_format_free(ff);

    if(!success) df_player_unload(app);
    return success;
}

void df_player_draw(DfApp* app, Canvas* canvas) {
    if(app->anim_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 26, AlignCenter, AlignCenter, "No dolphin animations");
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "found on the SD card");
        return;
    }

    if(app->seq_len == 0 || app->frames == NULL) return;

    uint8_t fi = app->seq[app->pos];
    if(fi >= app->frame_slots || app->frames[fi] == NULL) return;

    int32_t x = (128 - (int32_t)app->width) / 2;
    int32_t y = (64 - (int32_t)app->height) / 2;
    canvas_draw_bitmap(canvas, x, y, app->width, app->height, app->frames[fi]);
}
