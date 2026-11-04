#pragma once

#include <furi.h>
#include <stdint.h>
#include <stdbool.h>

#define FMH_DATA_PATH "/ext/apps_data/fox_freqmod_helper/vehicles.txt"

#define FMH_REGION_LEN 24

typedef struct {
    char** items;
    uint16_t count;
    uint16_t capacity;
} FmhStrList;

typedef struct {
    int32_t year_from;
    int32_t year_to;
} FmhYear;

typedef struct {
    FmhYear* items;
    uint16_t count;
    uint16_t capacity;
} FmhYearList;

typedef struct {
    char region[FMH_REGION_LEN];
    uint32_t freq;
    uint8_t preset;
    char launch;
} FmhResult;

typedef struct {
    FmhResult* items;
    uint16_t count;
    uint16_t capacity;
} FmhResultList;

void fmh_str_list_init(FmhStrList* list);
void fmh_str_list_reset(FmhStrList* list);
void fmh_str_list_free(FmhStrList* list);
int32_t fmh_str_list_index_of(const FmhStrList* list, const char* value);

void fmh_year_list_init(FmhYearList* list);
void fmh_year_list_reset(FmhYearList* list);
void fmh_year_list_free(FmhYearList* list);

void fmh_result_list_init(FmhResultList* list);
void fmh_result_list_reset(FmhResultList* list);
void fmh_result_list_free(FmhResultList* list);

bool fmh_data_file_exists(void);
bool fmh_data_load_makes(FmhStrList* out);
bool fmh_data_load_models(const char* make, FmhStrList* out);
bool fmh_data_load_years(const char* make, const char* model, FmhYearList* out);
bool fmh_data_load_results(
    const char* make,
    const char* model,
    int32_t year_from,
    int32_t year_to,
    FmhResultList* out);

const char* fmh_preset_name(uint8_t preset);
void fmh_format_mhz(uint32_t freq_hz, char* out, size_t out_size);
void fmh_format_year(int32_t year_from, int32_t year_to, char* out, size_t out_size);
