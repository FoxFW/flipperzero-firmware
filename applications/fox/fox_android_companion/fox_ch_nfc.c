#include "fox_ch_nfc.h"
#include "fox_watchdog.h" /* kick the dispatch watchdog during long emulation windows */

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_scanner.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_listener.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight_poller.h>

#include <toolbox/protocols/protocol_dict.h>
#include "lfrfid/lfrfid_worker.h"
#include "lfrfid/lfrfid_dict_file.h"
#include "lfrfid/protocols/lfrfid_protocols.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "FoxNfcIO"
#define FOX_NFC_STATUS EXT_PATH("apps_data/fox_companion/nfc_status.txt")

static void fox_nfc_write_status(const char* s) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(storage, EXT_PATH("apps_data/fox_companion"));
    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, FOX_NFC_STATUS, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, s, strlen(s));
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}

static const char* fox_nfc_read_hf(const char* out_path);
static const char* fox_nfc_emu_hf(const char* file_path, uint32_t ms);
static const char* fox_nfc_write_hf(const char* file_path);
static const char* fox_lf_read(const char* out_path);
static const char* fox_lf_emu(const char* file_path, uint32_t ms);
static const char* fox_lf_write(const char* file_path);

const char* fox_nfc_channel(const char* args) {
    if(!args || args[0] == '\0') return "bad-args";

    char mode[12] = {0};
    char path[256] = {0};
    unsigned long ms = 0;
    int n = sscanf(args, "%11s %255s %lu", mode, path, &ms);

    const char* status;
    if(n >= 2 && strcmp(mode, "READ_HF") == 0) {
        status = fox_nfc_read_hf(path);
    } else if(n >= 2 && strcmp(mode, "EMU_HF") == 0) {
        status = fox_nfc_emu_hf(path, ms ? (uint32_t)ms : 30000);
    } else if(n >= 2 && strcmp(mode, "WRITE_HF") == 0) {
        status = fox_nfc_write_hf(path);
    } else if(n >= 2 && strcmp(mode, "READ_LF") == 0) {
        status = fox_lf_read(path);
    } else if(n >= 2 && strcmp(mode, "EMU_LF") == 0) {
        status = fox_lf_emu(path, ms ? (uint32_t)ms : 30000);
    } else if(n >= 2 && strcmp(mode, "WRITE_LF") == 0) {
        status = fox_lf_write(path);
    } else {
        status = "bad-args";
    }

    fox_nfc_write_status(status);
    return status;
}

static NfcCommand fox_listener_cb(NfcGenericEvent event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return NfcCommandContinue;
}

static const char* fox_nfc_emu_hf(const char* file_path, uint32_t ms) {
    NfcDevice* dev = nfc_device_alloc();
    const char* status = "no-tag";
    if(nfc_device_load(dev, file_path)) {
        NfcProtocol protocol = nfc_device_get_protocol(dev);
        const NfcDeviceData* data = nfc_device_get_data(dev, protocol);
        Nfc* nfc = nfc_alloc();
        NfcListener* listener = nfc_listener_alloc(nfc, protocol, data);
        nfc_listener_start(listener, fox_listener_cb, NULL);
        uint32_t start = furi_get_tick();
        while((furi_get_tick() - start) < furi_ms_to_ticks(ms)) {
            fox_watchdog_kick(); // emulation is a legitimate long window, not a hang
            furi_delay_ms(50);
        }
        nfc_listener_stop(listener);
        nfc_listener_free(listener);
        nfc_free(nfc);
        status = "ok";
    } else {
        status = "bad-args";
    }
    nfc_device_free(dev);
    return status;
}

typedef struct {
    FuriEventFlag* flags;
    NfcProtocol protocol;
    bool detected;
} FoxScanCtx;

#define FOX_SCAN_FLAG (1u << 0)

static void fox_scanner_cb(NfcScannerEvent event, void* context) {
    FoxScanCtx* ctx = context;
    if(event.type == NfcScannerEventTypeDetected && event.data.protocol_num > 0 &&
       !ctx->detected) {
        ctx->protocol = event.data.protocols[0];
        ctx->detected = true;
        furi_event_flag_set(ctx->flags, FOX_SCAN_FLAG);
    }
}

