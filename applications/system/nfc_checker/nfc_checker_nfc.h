/**
 * @file nfc_checker_nfc.h
 * @brief NFC helpers: live reading, .nfc file loading and saving.
 *
 * Live reads work like the stock NFC reader: NfcScanner reports which protocol
 * answered, then a poller for that protocol (14443-3A, 14443-3B, ISO15693,
 * MIFARE, ...) is started and the reader waits until the poller data holds a
 * usable UID. The base pollers are used as fallbacks if the specific one does
 * not produce a UID. This gives UID - plus ATQA/SAK on 14443-3A tags - for any
 * protocol the firmware can read. Full memory images (sectors, keys, pages)
 * come from .nfc dumps loaded from /nfc.
 */
#pragma once

#include <furi.h>
#include <nfc/nfc_device.h>

#include "nfc_checker_tag.h"

#define NFC_CHECKER_FOLDER "/ext/nfc"
#define NFC_CHECKER_FILE_EXTENSION ".nfc"
#define NFC_CHECKER_SCAN_TIMEOUT_MS (3000)
/** Per protocol attempt; the reader usually succeeds on the first one. */
#define NFC_CHECKER_ACTIVATE_TIMEOUT_MS (900)
#define NFC_CHECKER_POLL_PERIOD_MS (25)
/** Gap between the two UID samples used to confirm a poller is done. */
#define NFC_CHECKER_CAPTURE_PERIOD_MS (100)

typedef enum {
    NfcCheckerReadOk = 0,
    NfcCheckerReadNoTag,
    NfcCheckerReadUnsupported,
    NfcCheckerReadFailed,
    NfcCheckerReadAborted,
} NfcCheckerReadResult;

typedef struct NfcCheckerReader NfcCheckerReader;

/** Called from the reader thread once the read has finished. */
typedef void (*NfcCheckerReaderCallback)(NfcCheckerReadResult result, void* context);

NfcCheckerReader* nfc_checker_reader_alloc(void);

void nfc_checker_reader_free(NfcCheckerReader* reader);

/**
 * @brief Start a live read.
 *
 * The tag snapshot and the NfcDevice are written before the callback runs.
 * A previous read must have been joined.
 */
void nfc_checker_reader_start(
    NfcCheckerReader* reader,
    NfcCheckerTag* tag,
    NfcDevice* device,
    NfcCheckerReaderCallback callback,
    void* context);

bool nfc_checker_reader_is_running(NfcCheckerReader* reader);

/** Ask the running read to stop. The completion callback still fires. */
void nfc_checker_reader_abort(NfcCheckerReader* reader);

/** Wait for the worker thread to finish and release it. */
void nfc_checker_reader_join(NfcCheckerReader* reader);

/** Human readable reason for the read result, for popups. */
const char* nfc_checker_read_result_text(NfcCheckerReadResult result);

/**
 * @brief Protocol name reported by the scanner for the last read.
 *
 * Kept even when the read failed, so the UI can say what answered. Never NULL,
 * empty when nothing was detected.
 */
const char* nfc_checker_reader_detected_name(NfcCheckerReader* reader);

/** Load a .nfc file into a snapshot. */
bool nfc_checker_load_file(const char* path, NfcDevice* device, NfcCheckerTag* tag);

/** Save the NfcDevice contents into /nfc. */
bool nfc_checker_save_file(const char* path, NfcDevice* device);

/**
 * @brief Build a collision free path in /nfc for the given tag.
 *
 * Produces /ext/nfc/NFCCHK_<UID>.nfc, adding a numeric suffix if needed.
 */
void nfc_checker_build_save_path(const NfcCheckerTag* tag, FuriString* path);
