#pragma once
#include <toolbox/protocols/protocol_dict.h>
#include "protocols/lfrfid_protocols.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LFRFIDWorkerWriteOK,
    LFRFIDWorkerWriteProtocolCannotBeWritten,
    LFRFIDWorkerWriteFobCannotBeWritten,
    LFRFIDWorkerWriteTooLongToWrite,
} LFRFIDWorkerWriteResult;

typedef enum {
    LFRFIDWorkerReadTypeAuto,
    LFRFIDWorkerReadTypeASKOnly,
    LFRFIDWorkerReadTypePSKOnly,
} LFRFIDWorkerReadType;

typedef enum {
    LFRFIDWorkerReadSenseStart,
    LFRFIDWorkerReadSenseEnd,
    LFRFIDWorkerReadSenseCardStart,
    LFRFIDWorkerReadSenseCardEnd,
    LFRFIDWorkerReadStartASK,
    LFRFIDWorkerReadStartPSK,
    LFRFIDWorkerReadDone,
} LFRFIDWorkerReadResult;

typedef enum {
    LFRFIDWorkerReadRawFileError,
    LFRFIDWorkerReadRawOverrun,
} LFRFIDWorkerReadRawResult;

typedef enum {
    LFRFIDWorkerEmulateRawFileError,
    LFRFIDWorkerEmulateRawOverrun,
} LFRFIDWorkerEmulateRawResult;

typedef void (
    *LFRFIDWorkerReadCallback)(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context);
typedef void (*LFRFIDWorkerWriteCallback)(LFRFIDWorkerWriteResult result, void* context);

typedef void (*LFRFIDWorkerReadRawCallback)(LFRFIDWorkerReadRawResult result, void* context);
typedef void (*LFRFIDWorkerEmulateRawCallback)(LFRFIDWorkerEmulateRawResult result, void* context);

typedef struct LFRFIDWorker LFRFIDWorker;

LFRFIDWorker* lfrfid_worker_alloc(ProtocolDict* dict);

void lfrfid_worker_free(LFRFIDWorker* worker);

void lfrfid_worker_start_thread(LFRFIDWorker* worker);

void lfrfid_worker_stop_thread(LFRFIDWorker* worker);

void lfrfid_worker_read_start(
    LFRFIDWorker* worker,
    LFRFIDWorkerReadType type,
    LFRFIDWorkerReadCallback callback,
    void* context);

void lfrfid_worker_write_start(
    LFRFIDWorker* worker,
    LFRFIDProtocol protocol,
    LFRFIDWorkerWriteCallback callback,
    void* context);

void lfrfid_worker_write_and_set_pass_start(
    LFRFIDWorker* worker,
    LFRFIDProtocol protocol,
    LFRFIDWorkerWriteCallback callback,
    void* context);

void lfrfid_worker_emulate_start(LFRFIDWorker* worker, LFRFIDProtocol protocol);

void lfrfid_worker_read_raw_start(
    LFRFIDWorker* worker,
    const char* filename,
    LFRFIDWorkerReadType type,
    LFRFIDWorkerReadRawCallback callback,
    void* context);

void lfrfid_worker_emulate_raw_start(
    LFRFIDWorker* worker,
    const char* filename,
    LFRFIDWorkerEmulateRawCallback callback,
    void* context);

void lfrfid_worker_stop(LFRFIDWorker* worker);

#ifdef __cplusplus
}
#endif