static NfcCommand fox_poller_cb(NfcGenericEvent event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return NfcCommandContinue;
}

static const char* fox_nfc_read_hf(const char* out_path) {
    Nfc* nfc = nfc_alloc();
    FoxScanCtx sctx = {.flags = furi_event_flag_alloc(), .protocol = 0, .detected = false};

    NfcScanner* scanner = nfc_scanner_alloc(nfc);
    nfc_scanner_start(scanner, fox_scanner_cb, &sctx);
    furi_event_flag_wait(sctx.flags, FOX_SCAN_FLAG, FuriFlagWaitAny, 3000);
    nfc_scanner_stop(scanner);
    nfc_scanner_free(scanner);

    const char* status = "no-tag";
    if(sctx.detected) {
        NfcPoller* poller = nfc_poller_alloc(nfc, sctx.protocol);
        nfc_poller_start(poller, fox_poller_cb, &sctx);
        furi_delay_ms(1500);
        nfc_poller_stop(poller);
        const NfcDeviceData* data = nfc_poller_get_data(poller);
        NfcDevice* dev = nfc_device_alloc();
        nfc_device_set_data(dev, sctx.protocol, data);
        nfc_poller_free(poller);
        if(nfc_device_save(dev, out_path)) status = "ok";
        else status = "write-failed";
        nfc_device_free(dev);
    }

    furi_event_flag_free(sctx.flags);
    nfc_free(nfc);
    return status;
}

typedef struct {
    FuriEventFlag* flags;
    const MfUltralightData* wdata;
    bool ok;
} FoxMfuWrCtx;

#define FOX_HFW_FLAG (1u << 0)

static NfcCommand fox_mfu_write_cb(NfcGenericEvent event, void* context) {
    FoxMfuWrCtx* ctx = context;
    if(event.protocol != NfcProtocolMfUltralight) return NfcCommandContinue;
    MfUltralightPollerEvent* e = event.event_data;
    switch(e->type) {
    case MfUltralightPollerEventTypeRequestMode:
        e->data->poller_mode = MfUltralightPollerModeWrite;
        return NfcCommandContinue;
    case MfUltralightPollerEventTypeRequestWriteData:
        e->data->write_data = ctx->wdata;
        return NfcCommandContinue;
    case MfUltralightPollerEventTypeWriteSuccess:
        ctx->ok = true;
        furi_event_flag_set(ctx->flags, FOX_HFW_FLAG);
        return NfcCommandStop;
    case MfUltralightPollerEventTypeWriteFail:
        ctx->ok = false;
        furi_event_flag_set(ctx->flags, FOX_HFW_FLAG);
        return NfcCommandStop;
    default:
        return NfcCommandContinue;
    }
}

static const char* fox_nfc_write_hf(const char* file_path) {
    NfcDevice* dev = nfc_device_alloc();
    const char* status = "bad-args";
    if(nfc_device_load(dev, file_path)) {
        NfcProtocol protocol = nfc_device_get_protocol(dev);
        if(protocol != NfcProtocolMfUltralight) {
            status = "not-writable";
        } else {
            const MfUltralightData* wdata = nfc_device_get_data(dev, NfcProtocolMfUltralight);
            Nfc* nfc = nfc_alloc();
            NfcPoller* poller = nfc_poller_alloc(nfc, NfcProtocolMfUltralight);
            FoxMfuWrCtx ctx = {.flags = furi_event_flag_alloc(), .wdata = wdata, .ok = false};
            nfc_poller_start(poller, fox_mfu_write_cb, &ctx);
            uint32_t flags =
                furi_event_flag_wait(ctx.flags, FOX_HFW_FLAG, FuriFlagWaitAny, 8000);
            nfc_poller_stop(poller);
            nfc_poller_free(poller);
            nfc_free(nfc);
            if(flags & FOX_HFW_FLAG) {
                status = ctx.ok ? "ok" : "write-failed";
            } else {
                status = "no-tag";
            }
            furi_event_flag_free(ctx.flags);
        }
    }
    nfc_device_free(dev);
    return status;
}

#define FOX_LF_FLAG (1u << 0)

