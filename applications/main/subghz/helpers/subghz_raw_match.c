#include "subghz_raw_match.h"
#include "subghz_protocol_timing.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <lib/subghz/registry.h>
#include <lib/subghz/protocols/base.h>

#define TAG "SubGhzRawMatch"

#ifndef SUBGHZ_RAW_MATCH_MAX_SAMPLES
#define SUBGHZ_RAW_MATCH_MAX_SAMPLES 2048u
#endif
#define SUBGHZ_RAW_MATCH_GAP_US      30000u
#define SUBGHZ_RAW_MATCH_TIMING_MAX  6000u
#define SUBGHZ_RAW_MATCH_TIMING_MIN  40u
#define SUBGHZ_RAW_MATCH_BINS        48u

static const uint16_t subghz_raw_match_scales[] = {100, 92, 108, 85, 118};

struct SubGhzRawMatch {
    SubGhzEnvironment* environment;
    int32_t* samples;
    size_t sample_count;
    SubGhzRadioPreset preset;
    uint32_t te_short;
    uint32_t te_long;
    SubGhzRawMatchCandidate candidates[SUBGHZ_RAW_MATCH_MAX_CANDIDATES];
    size_t candidate_count;
    SubGhzRawMatchHit hits[SUBGHZ_RAW_MATCH_MAX_HITS];
    size_t hit_count;
    size_t scan_index;
    size_t scan_total;
    const char* scan_name;
    FlipperFormat* fired_ff;
};

static void subghz_raw_match_load(SubGhzRawMatch* instance, FlipperFormat* raw) {
    instance->samples = malloc(SUBGHZ_RAW_MATCH_MAX_SAMPLES * sizeof(int32_t));
    instance->sample_count = 0;
    if(!raw || !flipper_format_rewind(raw)) return;
    uint32_t count = 0;
    while(instance->sample_count < SUBGHZ_RAW_MATCH_MAX_SAMPLES &&
          flipper_format_get_value_count(raw, "RAW_Data", &count) && count > 0) {
        size_t room = SUBGHZ_RAW_MATCH_MAX_SAMPLES - instance->sample_count;
        if(count > room) count = room;
        if(!flipper_format_read_int32(
               raw, "RAW_Data", &instance->samples[instance->sample_count], count)) {
            break;
        }
        instance->sample_count += count;
    }
}

static uint8_t subghz_raw_match_bin(uint32_t duration) {
    float bin = logf((float)duration / (float)SUBGHZ_RAW_MATCH_TIMING_MIN) / logf(1.12f);
    if(bin < 0.0f) return 0;
    if(bin >= (float)(SUBGHZ_RAW_MATCH_BINS - 1)) return SUBGHZ_RAW_MATCH_BINS - 1;
    return (uint8_t)bin;
}

static uint32_t subghz_raw_match_cluster_mean(SubGhzRawMatch* instance, uint8_t center) {
    uint64_t sum = 0;
    uint32_t count = 0;
    for(size_t i = 0; i < instance->sample_count; i++) {
        uint32_t d = (uint32_t)abs(instance->samples[i]);
        if(d < SUBGHZ_RAW_MATCH_TIMING_MIN || d > SUBGHZ_RAW_MATCH_TIMING_MAX) continue;
        uint8_t bin = subghz_raw_match_bin(d);
        if(bin + 1 >= center && bin <= center + 1) {
            sum += d;
            count++;
        }
    }
    return count ? (uint32_t)(sum / count) : 0;
}

static void subghz_raw_match_measure(SubGhzRawMatch* instance) {
    instance->te_short = 0;
    instance->te_long = 0;
    uint16_t counts[SUBGHZ_RAW_MATCH_BINS] = {0};
    for(size_t i = 0; i < instance->sample_count; i++) {
        uint32_t d = (uint32_t)abs(instance->samples[i]);
        if(d < SUBGHZ_RAW_MATCH_TIMING_MIN || d > SUBGHZ_RAW_MATCH_TIMING_MAX) continue;
        counts[subghz_raw_match_bin(d)]++;
    }
    uint16_t score[SUBGHZ_RAW_MATCH_BINS] = {0};
    for(uint8_t b = 0; b < SUBGHZ_RAW_MATCH_BINS; b++) {
        score[b] = (uint16_t)(counts[b] + (b > 0 ? counts[b - 1] : 0) +
                              ((uint32_t)b + 1u < SUBGHZ_RAW_MATCH_BINS ? counts[b + 1] : 0));
    }
    uint8_t first = 0;
    for(uint8_t b = 1; b < SUBGHZ_RAW_MATCH_BINS; b++) {
        if(score[b] > score[first]) first = b;
    }
    if(score[first] < 4) return;
    int16_t second = -1;
    for(uint8_t b = 0; b < SUBGHZ_RAW_MATCH_BINS; b++) {
        if((uint32_t)b + 4u > first && (uint32_t)b < (uint32_t)first + 4u) continue;
        if((uint32_t)score[b] * 5u < score[first]) continue;
        if(second < 0 || score[b] > score[second]) second = b;
    }
    uint32_t a = subghz_raw_match_cluster_mean(instance, first);
    uint32_t b = second >= 0 ? subghz_raw_match_cluster_mean(instance, (uint8_t)second) : 0;
    if(b && b < a) {
        uint32_t t = a;
        a = b;
        b = t;
    }
    instance->te_short = a;
    instance->te_long = b;
}

