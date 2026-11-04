#pragma once

#include <toolbox/bit_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Nfc Nfc;

typedef enum {
    NfcEventTypeUserAbort,
    NfcEventTypeFieldOn,
    NfcEventTypeFieldOff,
    NfcEventTypeTxStart,
    NfcEventTypeTxEnd,
    NfcEventTypeRxStart,
    NfcEventTypeRxEnd,

    NfcEventTypeListenerActivated,
    NfcEventTypePollerReady,
    NfcEventTypeListenerTick,
} NfcEventType;

typedef struct {
    BitBuffer* buffer;
} NfcEventData;

typedef struct {
    NfcEventType type;
    NfcEventData data;
} NfcEvent;

typedef enum {
    NfcCommandContinue,
    NfcCommandReset,
    NfcCommandStop,
    NfcCommandSleep,
} NfcCommand;

typedef NfcCommand (*NfcEventCallback)(NfcEvent event, void* context);

typedef enum {
    NfcModePoller,
    NfcModeListener,

    NfcModeNum,
} NfcMode;

typedef enum {
    NfcTechIso14443a,
    NfcTechIso14443b,
    NfcTechIso15693,
    NfcTechFelica,
    NfcTechTexkom,

    NfcTechNum,
} NfcTech;

typedef enum {
    NfcErrorNone,
    NfcErrorInternal,
    NfcErrorTimeout,
    NfcErrorIncompleteFrame,
    NfcErrorDataFormat,
} NfcError;

Nfc* nfc_alloc(void);

void nfc_free(Nfc* instance);

void nfc_config(Nfc* instance, NfcMode mode, NfcTech tech);

void nfc_set_fdt_poll_fc(Nfc* instance, uint32_t fdt_poll_fc);

void nfc_set_fdt_listen_fc(Nfc* instance, uint32_t fdt_listen_fc);

void nfc_set_mask_receive_time_fc(Nfc* instance, uint32_t mask_rx_time_fc);

void nfc_set_fdt_poll_poll_us(Nfc* instance, uint32_t fdt_poll_poll_us);

void nfc_set_guard_time_us(Nfc* instance, uint32_t guard_time_us);

void nfc_start(Nfc* instance, NfcEventCallback callback, void* context);

void nfc_stop(Nfc* instance);

NfcError
    nfc_poller_trx(Nfc* instance, const BitBuffer* tx_buffer, BitBuffer* rx_buffer, uint32_t fwt);

NfcError nfc_listener_tx(Nfc* instance, const BitBuffer* tx_buffer);

typedef enum {
    NfcIso14443aShortFrameSensReq,
    NfcIso14443aShortFrameAllReqa,
} NfcIso14443aShortFrame;

NfcError nfc_iso14443a_poller_trx_short_frame(
    Nfc* instance,
    NfcIso14443aShortFrame frame,
    BitBuffer* rx_buffer,
    uint32_t fwt);

NfcError nfc_iso14443a_poller_trx_sdd_frame(
    Nfc* instance,
    const BitBuffer* tx_buffer,
    BitBuffer* rx_buffer,
    uint32_t fwt);

NfcError nfc_iso14443a_poller_trx_custom_parity(
    Nfc* instance,
    const BitBuffer* tx_buffer,
    BitBuffer* rx_buffer,
    uint32_t fwt);

NfcError nfc_iso14443a_listener_tx_custom_parity(Nfc* instance, const BitBuffer* tx_buffer);

NfcError nfc_iso14443a_listener_set_col_res_data(
    Nfc* instance,
    uint8_t* uid,
    uint8_t uid_len,
    uint8_t* atqa,
    uint8_t sak);

NfcError nfc_felica_listener_set_sensf_res_data(
    Nfc* instance,
    const uint8_t* idm,
    const uint8_t idm_len,
    const uint8_t* pmm,
    const uint8_t pmm_len,
    const uint16_t sys_code);

NfcError nfc_iso15693_listener_tx_sof(Nfc* instance);

void nfc_felica_listener_timer_anticol_start(Nfc* instance, uint8_t target_time_slot);

void nfc_felica_listener_timer_anticol_stop(Nfc* instance);

/* Texkom: capture one impulse train in poller mode (RX-only; tag repeats its frame). */
NfcError nfc_texkom_poller_rx(Nfc* instance, BitBuffer* rx_buffer);

#ifdef __cplusplus
}
#endif
