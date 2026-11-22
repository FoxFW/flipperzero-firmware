#include "nfc_checker_compare.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define NFC_CHECKER_MIN(a, b) (((a) < (b)) ? (a) : (b))

static NfcCheckerCheck* checker_check(
    NfcCheckerReport* report,
    NfcCheckerCheckId id,
    const char* label,
    uint8_t weight) {
    NfcCheckerCheck* check = &report->checks[id];
    memset(check, 0, sizeof(NfcCheckerCheck));
    check->id = id;
    check->status = NfcCheckerCheckStatusInfo;
    check->weight = weight;
    snprintf(check->label, sizeof(check->label), "%s", label);
    snprintf(check->value, sizeof(check->value), "-");
    return check;
}

static void checker_set(NfcCheckerCheck* check, NfcCheckerCheckStatus status, const char* value) {
    check->status = status;
    snprintf(check->value, sizeof(check->value), "%s", value);
}

static void checker_detail(NfcCheckerCheck* check, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(check->detail, sizeof(check->detail), format, args);
    va_end(args);
}

static void checker_award(NfcCheckerCheck* check, uint32_t part, uint32_t total) {
    if(total == 0) {
        check->earned = 0;
        return;
    }
    check->earned = (uint8_t)(((uint32_t)check->weight * part + total / 2) / total);
    if(check->earned > check->weight) {
        check->earned = check->weight;
    }
}

static void compare_uid(NfcCheckerReport* report, const NfcCheckerTag* baseline, const NfcCheckerTag* target) {
    NfcCheckerCheck* check =
        checker_check(report, NfcCheckerCheckUid, "UID", NFC_CHECKER_WEIGHT_UID);

    if(!baseline->has_uid) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "Baseline has no UID");
        return;
    }
    if(!target->has_uid) {
        checker_set(check, NfcCheckerCheckStatusFail, "MISSING");
        checker_detail(check, "Target has no UID");
        return;
    }

    if(nfc_checker_tag_uid_equal(baseline, target)) {
        check->earned = check->weight;
        checker_set(check, NfcCheckerCheckStatusOk, "MATCH");
        checker_detail(check, "UID is identical (%u bytes)", baseline->uid_len);
        return;
    }

    checker_set(check, NfcCheckerCheckStatusFail, "MISMATCH");
    if(baseline->uid_len != target->uid_len) {
        checker_detail(
            check, "Length %u != %u", (unsigned)baseline->uid_len, (unsigned)target->uid_len);
        return;
    }

    for(uint8_t i = 0; i < baseline->uid_len; i++) {
        if(baseline->uid[i] != target->uid[i]) {
            checker_detail(
                check,
                "Byte %u: %02X != %02X",
                (unsigned)i,
                baseline->uid[i],
                target->uid[i]);
            break;
        }
    }
}

static void compare_protocol(
    NfcCheckerReport* report,
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target) {
    NfcCheckerCheck* check =
        checker_check(report, NfcCheckerCheckProtocol, "PROTOCOL", NFC_CHECKER_WEIGHT_PROTOCOL);

    if(baseline->protocol == NfcProtocolInvalid || target->protocol == NfcProtocolInvalid) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "Protocol is unknown");
        return;
    }

    if(baseline->protocol == target->protocol) {
        check->earned = check->weight;
        checker_set(check, NfcCheckerCheckStatusOk, "MATCH");
        checker_detail(check, "%s", nfc_checker_tag_protocol_name(baseline));
        return;
    }

    const char* target_name = nfc_checker_tag_protocol_name(target);
    if(baseline->has_atqa_sak && target->has_atqa_sak) {
        /* Both answer ISO14443-3A: readers usually only look at UID/ATQA/SAK,
         * so a different product in the same family may still work. */
        checker_award(check, 1, 3);
        checker_set(check, NfcCheckerCheckStatusWarn, "DIFFERENT");
        checker_detail(check, "Target: %s", target_name);
    } else {
        checker_set(check, NfcCheckerCheckStatusFail, "MISMATCH");
        checker_detail(check, "Target: %s", target_name);
    }
}