static float subghz_raw_match_distance(SubGhzRawMatch* instance, uint16_t te_short, uint16_t te_long) {
    float distance = fabsf(logf((float)te_short / (float)instance->te_short));
    if(instance->te_long && te_long) {
        distance += fabsf(logf((float)te_long / (float)instance->te_long));
    } else {
        distance += 0.5f;
    }
    return distance;
}

static void subghz_raw_match_rank(SubGhzRawMatch* instance) {
    instance->candidate_count = 0;
    if(!instance->te_short) return;
    float best[SUBGHZ_RAW_MATCH_MAX_CANDIDATES];
    for(size_t i = 0; i < subghz_protocol_timing_count; i++) {
        const SubGhzProtocolTiming* timing = &subghz_protocol_timing[i];
        if(!timing->te_short) continue;
        float distance = subghz_raw_match_distance(instance, timing->te_short, timing->te_long);
        size_t pos = instance->candidate_count;
        while(pos > 0 && best[pos - 1] > distance) pos--;
        if(pos >= SUBGHZ_RAW_MATCH_MAX_CANDIDATES) continue;
        size_t last = instance->candidate_count < SUBGHZ_RAW_MATCH_MAX_CANDIDATES ?
                          instance->candidate_count :
                          SUBGHZ_RAW_MATCH_MAX_CANDIDATES - 1;
        for(size_t k = last; k > pos; k--) {
            best[k] = best[k - 1];
            instance->candidates[k] = instance->candidates[k - 1];
        }
        best[pos] = distance;
        instance->candidates[pos].name = timing->name;
        instance->candidates[pos].te_short = timing->te_short;
        instance->candidates[pos].te_long = timing->te_long;
        if(instance->candidate_count < SUBGHZ_RAW_MATCH_MAX_CANDIDATES) instance->candidate_count++;
    }
}

SubGhzRawMatch* subghz_raw_match_alloc(
    SubGhzEnvironment* environment,
    FlipperFormat* raw,
    const SubGhzRadioPreset* preset) {
    SubGhzRawMatch* instance = malloc(sizeof(SubGhzRawMatch));
    memset(instance, 0, sizeof(SubGhzRawMatch));
    instance->environment = environment;
    instance->preset.name = furi_string_alloc();
    if(preset) {
        furi_string_set(instance->preset.name, preset->name);
        instance->preset.frequency = preset->frequency;
        instance->preset.data = preset->data;
        instance->preset.data_size = preset->data_size;
    }
    subghz_raw_match_load(instance, raw);
    subghz_raw_match_measure(instance);
    subghz_raw_match_rank(instance);
    return instance;
}

void subghz_raw_match_free(SubGhzRawMatch* instance) {
    furi_check(instance);
    for(size_t i = 0; i < instance->hit_count; i++) {
        if(instance->hits[i].ff) flipper_format_free(instance->hits[i].ff);
    }
    furi_string_free(instance->preset.name);
    free(instance->samples);
    free(instance);
}

size_t subghz_raw_match_get_edge_count(SubGhzRawMatch* instance) {
    return instance->sample_count;
}

uint32_t subghz_raw_match_get_te_short(SubGhzRawMatch* instance) {
    return instance->te_short;
}

uint32_t subghz_raw_match_get_te_long(SubGhzRawMatch* instance) {
    return instance->te_long;
}

size_t subghz_raw_match_get_candidate_count(SubGhzRawMatch* instance) {
    return instance->candidate_count;
}

const SubGhzRawMatchCandidate* subghz_raw_match_get_candidate(SubGhzRawMatch* instance, size_t index) {
    return index < instance->candidate_count ? &instance->candidates[index] : NULL;
}

