#include "fmh_vehicle_data.h"

#include <storage/storage.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/buffered_file_stream.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char make[32];
    char model[48];
    int32_t yfrom;
    int32_t yto;
    char region[FMH_REGION_LEN];
    uint32_t freq;
    uint8_t preset;
    char launch;
} FmhRow;

void fmh_str_list_init(FmhStrList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void fmh_str_list_ensure(FmhStrList* list, uint16_t need) {
    if(list->capacity >= need) return;
    uint16_t cap = list->capacity ? list->capacity : 16;
    while(cap < need) cap = (uint16_t)(cap * 2);
    char** grown = realloc(list->items, cap * sizeof(char*));
    furi_check(grown);
    list->items = grown;
    list->capacity = cap;
}

static void fmh_str_list_add(FmhStrList* list, const char* value) {
    fmh_str_list_ensure(list, (uint16_t)(list->count + 1));
    size_t len = strlen(value);
    char* dup = malloc(len + 1);
    furi_check(dup);
    memcpy(dup, value, len + 1);
    list->items[list->count++] = dup;
}

void fmh_str_list_reset(FmhStrList* list) {
    for(uint16_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    list->count = 0;
}

void fmh_str_list_free(FmhStrList* list) {
    fmh_str_list_reset(list);
    if(list->items) free(list->items);
    list->items = NULL;
    list->capacity = 0;
}

int32_t fmh_str_list_index_of(const FmhStrList* list, const char* value) {
    for(uint16_t i = 0; i < list->count; i++) {
        if(strcmp(list->items[i], value) == 0) return (int32_t)i;
    }
    return -1;
}

void fmh_year_list_init(FmhYearList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void fmh_year_list_ensure(FmhYearList* list, uint16_t need) {
    if(list->capacity >= need) return;
    uint16_t cap = list->capacity ? list->capacity : 8;
    while(cap < need) cap = (uint16_t)(cap * 2);
    FmhYear* grown = realloc(list->items, cap * sizeof(FmhYear));
    furi_check(grown);
    list->items = grown;
    list->capacity = cap;
}

static void fmh_year_list_add(FmhYearList* list, int32_t yfrom, int32_t yto) {
    fmh_year_list_ensure(list, (uint16_t)(list->count + 1));
    list->items[list->count].year_from = yfrom;
    list->items[list->count].year_to = yto;
    list->count++;
}

static bool fmh_year_list_contains(const FmhYearList* list, int32_t yfrom, int32_t yto) {
    for(uint16_t i = 0; i < list->count; i++) {
        if(list->items[i].year_from == yfrom && list->items[i].year_to == yto) return true;
    }
    return false;
}

void fmh_year_list_reset(FmhYearList* list) {
    list->count = 0;
}

void fmh_year_list_free(FmhYearList* list) {
    if(list->items) free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void fmh_result_list_init(FmhResultList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void fmh_result_list_ensure(FmhResultList* list, uint16_t need) {
    if(list->capacity >= need) return;
    uint16_t cap = list->capacity ? list->capacity : 8;
    while(cap < need) cap = (uint16_t)(cap * 2);
    FmhResult* grown = realloc(list->items, cap * sizeof(FmhResult));
    furi_check(grown);
    list->items = grown;
    list->capacity = cap;
}

static void fmh_result_list_add(
    FmhResultList* list,
    const char* region,
    uint32_t freq,
    uint8_t preset,
    char launch) {
    fmh_result_list_ensure(list, (uint16_t)(list->count + 1));
    strncpy(list->items[list->count].region, region, FMH_REGION_LEN - 1);
    list->items[list->count].region[FMH_REGION_LEN - 1] = '\0';
    list->items[list->count].freq = freq;
    list->items[list->count].preset = preset;
    list->items[list->count].launch = launch;
    list->count++;
}

void fmh_result_list_reset(FmhResultList* list) {
    list->count = 0;
}

void fmh_result_list_free(FmhResultList* list) {
    if(list->items) free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool fmh_parse_line(const char* line, FmhRow* row) {
    if(line[0] == '\0' || line[0] == '#') return false;

    char buf[160];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* fields[8];
    uint8_t n = 0;
    char* p = buf;
    fields[n++] = p;
    while(*p && n < 8) {
        if(*p == '|') {
            *p = '\0';
            fields[n++] = p + 1;
        }
        p++;
    }
    if(n < 8) return false;

    strncpy(row->make, fields[0], sizeof(row->make) - 1);
    row->make[sizeof(row->make) - 1] = '\0';
    strncpy(row->model, fields[1], sizeof(row->model) - 1);
    row->model[sizeof(row->model) - 1] = '\0';
    row->yfrom = atoi(fields[2]);
    row->yto = atoi(fields[3]);
    strncpy(row->region, fields[4], sizeof(row->region) - 1);
    row->region[sizeof(row->region) - 1] = '\0';
    row->freq = (uint32_t)strtoul(fields[5], NULL, 10);
    row->preset = (uint8_t)atoi(fields[6]);
    row->launch = fields[7][0];
    return true;
}

bool fmh_data_file_exists(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, FMH_DATA_PATH);
    furi_record_close(RECORD_STORAGE);
    return exists;
}

bool fmh_data_load_makes(FmhStrList* out) {
    fmh_str_list_reset(out);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = buffered_file_stream_alloc(storage);
    bool ok = false;
    if(buffered_file_stream_open(stream, FMH_DATA_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ok = true;
        FuriString* line = furi_string_alloc();
        FmhRow row;
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            if(!fmh_parse_line(furi_string_get_cstr(line), &row)) continue;
            if(fmh_str_list_index_of(out, row.make) < 0) {
                fmh_str_list_add(out, row.make);
            }
        }
        furi_string_free(line);
    }
    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool fmh_data_load_models(const char* make, FmhStrList* out) {
    fmh_str_list_reset(out);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = buffered_file_stream_alloc(storage);
    bool ok = false;
    if(buffered_file_stream_open(stream, FMH_DATA_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ok = true;
        FuriString* line = furi_string_alloc();
        FmhRow row;
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            if(!fmh_parse_line(furi_string_get_cstr(line), &row)) continue;
            if(strcmp(row.make, make) != 0) continue;
            if(row.model[0] == '\0') continue;
            if(fmh_str_list_index_of(out, row.model) < 0) {
                fmh_str_list_add(out, row.model);
            }
        }
        furi_string_free(line);
    }
    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool fmh_data_load_years(const char* make, const char* model, FmhYearList* out) {
    fmh_year_list_reset(out);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = buffered_file_stream_alloc(storage);
    bool ok = false;
    if(buffered_file_stream_open(stream, FMH_DATA_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ok = true;
        FuriString* line = furi_string_alloc();
        FmhRow row;
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            if(!fmh_parse_line(furi_string_get_cstr(line), &row)) continue;
            if(strcmp(row.make, make) != 0) continue;
            if(strcmp(row.model, model) != 0) continue;
            if(!fmh_year_list_contains(out, row.yfrom, row.yto)) {
                fmh_year_list_add(out, row.yfrom, row.yto);
            }
        }
        furi_string_free(line);
    }
    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool fmh_data_load_results(
    const char* make,
    const char* model,
    int32_t year_from,
    int32_t year_to,
    FmhResultList* out) {
    fmh_result_list_reset(out);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = buffered_file_stream_alloc(storage);
    bool ok = false;
    if(buffered_file_stream_open(stream, FMH_DATA_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ok = true;
        FuriString* line = furi_string_alloc();
        FmhRow row;
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            if(!fmh_parse_line(furi_string_get_cstr(line), &row)) continue;
            if(strcmp(row.make, make) != 0) continue;
            if(strcmp(row.model, model) != 0) continue;
            if(row.yfrom != year_from || row.yto != year_to) continue;
            fmh_result_list_add(out, row.region, row.freq, row.preset, row.launch);
        }
        furi_string_free(line);
    }
    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

const char* fmh_preset_name(uint8_t preset) {
    switch(preset) {
    case 0:
        return "AM270";
    case 1:
        return "AM650";
    case 2:
        return "FM238";
    case 3:
        return "FM476";
    case 4:
        return "FM12K";
    default:
        return "AM650";
    }
}

void fmh_format_mhz(uint32_t freq_hz, char* out, size_t out_size) {
    uint32_t whole = freq_hz / 1000000UL;
    uint32_t frac = (freq_hz % 1000000UL) / 10000UL;
    snprintf(out, out_size, "%lu.%02lu", (unsigned long)whole, (unsigned long)frac);
}

void fmh_format_year(int32_t year_from, int32_t year_to, char* out, size_t out_size) {
    if(year_from <= 0 && year_to <= 0) {
        snprintf(out, out_size, "All Years");
    } else if(year_to <= 0) {
        snprintf(out, out_size, "%ld+", (long)year_from);
    } else if(year_from <= 0) {
        snprintf(out, out_size, "Up to %ld", (long)year_to);
    } else {
        snprintf(out, out_size, "%ld-%ld", (long)year_from, (long)year_to);
    }
}
