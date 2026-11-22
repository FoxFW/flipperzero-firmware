#pragma once

#include <furi.h>
#include <stdbool.h>
#include <stdint.h>

#define CAR_BREAKER_AUDIT_MAX_FRAMES 10
#define CAR_BREAKER_AUDIT_MAX_PULSES 512
#define CAR_BREAKER_AUDIT_MAX_REPLAYS 5
#define CAR_BREAKER_AUDIT_DIR "/ext/apps_data/car_breaker19"
#define CAR_BREAKER_AUDIT_LOG_PREFIX "audit_"
// Minimum seconds between capture and replay for the static-code test so a
// still-fresh rolling code cannot mask a fixed-code result.
#define CAR_BREAKER_AUDIT_STATIC_SETTLE_S 30
// Pause between consecutive replays inside a RollBack burst (ms).
#define CAR_BREAKER_AUDIT_BURST_GAP_MS 400

/** Guided audit tests, each mirroring a published attack mechanism. */
typedef enum {
    CarBreakerAuditTestStaticCode = 0, // CVE-2022-27254 class: replay a used code
    CarBreakerAuditTestRollBack,       // arXiv:2210.11923: replay burst resyncs counter
    CarBreakerAuditTestRollingPwn,     // CVE-2021-46145 class: replay unused consecutive codes
    CarBreakerAuditTestCount
} CarBreakerAuditTest;

typedef enum {
    CarBreakerAuditVerdictPending = 0,
    CarBreakerAuditVerdictVulnerable,
    CarBreakerAuditVerdictNotDetected,
    CarBreakerAuditVerdictInconclusive,
    CarBreakerAuditVerdictAborted
} CarBreakerAuditVerdict;

/** Raw retained frame, RAW_Data-compatible signed durations (+high/-low). */
typedef struct {
    int32_t durations[CAR_BREAKER_AUDIT_MAX_PULSES];
    uint16_t count;
    uint32_t timestamp_ms;
    uint32_t hash;
    uint8_t preset_index;
    bool valid;
} CarBreakerAuditFrame;

typedef struct {
    CarBreakerAuditTest test;

    // Capture phase targets (per test methodology)
    uint16_t target_frames;
    uint16_t min_frames;

    // Replay plan: indices into the retention ring, bounded
    uint8_t replay_order[CAR_BREAKER_AUDIT_MAX_REPLAYS];
    uint8_t replay_count;
    uint8_t replay_done;
    bool last_tx_ok;

    // Results
    CarBreakerAuditVerdict verdict;
    uint32_t first_capture_ms;
    uint32_t last_capture_ms;

    // Audit log
    FuriString* log;
    bool log_exported;
} CarBreakerAuditState;

/** Known-issue table sourced from the 2025 RKE systematization (arXiv:2505.02713). */
typedef struct {
    const char* brand;
    const char* issue;
    const char* detail;
} CarBreakerKnownIssue;

CarBreakerAuditState* car_breaker_audit_alloc(void);
void car_breaker_audit_free(CarBreakerAuditState* audit);

/** Begin a test: reset state, set targets, open the log. */
void car_breaker_audit_begin(CarBreakerAuditState* audit, CarBreakerAuditTest test);

/** Retain a raw frame (called under the app lock from the RX finalize path). */
void car_breaker_audit_retain(
    CarBreakerAuditState* audit,
    const uint32_t* signed_durations,
    uint16_t count,
    uint32_t timestamp_ms,
    uint32_t hash,
    uint8_t preset_index);

/** Frames retained since the audit began. */
uint16_t car_breaker_audit_capture_count(const CarBreakerAuditState* audit);

/** Total frames currently in the retention ring. */
uint16_t car_breaker_audit_retained_total(const CarBreakerAuditState* audit);

/** Retained frame by capture order (0 = oldest), or NULL if out of range. */
const CarBreakerAuditFrame* car_breaker_audit_retained_frame(uint16_t index_from_oldest);

/** Build the bounded replay plan for the running test. */
void car_breaker_audit_build_plan(CarBreakerAuditState* audit);

/** Frame to replay at plan position idx, or NULL when idx is out of range. */
const CarBreakerAuditFrame* car_breaker_audit_plan_frame(const CarBreakerAuditState* audit, uint8_t idx);

/** Append a line to the audit log (caller holds the app lock). */
void car_breaker_audit_log(CarBreakerAuditState* audit, const char* fmt, ...);

/** Write the audit log to CAR_BREAKER_AUDIT_DIR/audit_<epoch>.txt. */
bool car_breaker_audit_export_log(CarBreakerAuditState* audit, uint32_t epoch);

/** Export one retained frame as a stock-compatible .sub RAW file. */
bool car_breaker_audit_export_sub(
    const CarBreakerAuditFrame* frame,
    uint32_t frequency,
    const uint8_t* regs,
    size_t regs_size,
    const char* path);

/** Per-test human-readable names and guidance. */
const char* car_breaker_audit_test_name(CarBreakerAuditTest test);
const char* car_breaker_audit_test_brief(CarBreakerAuditTest test);
const char* car_breaker_audit_verdict_name(CarBreakerAuditVerdict verdict);

const CarBreakerKnownIssue* car_breaker_audit_known_issue(uint8_t index);
uint8_t car_breaker_audit_known_issue_count(void);
