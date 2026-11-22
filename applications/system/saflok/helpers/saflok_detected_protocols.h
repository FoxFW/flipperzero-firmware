#pragma once

#include <stdint.h>
#include <nfc/protocols/nfc_protocol.h>

typedef struct SaflokDetectedProtocols SaflokDetectedProtocols;

SaflokDetectedProtocols* saflok_detected_protocols_alloc(void);

void saflok_detected_protocols_free(SaflokDetectedProtocols* detectedProtocols);

void saflok_detected_protocols_reset(SaflokDetectedProtocols* detectedProtocols);

void saflok_detected_protocols_select(SaflokDetectedProtocols* detectedProtocols, uint32_t idx);

void saflok_detected_protocols_set(
    SaflokDetectedProtocols* detectedProtocols,
    const NfcProtocol* types,
    uint32_t count);

uint32_t saflok_detected_protocols_get_num(SaflokDetectedProtocols* detectedProtocols);

NfcProtocol saflok_detected_protocols_get_protocol(SaflokDetectedProtocols* detectedProtocols, uint32_t idx);

void saflok_detected_protocols_fill_all_protocols(SaflokDetectedProtocols* detectedProtocols);

NfcProtocol saflok_detected_protocols_get_selected(SaflokDetectedProtocols* detectedProtocols);

uint32_t saflok_detected_protocols_get_selected_idx(SaflokDetectedProtocols* detectedProtocols);