static void compare_atqa(
    NfcCheckerReport* report,
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target) {
    NfcCheckerCheck* check =
        checker_check(report, NfcCheckerCheckAtqa, "ATQA", NFC_CHECKER_WEIGHT_ATQA);

    if(!baseline->has_atqa_sak || !target->has_atqa_sak) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "Not an ISO14443-3A tag");
        return;
    }

    if(baseline->atqa[0] == target->atqa[0] && baseline->atqa[1] == target->atqa[1]) {
        check->earned = check->weight;
        checker_set(check, NfcCheckerCheckStatusOk, "MATCH");
        checker_detail(check, "ATQA %02X%02X", baseline->atqa[0], baseline->atqa[1]);
    } else {
        checker_set(check, NfcCheckerCheckStatusFail, "MISMATCH");
        checker_detail(
            check,
            "%02X%02X != %02X%02X",
            baseline->atqa[0],
            baseline->atqa[1],
            target->atqa[0],
            target->atqa[1]);
    }
}

static void compare_sak(
    NfcCheckerReport* report,
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target) {
    NfcCheckerCheck* check =
        checker_check(report, NfcCheckerCheckSak, "SAK", NFC_CHECKER_WEIGHT_SAK);

    if(!baseline->has_atqa_sak || !target->has_atqa_sak) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "Not an ISO14443-3A tag");
        return;
    }

    if(baseline->sak == target->sak) {
        check->earned = check->weight;
        checker_set(check, NfcCheckerCheckStatusOk, "MATCH");
        checker_detail(check, "SAK %02X", baseline->sak);
    } else {
        checker_set(check, NfcCheckerCheckStatusFail, "MISMATCH");
        checker_detail(check, "%02X != %02X", baseline->sak, target->sak);
    }
}

/* MIFARE Classic 1K/2K/4K: sectors 0-31 hold 4 blocks, 32-39 hold 16. */
static uint8_t compare_sector_by_block(uint16_t block) {
    if(block < 128) {
        return (uint8_t)(block / 4);
    }
    return (uint8_t)(32 + (block - 128) / 16);
}

static void compare_memory_classic(
    NfcCheckerCheck* check,
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target) {
    uint16_t compared = 0;
    uint16_t matched = 0;
    uint16_t missing = 0;
    uint16_t different = 0;
    uint16_t first_bad_block = 0;
    bool have_bad_block = false;

    uint16_t blocks = NFC_CHECKER_MIN(baseline->memory.block_count, target->memory.block_count);
    blocks = NFC_CHECKER_MIN(blocks, NFC_CHECKER_CLASSIC_MAX_BLOCKS);

    for(uint16_t block = 0; block < blocks; block++) {
        /* Only judge what the reference actually contains. */
        if(!nfc_checker_tag_block_read(baseline, block)) {
            continue;
        }
        compared++;
        if(!nfc_checker_tag_block_read(target, block)) {
            missing++;
            if(!have_bad_block) {
                first_bad_block = block;
                have_bad_block = true;
            }
            continue;
        }
        if(memcmp(
               baseline->memory.blocks[block], target->memory.blocks[block], NFC_CHECKER_CLASSIC_BLOCK_SIZE) == 0) {
            matched++;
        } else {
            different++;
            if(!have_bad_block) {
                first_bad_block = block;
                have_bad_block = true;
            }
        }
    }

    if(compared == 0) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "No sectors in reference");
        return;
    }

    checker_award(check, matched, compared);
    uint8_t percent = (uint8_t)(((uint32_t)matched * 100 + compared / 2) / compared);
    snprintf(check->value, sizeof(check->value), "%u%%", percent);

    if(matched == compared) {
        if(baseline->memory.block_count != target->memory.block_count) {
            checker_set(check, NfcCheckerCheckStatusWarn, check->value);
            checker_detail(
                check,
                "%u != %u blocks",
                (unsigned)baseline->memory.block_count,
                (unsigned)target->memory.block_count);
        } else {
            checker_set(check, NfcCheckerCheckStatusOk, check->value);
            checker_detail(check, "All %u blocks match", compared);
        }
    } else if(percent >= 95) {
        checker_set(check, NfcCheckerCheckStatusWarn, check->value);
        checker_detail(check, "%u of %u blocks differ", different + missing, compared);
    } else {
        checker_set(check, NfcCheckerCheckStatusFail, check->value);
        if(different == 0 && missing > 0) {
            checker_detail(
                check,
                "Sector %u not readable",
                (unsigned)compare_sector_by_block(first_bad_block));
        } else {
            checker_detail(check, "Block %u differs", (unsigned)first_bad_block);
        }
    }
}

