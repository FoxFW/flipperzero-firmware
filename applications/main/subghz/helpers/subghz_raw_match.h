#pragma once

#include <furi.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/types.h>

#define SUBGHZ_RAW_MATCH_MAX_CANDIDATES 10
#define SUBGHZ_RAW_MATCH_MAX_HITS       8

typedef struct SubGhzRawMatch SubGhzRawMatch;

typedef struct {
    const char* name;
    uint16_t te_short;
    uint16_t te_long;
} SubGhzRawMatchCandidate;

typedef struct {
    const char* name;
    uint16_t scale_pct;
    bool inverted;
    uint8_t type;
    FlipperFormat* ff;
} SubGhzRawMatchHit;

SubGhzRawMatch* subghz_raw_match_alloc(
    SubGhzEnvironment* environment,
    FlipperFormat* raw,
    const SubGhzRadioPreset* preset);
void subghz_raw_match_free(SubGhzRawMatch* instance);

size_t subghz_raw_match_get_edge_count(SubGhzRawMatch* instance);
uint32_t subghz_raw_match_get_te_short(SubGhzRawMatch* instance);
uint32_t subghz_raw_match_get_te_long(SubGhzRawMatch* instance);

size_t subghz_raw_match_get_candidate_count(SubGhzRawMatch* instance);
const SubGhzRawMatchCandidate* subghz_raw_match_get_candidate(SubGhzRawMatch* instance, size_t index);

int16_t subghz_raw_match_try_protocol(SubGhzRawMatch* instance, const char* name);

void subghz_raw_match_scan_begin(SubGhzRawMatch* instance);
bool subghz_raw_match_scan_step(SubGhzRawMatch* instance, uint32_t budget_ms);
size_t subghz_raw_match_scan_position(SubGhzRawMatch* instance);
size_t subghz_raw_match_scan_total(SubGhzRawMatch* instance);
const char* subghz_raw_match_scan_current_name(SubGhzRawMatch* instance);

size_t subghz_raw_match_get_hit_count(SubGhzRawMatch* instance);
const SubGhzRawMatchHit* subghz_raw_match_get_hit(SubGhzRawMatch* instance, size_t index);
bool subghz_raw_match_get_hit_string(SubGhzRawMatch* instance, size_t index, FuriString* output);
FlipperFormat* subghz_raw_match_take_hit_ff(SubGhzRawMatch* instance, size_t index);