static void subghz_raw_match_fired(SubGhzProtocolDecoderBase* decoder_base, void* context) {
    SubGhzRawMatch* instance = context;
    if(instance->fired_ff) return;
    FlipperFormat* ff = flipper_format_string_alloc();
    if(subghz_protocol_decoder_base_serialize(decoder_base, ff, &instance->preset) ==
       SubGhzProtocolStatusOk) {
        instance->fired_ff = ff;
    } else {
        flipper_format_free(ff);
    }
}

static FlipperFormat* subghz_raw_match_run(
    SubGhzRawMatch* instance,
    const SubGhzProtocol* protocol,
    uint16_t scale_pct,
    bool inverted) {
    void* decoder = protocol->decoder->alloc(instance->environment);
    SubGhzProtocolDecoderBase* decoder_base = decoder;
    protocol->decoder->reset(decoder);
    instance->fired_ff = NULL;
    subghz_protocol_decoder_base_set_decoder_callback(
        decoder_base, subghz_raw_match_fired, instance);
    for(size_t i = 0; i < instance->sample_count && !instance->fired_ff; i++) {
        int32_t sample = instance->samples[i];
        bool level = (sample > 0) != inverted;
        uint32_t duration = (uint32_t)abs(sample) * scale_pct / 100u;
        if(i + 1 == instance->sample_count && !level && duration < SUBGHZ_RAW_MATCH_GAP_US) {
            duration = SUBGHZ_RAW_MATCH_GAP_US;
        }
        protocol->decoder->feed(decoder, level, duration);
        if(i + 1 == instance->sample_count && level && !instance->fired_ff) {
            protocol->decoder->feed(decoder, false, SUBGHZ_RAW_MATCH_GAP_US);
        }
    }
    protocol->decoder->free(decoder);
    FlipperFormat* ff = instance->fired_ff;
    instance->fired_ff = NULL;
    return ff;
}

static bool subghz_raw_match_testable(const SubGhzProtocol* protocol) {
    if(!protocol || !protocol->decoder || !protocol->decoder->alloc) return false;
    if(protocol->flag & (SubGhzProtocolFlag_RAW | SubGhzProtocolFlag_BinRAW)) return false;
    return (protocol->flag & SubGhzProtocolFlag_Decodable) != 0;
}

static int16_t subghz_raw_match_find_hit(SubGhzRawMatch* instance, const char* name) {
    for(size_t i = 0; i < instance->hit_count; i++) {
        if(!strcmp(instance->hits[i].name, name)) return (int16_t)i;
    }
    return -1;
}

static int16_t subghz_raw_match_add_hit(
    SubGhzRawMatch* instance,
    const SubGhzProtocol* protocol,
    uint16_t scale_pct,
    bool inverted,
    FlipperFormat* ff) {
    int16_t existing = subghz_raw_match_find_hit(instance, protocol->name);
    if(existing >= 0) {
        flipper_format_free(ff);
        return existing;
    }
    if(instance->hit_count >= SUBGHZ_RAW_MATCH_MAX_HITS) {
        flipper_format_free(ff);
        return -1;
    }
    SubGhzRawMatchHit* hit = &instance->hits[instance->hit_count];
    hit->name = protocol->name;
    hit->scale_pct = scale_pct;
    hit->inverted = inverted;
    hit->type = protocol->type;
    hit->ff = ff;
    size_t pos = instance->hit_count++;
    uint16_t distance = scale_pct > 100 ? scale_pct - 100 : 100 - scale_pct;
    while(pos > 0) {
        const SubGhzRawMatchHit* prev = &instance->hits[pos - 1];
        uint16_t prev_distance = prev->scale_pct > 100 ? prev->scale_pct - 100 : 100 - prev->scale_pct;
        if(prev_distance < distance || (prev_distance == distance && prev->inverted <= inverted)) break;
        SubGhzRawMatchHit tmp = instance->hits[pos - 1];
        instance->hits[pos - 1] = instance->hits[pos];
        instance->hits[pos] = tmp;
        pos--;
    }
    return (int16_t)pos;
}

static int16_t subghz_raw_match_try(SubGhzRawMatch* instance, const SubGhzProtocol* protocol, uint16_t fit_pct) {
    uint16_t order[COUNT_OF(subghz_raw_match_scales) + 1];
    size_t count = 0;
    order[count++] = 100;
    if(fit_pct >= 50 && fit_pct <= 200 && (fit_pct < 97 || fit_pct > 103)) order[count++] = fit_pct;
    for(size_t i = 1; i < COUNT_OF(subghz_raw_match_scales); i++) {
        order[count++] = subghz_raw_match_scales[i];
    }
    for(size_t i = 0; i < count; i++) {
        for(uint8_t pass = 0; pass < 2; pass++) {
            FlipperFormat* ff = subghz_raw_match_run(instance, protocol, order[i], pass == 1);
            if(ff) return subghz_raw_match_add_hit(instance, protocol, order[i], pass == 1, ff);
        }
    }
    return -1;
}