static void compare_memory_ultralight(
    NfcCheckerCheck* check,
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target) {
    uint16_t compared = 0;
    uint16_t matched = 0;
    uint16_t first_bad_page = 0;
    bool have_bad_page = false;

    uint16_t pages = NFC_CHECKER_MIN(baseline->memory.pages_total, baseline->memory.pages_read);
    pages = NFC_CHECKER_MIN(pages, target->memory.pages_read);
    pages = NFC_CHECKER_MIN(pages, NFC_CHECKER_ULTRALIGHT_MAX_PAGES);

    for(uint16_t page = 0; page < pages; page++) {
        compared++;
        if(memcmp(
               baseline->memory.pages[page],
               target->memory.pages[page],
               NFC_CHECKER_ULTRALIGHT_PAGE_SIZE) == 0) {
            matched++;
        } else if(!have_bad_page) {
            first_bad_page = page;
            have_bad_page = true;
        }
    }

    if(compared == 0) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "No pages in reference");
        return;
    }

    checker_award(check, matched, compared);
    uint8_t percent = (uint8_t)(((uint32_t)matched * 100 + compared / 2) / compared);
    snprintf(check->value, sizeof(check->value), "%u%%", percent);

    if(matched == compared) {
        if(baseline->memory.pages_total != target->memory.pages_total) {
            checker_set(check, NfcCheckerCheckStatusWarn, check->value);
            checker_detail(
                check,
                "%u != %u pages",
                (unsigned)baseline->memory.pages_total,
                (unsigned)target->memory.pages_total);
        } else {
            checker_set(check, NfcCheckerCheckStatusOk, check->value);
            checker_detail(check, "All %u pages match", compared);
        }
    } else if(percent >= 95) {
        checker_set(check, NfcCheckerCheckStatusWarn, check->value);
        checker_detail(check, "%u of %u pages differ", compared - matched, compared);
    } else {
        checker_set(check, NfcCheckerCheckStatusFail, check->value);
        checker_detail(check, "Page %u differs", (unsigned)first_bad_page);
    }
}

static void compare_memory(
    NfcCheckerReport* report,
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target) {
    NfcCheckerCheck* check =
        checker_check(report, NfcCheckerCheckMemory, "MEMORY", NFC_CHECKER_WEIGHT_MEMORY);

    if(baseline->memory.kind == NfcCheckerMemoryNone ||
       target->memory.kind == NfcCheckerMemoryNone) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "Live read: no memory image");
        return;
    }

    if(baseline->memory.kind != target->memory.kind) {
        checker_set(check, NfcCheckerCheckStatusFail, "MISMATCH");
        checker_detail(check, "Different tag families");
        return;
    }

    if(baseline->memory.kind == NfcCheckerMemoryClassic) {
        compare_memory_classic(check, baseline, target);
    } else {
        compare_memory_ultralight(check, baseline, target);
    }
}

static void compare_keys(
    NfcCheckerReport* report,
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target) {
    NfcCheckerCheck* check =
        checker_check(report, NfcCheckerCheckKeys, "KEYS", NFC_CHECKER_WEIGHT_KEYS);

    if(!nfc_checker_tag_has_classic(baseline) || !nfc_checker_tag_has_classic(target)) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "No MIFARE Classic keys");
        return;
    }

    uint16_t known = 0;
    uint16_t matching = 0;
    uint8_t first_missing_sector = 0;
    bool have_missing = false;
    bool missing_key_b = false;
    bool have_differing = false;
    uint8_t first_differing_sector = 0;
    bool differing_key_b = false;

    uint8_t sectors =
        NFC_CHECKER_MIN(baseline->memory.sector_count, target->memory.sector_count);
    sectors = NFC_CHECKER_MIN(sectors, NFC_CHECKER_CLASSIC_MAX_SECTORS);

    for(uint8_t sector = 0; sector < sectors; sector++) {
        for(uint8_t type = 0; type < 2; type++) {
            bool key_b = type == 1;
            const uint8_t* reference = nfc_checker_tag_key(baseline, sector, key_b);
            if(!reference) {
                continue;
            }
            known++;
            const uint8_t* clone = nfc_checker_tag_key(target, sector, key_b);
            if(!clone) {
                if(!have_missing) {
                    have_missing = true;
                    first_missing_sector = sector;
                    missing_key_b = key_b;
                }
            } else if(memcmp(reference, clone, NFC_CHECKER_CLASSIC_KEY_SIZE) == 0) {
                matching++;
            } else if(!have_differing) {
                have_differing = true;
                first_differing_sector = sector;
                differing_key_b = key_b;
            }
        }
    }

    if(known == 0) {
        check->weight = 0;
        checker_set(check, NfcCheckerCheckStatusInfo, "N/A");
        checker_detail(check, "No keys known in reference");
        return;
    }

    checker_award(check, matching, known);
    snprintf(check->value, sizeof(check->value), "%u/%u", matching, known);

    if(matching == known) {
        checker_set(check, NfcCheckerCheckStatusOk, check->value);
        checker_detail(check, "All %u known keys match", known);
    } else if((uint32_t)matching * 100 >= (uint32_t)known * 80) {
        checker_set(check, NfcCheckerCheckStatusWarn, check->value);
        if(have_missing) {
            checker_detail(
                check,
                "Key %c sector %u missing",
                missing_key_b ? 'B' : 'A',
                (unsigned)first_missing_sector);
        } else {
            checker_detail(
                check,
                "Key %c sector %u differs",
                differing_key_b ? 'B' : 'A',
                (unsigned)first_differing_sector);
        }
    } else {
        checker_set(check, NfcCheckerCheckStatusFail, check->value);
        if(have_missing) {
            checker_detail(
                check,
                "Key %c sector %u missing",
                missing_key_b ? 'B' : 'A',
                (unsigned)first_missing_sector);
        } else {
            checker_detail(
                check,
                "Key %c sector %u differs",
                differing_key_b ? 'B' : 'A',
                (unsigned)first_differing_sector);
        }
    }
}

