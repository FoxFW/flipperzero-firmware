/**
 * @file nfc_checker_compare.h
 * @brief Emulation Success Score calculation.
 *
 * The score answers a single question: if this clone were presented to a real
 * reader, how likely is it to be accepted as the original tag?
 *
 * Each check carries a weight. Checks that cannot be evaluated for a given pair
 * of tags (for example memory when the clone was read live, or ATQA/SAK on a
 * non ISO14443-3A tag) are dropped from the total, so the score always spans
 * 0..100 instead of being diluted by unavailable data.
 */
#pragma once

#include "nfc_checker_tag.h"

#define NFC_CHECKER_LABEL_SIZE (16)
#define NFC_CHECKER_VALUE_SIZE (24)
#define NFC_CHECKER_DETAIL_SIZE (64)

/** Weight of every check when it is applicable, in points. */
#define NFC_CHECKER_WEIGHT_UID (40)
#define NFC_CHECKER_WEIGHT_PROTOCOL (15)
#define NFC_CHECKER_WEIGHT_ATQA (10)
#define NFC_CHECKER_WEIGHT_SAK (10)
#define NFC_CHECKER_WEIGHT_MEMORY (20)
#define NFC_CHECKER_WEIGHT_KEYS (5)

typedef enum {
    NfcCheckerCheckStatusOk = 0,
    NfcCheckerCheckStatusWarn,
    NfcCheckerCheckStatusFail,
    NfcCheckerCheckStatusInfo,
} NfcCheckerCheckStatus;

typedef enum {
    NfcCheckerCheckUid = 0,
    NfcCheckerCheckProtocol,
    NfcCheckerCheckAtqa,
    NfcCheckerCheckSak,
    NfcCheckerCheckMemory,
    NfcCheckerCheckKeys,
    NfcCheckerCheckSummary,
    NfcCheckerCheckCount,
} NfcCheckerCheckId;

typedef struct {
    NfcCheckerCheckId id;
    NfcCheckerCheckStatus status;
    uint8_t weight; /**< Points this check is worth (0 = not applicable). */
    uint8_t earned; /**< Points actually awarded. */
    char label[NFC_CHECKER_LABEL_SIZE];
    char value[NFC_CHECKER_VALUE_SIZE];
    char detail[NFC_CHECKER_DETAIL_SIZE];
} NfcCheckerCheck;

typedef enum {
    NfcCheckerVerdictAccepted = 0,
    NfcCheckerVerdictLikely,
    NfcCheckerVerdictRisky,
    NfcCheckerVerdictRejected,
} NfcCheckerVerdict;

typedef struct {
    uint8_t score; /**< 0..100, the Emulation Success Score. */
    uint16_t weight_total;
    uint16_t weight_earned;
    uint8_t warned;
    uint8_t failed;
    NfcCheckerVerdict verdict;
    uint8_t check_count;
    NfcCheckerCheck checks[NfcCheckerCheckCount];
} NfcCheckerReport;

/**
 * @brief Compare a baseline tag against a clone and produce the score report.
 *
 * @param[in]  baseline  reference tag (what the reader expects)
 * @param[in]  target    emulated/cloned tag (what the reader would receive)
 * @param[out] report    filled with checks, verdict and score
 */
void nfc_checker_compare(
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target,
    NfcCheckerReport* report);

/** Short text for a check status ("MATCH", "WARN", ...). */
const char* nfc_checker_check_status_text(NfcCheckerCheckStatus status);

/** Verdict sentence used by the results screen. */
const char* nfc_checker_verdict_text(NfcCheckerVerdict verdict);