int16_t subghz_raw_match_try_protocol(SubGhzRawMatch* instance, const char* name) {
    furi_check(instance);
    int16_t existing = subghz_raw_match_find_hit(instance, name);
    if(existing >= 0) return existing;
    const SubGhzProtocol* protocol = subghz_protocol_registry_get_by_name(
        subghz_environment_get_protocol_registry(instance->environment), name);
    if(!subghz_raw_match_testable(protocol) || !instance->sample_count) return -1;
    uint16_t fit_pct = 100;
    for(size_t i = 0; i < subghz_protocol_timing_count; i++) {
        if(!strcmp(subghz_protocol_timing[i].name, name) && instance->te_short) {
            fit_pct = (uint16_t)(subghz_protocol_timing[i].te_short * 100u / instance->te_short);
            break;
        }
    }
    return subghz_raw_match_try(instance, protocol, fit_pct);
}

void subghz_raw_match_scan_begin(SubGhzRawMatch* instance) {
    furi_check(instance);
    instance->scan_index = 0;
    instance->scan_total =
        subghz_protocol_registry_count(subghz_environment_get_protocol_registry(instance->environment));
    instance->scan_name = NULL;
}

bool subghz_raw_match_scan_step(SubGhzRawMatch* instance, uint32_t budget_ms) {
    furi_check(instance);
    const SubGhzProtocolRegistry* registry =
        subghz_environment_get_protocol_registry(instance->environment);
    uint32_t start = furi_get_tick();
    while(instance->scan_index < instance->scan_total) {
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(registry, instance->scan_index++);
        instance->scan_name = protocol ? protocol->name : NULL;
        if(subghz_raw_match_testable(protocol) && instance->sample_count &&
           instance->hit_count < SUBGHZ_RAW_MATCH_MAX_HITS) {
            for(size_t i = 0; i < COUNT_OF(subghz_raw_match_scales); i++) {
                FlipperFormat* ff = NULL;
                bool inverted = false;
                for(uint8_t pass = 0; pass < 2 && !ff; pass++) {
                    inverted = pass == 1;
                    ff = subghz_raw_match_run(instance, protocol, subghz_raw_match_scales[i], inverted);
                }
                if(ff) {
                    subghz_raw_match_add_hit(instance, protocol, subghz_raw_match_scales[i], inverted, ff);
                    break;
                }
            }
        }
        if(furi_get_tick() - start >= furi_ms_to_ticks(budget_ms)) break;
    }
    return instance->scan_index >= instance->scan_total;
}

size_t subghz_raw_match_scan_position(SubGhzRawMatch* instance) {
    return instance->scan_index;
}

size_t subghz_raw_match_scan_total(SubGhzRawMatch* instance) {
    return instance->scan_total;
}

const char* subghz_raw_match_scan_current_name(SubGhzRawMatch* instance) {
    return instance->scan_name ? instance->scan_name : "";
}

size_t subghz_raw_match_get_hit_count(SubGhzRawMatch* instance) {
    return instance->hit_count;
}

const SubGhzRawMatchHit* subghz_raw_match_get_hit(SubGhzRawMatch* instance, size_t index) {
    return index < instance->hit_count ? &instance->hits[index] : NULL;
}

bool subghz_raw_match_get_hit_string(SubGhzRawMatch* instance, size_t index, FuriString* output) {
    furi_check(instance);
    furi_string_reset(output);
    if(index >= instance->hit_count || !instance->hits[index].ff) return false;
    const SubGhzProtocol* protocol = subghz_protocol_registry_get_by_name(
        subghz_environment_get_protocol_registry(instance->environment), instance->hits[index].name);
    if(!protocol || !protocol->decoder) return false;
    void* decoder = protocol->decoder->alloc(instance->environment);
    bool ok = false;
    flipper_format_rewind(instance->hits[index].ff);
    if(subghz_protocol_decoder_base_deserialize(decoder, instance->hits[index].ff) ==
       SubGhzProtocolStatusOk) {
        ok = subghz_protocol_decoder_base_get_string(decoder, output);
    }
    protocol->decoder->free(decoder);
    flipper_format_rewind(instance->hits[index].ff);
    return ok;
}

FlipperFormat* subghz_raw_match_take_hit_ff(SubGhzRawMatch* instance, size_t index) {
    furi_check(instance);
    if(index >= instance->hit_count) return NULL;
    FlipperFormat* ff = instance->hits[index].ff;
    instance->hits[index].ff = NULL;
    return ff;
}