const char* nfc_checker_check_status_text(NfcCheckerCheckStatus status) {
    switch(status) {
    case NfcCheckerCheckStatusOk:
        return "MATCH";
    case NfcCheckerCheckStatusWarn:
        return "WARN";
    case NfcCheckerCheckStatusFail:
        return "MISMATCH";
    case NfcCheckerCheckStatusInfo:
    default:
        return "N/A";
    }
}

const char* nfc_checker_verdict_text(NfcCheckerVerdict verdict) {
    switch(verdict) {
    case NfcCheckerVerdictAccepted:
        return "Reader will accept it";
    case NfcCheckerVerdictLikely:
        return "Probably accepted";
    case NfcCheckerVerdictRisky:
        return "May be rejected";
    case NfcCheckerVerdictRejected:
    default:
        return "Reader will reject it";
    }
}

void nfc_checker_compare(
    const NfcCheckerTag* baseline,
    const NfcCheckerTag* target,
    NfcCheckerReport* report) {
    furi_assert(baseline);
    furi_assert(target);
    furi_assert(report);

    memset(report, 0, sizeof(NfcCheckerReport));

    compare_uid(report, baseline, target);
    compare_protocol(report, baseline, target);
    compare_atqa(report, baseline, target);
    compare_sak(report, baseline, target);
    compare_memory(report, baseline, target);
    compare_keys(report, baseline, target);

    for(uint8_t index = 0; index < NfcCheckerCheckCount; index++) {
        const NfcCheckerCheck* check = &report->checks[index];
        report->weight_total += check->weight;
        report->weight_earned += check->earned;
        if(check->status == NfcCheckerCheckStatusFail) {
            report->failed++;
        } else if(check->status == NfcCheckerCheckStatusWarn) {
            report->warned++;
        }
    }
    if(report->weight_earned > report->weight_total) {
        report->weight_earned = report->weight_total;
    }

    if(report->weight_total > 0) {
        report->score = (uint8_t)(((uint32_t)report->weight_earned * 100 +
                                   report->weight_total / 2) /
                                  report->weight_total);
    } else {
        report->score = 0;
    }

    if(report->score >= 95 && report->failed == 0) {
        report->verdict = NfcCheckerVerdictAccepted;
    } else if(report->score >= 80) {
        report->verdict = NfcCheckerVerdictLikely;
    } else if(report->score >= 50) {
        report->verdict = NfcCheckerVerdictRisky;
    } else {
        report->verdict = NfcCheckerVerdictRejected;
    }

    NfcCheckerCheck* summary =
        checker_check(report, NfcCheckerCheckSummary, "VERDICT", 0);
    switch(report->verdict) {
    case NfcCheckerVerdictAccepted:
        checker_set(summary, NfcCheckerCheckStatusOk, "ACCEPTED");
        break;
    case NfcCheckerVerdictLikely:
        checker_set(summary, NfcCheckerCheckStatusOk, "LIKELY");
        break;
    case NfcCheckerVerdictRisky:
        checker_set(summary, NfcCheckerCheckStatusWarn, "RISKY");
        break;
    default:
        checker_set(summary, NfcCheckerCheckStatusFail, "REJECTED");
        break;
    }
    checker_detail(summary, "%s", nfc_checker_verdict_text(report->verdict));

    report->check_count = NfcCheckerCheckCount;
}
