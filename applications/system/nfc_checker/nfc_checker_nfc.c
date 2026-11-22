#include "nfc_checker_nfc.h"

#include <stdio.h>
#include <string.h>

#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_scanner.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <storage/storage.h>

#define NFC_CHECKER_READER_NAME_SIZE (24)
#define NFC_CHECKER_ATTEMPT_MAX (4)

struct NfcCheckerReader {
    FuriThread* thread;
    NfcCheckerTag* tag;
    NfcDevice* device;
    NfcCheckerReaderCallback callback;
    void* callback_context;

    volatile bool running;
    volatile bool abort_requested;
    volatile bool detected;
    volatile bool card_ready;
    volatile bool captured;
    volatile NfcProtocol protocol;

    NfcPoller* poller; /**< Set while an attempt is running. */
    NfcProtocol poller_protocol;
    char detected_name[NFC_CHECKER_READER_NAME_SIZE];
};

/**
 * @brief Protocols whose poller data this application can copy safely.
 *
 * MIFARE Classic and Ultralight hold their data in plain structures. Higher
 * level protocols (ISO14443-4A, DESFire, Plus) keep heap allocated members
 * (ATS/historical bytes, files) that the poller thread re-initialises while it
 * runs, so copying them was what froze the device on phone-emulated tags.
 * Those tags are read through the base protocols instead, which carry the same
 * UID/ATQA/SAK.
 */
static const NfcProtocol nfc_checker_preferred_protocols[] = {
    NfcProtocolMfClassic,
    NfcProtocolMfUltralight,
};

static bool nfc_checker_protocol_is_base(NfcProtocol protocol) {
    return protocol == NfcProtocolIso14443_3a || protocol == NfcProtocolIso14443_3b ||
           protocol == NfcProtocolIso15693_3;
}

static NfcProtocol nfc_checker_pick_protocol(const NfcProtocol* protocols, size_t count) {
    for(size_t i = 0; i < COUNT_OF(nfc_checker_preferred_protocols); i++) {
        for(size_t j = 0; j < count; j++) {
            if(protocols[j] == nfc_checker_preferred_protocols[i]) {
                return protocols[j];
            }
        }
    }

    /* A base protocol answers for itself: 14443-3A, 14443-3B, ISO15693. */
    for(size_t j = 0; j < count; j++) {
        if(nfc_checker_protocol_is_base(protocols[j])) {
            return protocols[j];
        }
    }

    /* Anything else (ISO14443-4A, DESFire, Plus, SLIX, ...): start on the
     * 14443-3A layer. The base pollers in the attempt list pick up the rest. */
    return (count > 0) ? NfcProtocolIso14443_3a : NfcProtocolInvalid;
}

static void nfc_checker_scanner_callback(NfcScannerEvent event, void* context) {
    NfcCheckerReader* reader = context;

    if(event.type != NfcScannerEventTypeDetected) {
        return;
    }
    if(event.data.protocol_num == 0) {
        return;
    }

    NfcProtocol protocol =
        nfc_checker_pick_protocol(event.data.protocols, event.data.protocol_num);

    const char* name =
        (protocol == NfcProtocolInvalid) ? NULL : nfc_device_get_protocol_name(protocol);
    snprintf(reader->detected_name, sizeof(reader->detected_name), "%s", name ? name : "Unknown");

    reader->protocol = protocol;
    reader->detected = true;
}

/**
 * @brief Take a UID-only snapshot from the poller thread.
 *
 * Safe to call here: the poller is between transactions while its callback
 * runs. It does not delay, so the poller thread is never blocked.
 */