typedef struct {
    FuriEventFlag* flags;
    ProtocolId protocol;
} FoxLfRxCtx;

static void fox_lf_read_cb(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    FoxLfRxCtx* ctx = context;
    if(result == LFRFIDWorkerReadDone) {
        ctx->protocol = protocol;
        furi_event_flag_set(ctx->flags, FOX_LF_FLAG);
    }
}

typedef struct {
    FuriEventFlag* flags;
    LFRFIDWorkerWriteResult result;
} FoxLfWrCtx;

static void fox_lf_write_cb(LFRFIDWorkerWriteResult result, void* context) {
    FoxLfWrCtx* ctx = context;
    ctx->result = result;
    furi_event_flag_set(ctx->flags, FOX_LF_FLAG);
}

static const char* fox_lf_read(const char* out_path) {
    ProtocolDict* dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    LFRFIDWorker* worker = lfrfid_worker_alloc(dict);
    FoxLfRxCtx ctx = {.flags = furi_event_flag_alloc(), .protocol = PROTOCOL_NO};
    lfrfid_worker_start_thread(worker);
    lfrfid_worker_read_start(worker, LFRFIDWorkerReadTypeAuto, fox_lf_read_cb, &ctx);
    furi_event_flag_wait(ctx.flags, FOX_LF_FLAG, FuriFlagWaitAny, 5000);
    lfrfid_worker_stop(worker);
    lfrfid_worker_stop_thread(worker);

    const char* status = "no-tag";
    if(ctx.protocol != PROTOCOL_NO) {
        status = lfrfid_dict_file_save(dict, ctx.protocol, out_path) ? "ok" : "write-failed";
    }
    lfrfid_worker_free(worker);
    protocol_dict_free(dict);
    furi_event_flag_free(ctx.flags);
    return status;
}

static const char* fox_lf_emu(const char* file_path, uint32_t ms) {
    ProtocolDict* dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    ProtocolId protocol = lfrfid_dict_file_load(dict, file_path);
    const char* status;
    if(protocol == PROTOCOL_NO) {
        status = "bad-args";
    } else {
        LFRFIDWorker* worker = lfrfid_worker_alloc(dict);
        lfrfid_worker_start_thread(worker);
        lfrfid_worker_emulate_start(worker, (LFRFIDProtocol)protocol);
        uint32_t start = furi_get_tick();
        while((furi_get_tick() - start) < furi_ms_to_ticks(ms)) {
            fox_watchdog_kick(); // emulation is a legitimate long window, not a hang
            furi_delay_ms(50);
        }
        lfrfid_worker_stop(worker);
        lfrfid_worker_stop_thread(worker);
        lfrfid_worker_free(worker);
        status = "ok";
    }
    protocol_dict_free(dict);
    return status;
}

static const char* fox_lf_write(const char* file_path) {
    ProtocolDict* dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    ProtocolId protocol = lfrfid_dict_file_load(dict, file_path);
    const char* status;
    if(protocol == PROTOCOL_NO) {
        status = "bad-args";
    } else {
        LFRFIDWorker* worker = lfrfid_worker_alloc(dict);
        FoxLfWrCtx ctx = {.flags = furi_event_flag_alloc(), .result = LFRFIDWorkerWriteTooLongToWrite};
        lfrfid_worker_start_thread(worker);
        lfrfid_worker_write_start(worker, (LFRFIDProtocol)protocol, fox_lf_write_cb, &ctx);
        furi_event_flag_wait(ctx.flags, FOX_LF_FLAG, FuriFlagWaitAny, 12000);
        lfrfid_worker_stop(worker);
        lfrfid_worker_stop_thread(worker);
        lfrfid_worker_free(worker);
        switch(ctx.result) {
        case LFRFIDWorkerWriteOK:
            status = "ok";
            break;
        case LFRFIDWorkerWriteProtocolCannotBeWritten:
        case LFRFIDWorkerWriteFobCannotBeWritten:
            status = "not-writable";
            break;
        default:
            status = "write-failed";
            break;
        }
        furi_event_flag_free(ctx.flags);
    }
    protocol_dict_free(dict);
    return status;
}