static bool nfc_checker_reader_snapshot_ready(NfcCheckerReader* reader, NfcPoller* poller) {
    const NfcDeviceData* data = nfc_poller_get_data(poller);
    if(data == NULL) {
        return false;
    }

    nfc_device_set_data(reader->device, reader->poller_protocol, data);

    size_t uid_len = 0;
    const uint8_t* uid = nfc_device_get_uid(reader->device, &uid_len);
    if(uid == NULL || uid_len == 0 || uid_len > NFC_CHECKER_UID_MAX_SIZE) {
        return false;
    }

    for(size_t i = 0; i < uid_len; i++) {
        if(uid[i] != 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Poller callback: snapshot the tag and stop the poller.
 *
 * Also notices a completed ISO14443-3A activation, which lets the reader thread
 * trust a single UID sample.
 *
 * The poller is stopped from here, not by the reader thread, as soon as the data
 * carries a UID: once a tag is activated a base protocol keeps re-emitting its
 * ready event without any hardware work, and that loop runs on the highest
 * priority thread. Waiting for another thread to stop it starves the rest of
 * the system and freezes the device (phones emulating a tag are the worst case,
 * since they present ISO14443-4A and are read on the 14443-3A layer).
 */
static NfcCommand nfc_checker_poller_callback(NfcGenericEvent event, void* context) {
    NfcCheckerReader* reader = context;

    if(event.protocol == NfcProtocolIso14443_3a && event.event_data) {
        const Iso14443_3aPollerEvent* poller_event = event.event_data;
        if(poller_event->type == Iso14443_3aPollerEventTypeReady) {
            reader->card_ready = true;
        }
    }

    if(!reader->captured && reader->poller) {
        if(nfc_checker_reader_snapshot_ready(reader, reader->poller)) {
            reader->captured = true;
        }
    }

    return reader->captured ? NfcCommandStop : NfcCommandContinue;
}

/**
 * @brief Copy the poller data into the device and read its UID.
 *
 * @return true if the data holds a non-empty, non-zero UID.
 */
static bool nfc_checker_reader_uid_probe(
    NfcCheckerReader* reader,
    NfcPoller* poller,
    uint8_t* uid,
    size_t* uid_len) {
    const NfcDeviceData* data = nfc_poller_get_data(poller);
    if(data == NULL) {
        return false;
    }

    nfc_device_set_data(reader->device, reader->poller_protocol, data);

    size_t length = 0;
    const uint8_t* value = nfc_device_get_uid(reader->device, &length);
    if(value == NULL || length == 0 || length > NFC_CHECKER_UID_MAX_SIZE) {
        return false;
    }

    for(size_t i = 0; i < length; i++) {
        if(value[i] != 0) {
            memcpy(uid, value, length);
            *uid_len = length;
            return true;
        }
    }

    return false;
}

/**
 * @brief Try to capture the tag data currently held by the poller.
 *
 * The poller thread keeps writing into its data while we look at it, so unless
 * the protocol told us it is done, the UID has to stay the same for a moment
 * before the snapshot is trusted.
 */
static bool nfc_checker_reader_capture(NfcCheckerReader* reader, NfcPoller* poller) {
    uint8_t first[NFC_CHECKER_UID_MAX_SIZE];
    size_t first_len = 0;
    if(!nfc_checker_reader_uid_probe(reader, poller, first, &first_len)) {
        return false;
    }

    if(reader->card_ready) {
        /* ISO14443-3A reported a finished activation, the data is complete. */
        return true;
    }

    furi_delay_ms(NFC_CHECKER_CAPTURE_PERIOD_MS);

    uint8_t second[NFC_CHECKER_UID_MAX_SIZE];
    size_t second_len = 0;
    if(!nfc_checker_reader_uid_probe(reader, poller, second, &second_len)) {
        return false;
    }

    return (first_len == second_len) && (memcmp(first, second, first_len) == 0);
}

static bool nfc_checker_reader_attempt(
    NfcCheckerReader* reader,
    Nfc* nfc,
    NfcProtocol protocol) {
    NfcPoller* poller = nfc_poller_alloc(nfc, protocol);
    if(poller == NULL) {
        return false;
    }

    reader->poller = poller;
    reader->poller_protocol = protocol;
    reader->card_ready = false;
    reader->captured = false;

    nfc_poller_start(poller, nfc_checker_poller_callback, reader);

    bool captured = false;
    uint32_t start = furi_get_tick();

    while((furi_get_tick() - start) < NFC_CHECKER_ACTIVATE_TIMEOUT_MS) {
        if(reader->abort_requested) {
            break;
        }
        if(reader->captured) {
            captured = true;
            break;
        }
        if(nfc_checker_reader_capture(reader, poller)) {
            captured = true;
            break;
        }
        furi_delay_ms(NFC_CHECKER_POLL_PERIOD_MS);
    }

    nfc_poller_stop(poller);
    reader->poller = NULL;
    nfc_poller_free(poller);

    return captured || reader->captured;
}

/**
 * @brief Poll order: detected protocol first, then the base protocols.
 *
 * The base pollers only need anti-collision, so they are a safety net for tags
 * whose specific poller needs extra steps to report a UID.
 */
static uint8_t nfc_checker_reader_build_attempts(
    const NfcCheckerReader* reader,
    NfcProtocol* attempts) {
    static const NfcProtocol fallbacks[] = {
        NfcProtocolIso14443_3a,
        NfcProtocolIso15693_3,
        NfcProtocolIso14443_3b,
    };

    uint8_t count = 0;

    if(reader->protocol != NfcProtocolInvalid) {
        attempts[count++] = reader->protocol;
    }

    for(size_t i = 0; i < COUNT_OF(fallbacks); i++) {
        bool duplicate = false;
        for(uint8_t j = 0; j < count; j++) {
            if(attempts[j] == fallbacks[i]) {
                duplicate = true;
            }
        }
        if(!duplicate && count < NFC_CHECKER_ATTEMPT_MAX) {
            attempts[count++] = fallbacks[i];
        }
    }

    return count;
}

static int32_t nfc_checker_reader_thread(void* context) {
    NfcCheckerReader* reader = context;

    reader->protocol = NfcProtocolInvalid;
    reader->detected = false;
    reader->card_ready = false;
    reader->captured = false;
    reader->poller = NULL;
    reader->poller_protocol = NfcProtocolInvalid;
    reader->detected_name[0] = '\0';

    NfcCheckerReadResult result = NfcCheckerReadFailed;

    Nfc* nfc = nfc_alloc();
    NfcScanner* scanner = nfc_scanner_alloc(nfc);

    nfc_scanner_start(scanner, nfc_checker_scanner_callback, reader);

    uint32_t elapsed = 0;
    while(elapsed < NFC_CHECKER_SCAN_TIMEOUT_MS) {
        if(reader->abort_requested || reader->detected) {
            break;
        }
        furi_delay_ms(NFC_CHECKER_POLL_PERIOD_MS);
        elapsed += NFC_CHECKER_POLL_PERIOD_MS;
    }
    nfc_scanner_stop(scanner);
    nfc_scanner_free(scanner);

    if(reader->abort_requested) {
        result = NfcCheckerReadAborted;
    } else if(!reader->detected) {
        result = NfcCheckerReadNoTag;
    } else {
        NfcProtocol attempts[NFC_CHECKER_ATTEMPT_MAX];
        uint8_t attempt_count = nfc_checker_reader_build_attempts(reader, attempts);

        bool captured = false;
        for(uint8_t i = 0; i < attempt_count; i++) {
            if(reader->abort_requested) {
                break;
            }
            if(nfc_checker_reader_attempt(reader, nfc, attempts[i])) {
                captured = true;
                break;
            }
        }

        if(reader->abort_requested) {
            result = NfcCheckerReadAborted;
        } else if(!captured) {
            result = NfcCheckerReadUnsupported;
        } else {
            nfc_checker_tag_from_device(reader->tag, reader->device);

            /* Report the protocol that answered, not the base protocol that was
             * used to capture it. */
            if(reader->protocol != NfcProtocolInvalid) {
                reader->tag->protocol = reader->protocol;
            } else {
                reader->tag->protocol = reader->poller_protocol;
            }

            const char* name = (reader->tag->protocol == NfcProtocolInvalid) ?
                                   NULL :
                                   nfc_device_get_protocol_name(reader->tag->protocol);
            snprintf(
                reader->tag->protocol_name,
                sizeof(reader->tag->protocol_name),
                "%s",
                name ? name : "Unknown");
            reader->tag->from_file = false;

            snprintf(
                reader->detected_name,
                sizeof(reader->detected_name),
                "%s",
                reader->tag->protocol_name);
            result = NfcCheckerReadOk;
        }
    }

    nfc_free(nfc);

    reader->running = false;
    if(reader->callback) {
        reader->callback(result, reader->callback_context);
    }

    return (int32_t)result;
}

NfcCheckerReader* nfc_checker_reader_alloc(void) {
    NfcCheckerReader* reader = malloc(sizeof(NfcCheckerReader));
    furi_check(reader);
    memset(reader, 0, sizeof(NfcCheckerReader));
    reader->protocol = NfcProtocolInvalid;
    reader->poller_protocol = NfcProtocolInvalid;
    return reader;
}

void nfc_checker_reader_join(NfcCheckerReader* reader) {
    furi_assert(reader);
    if(reader->thread) {
        furi_thread_join(reader->thread);
        furi_thread_free(reader->thread);
        reader->thread = NULL;
    }
}

void nfc_checker_reader_free(NfcCheckerReader* reader) {
    furi_assert(reader);
    nfc_checker_reader_abort(reader);
    nfc_checker_reader_join(reader);
    free(reader);
}

void nfc_checker_reader_start(
    NfcCheckerReader* reader,
    NfcCheckerTag* tag,
    NfcDevice* device,
    NfcCheckerReaderCallback callback,
    void* context) {
    furi_assert(reader);
    furi_assert(tag);
    furi_assert(device);

    nfc_checker_reader_join(reader);

    reader->tag = tag;
    reader->device = device;
    reader->callback = callback;
    reader->callback_context = context;
    reader->abort_requested = false;
    reader->detected = false;
    reader->card_ready = false;
    reader->captured = false;
    reader->protocol = NfcProtocolInvalid;
    reader->poller = NULL;
    reader->poller_protocol = NfcProtocolInvalid;
    reader->detected_name[0] = '\0';
    reader->running = true;

    nfc_checker_tag_reset(tag);

    reader->thread =
        furi_thread_alloc_ex("NfcCheckerRead", 6 * 1024, nfc_checker_reader_thread, reader);
    furi_thread_start(reader->thread);
}

bool nfc_checker_reader_is_running(NfcCheckerReader* reader) {
    furi_assert(reader);
    return reader->running;
}

void nfc_checker_reader_abort(NfcCheckerReader* reader) {
    furi_assert(reader);
    reader->abort_requested = true;
}

const char* nfc_checker_reader_detected_name(NfcCheckerReader* reader) {
    furi_assert(reader);
    return reader->detected_name;
}

const char* nfc_checker_read_result_text(NfcCheckerReadResult result) {
    switch(result) {
    case NfcCheckerReadOk:
        return "Tag read";
    case NfcCheckerReadNoTag:
        return "No tag found";
    case NfcCheckerReadUnsupported:
        return "Unsupported tag";
    case NfcCheckerReadFailed:
        return "Tag did not respond";
    case NfcCheckerReadAborted:
    default:
        return "Read cancelled";
    }
}

bool nfc_checker_load_file(const char* path, NfcDevice* device, NfcCheckerTag* tag) {
    furi_assert(path);
    furi_assert(device);
    furi_assert(tag);

    nfc_device_clear(device);
    if(!nfc_device_load(device, path)) {
        return false;
    }
    if(!nfc_checker_tag_from_device(tag, device)) {
        return false;
    }

    tag->from_file = true;
    return true;
}

bool nfc_checker_save_file(const char* path, NfcDevice* device) {
    furi_assert(path);
    furi_assert(device);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, NFC_CHECKER_FOLDER);
    furi_record_close(RECORD_STORAGE);

    return nfc_device_save(device, path);
}

void nfc_checker_build_save_path(const NfcCheckerTag* tag, FuriString* path) {
    furi_assert(tag);
    furi_assert(path);

    char uid[3 * NFC_CHECKER_UID_MAX_SIZE] = {0};
    nfc_checker_tag_uid_to_string(tag, uid, sizeof(uid));

    furi_string_printf(path, NFC_CHECKER_FOLDER "/NFCCHK_%s" NFC_CHECKER_FILE_EXTENSION, uid);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, NFC_CHECKER_FOLDER);

    FileInfo info;
    for(uint8_t attempt = 1; attempt < 10; attempt++) {
        if(storage_common_stat(storage, furi_string_get_cstr(path), &info) != FSE_OK) {
            break;
        }
        furi_string_printf(
            path,
            NFC_CHECKER_FOLDER "/NFCCHK_%s_%u" NFC_CHECKER_FILE_EXTENSION,
            uid,
            attempt);
    }
    furi_record_close(RECORD_STORAGE);
}
