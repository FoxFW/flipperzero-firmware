#include "saflok_detected_protocols.h"

#include <furi.h>

struct SaflokDetectedProtocols {
    uint32_t protocols_detected_num;
    NfcProtocol protocols_detected[NfcProtocolNum];
    uint32_t selected_idx;
};

SaflokDetectedProtocols* saflok_detected_protocols_alloc(void) {
    SaflokDetectedProtocols* detectedProtocols = malloc(sizeof(SaflokDetectedProtocols));

    detectedProtocols->protocols_detected_num = 0;
    detectedProtocols->selected_idx = 0;

    return detectedProtocols;
}

void saflok_detected_protocols_free(SaflokDetectedProtocols* detectedProtocols) {
    furi_assert(detectedProtocols);

    free(detectedProtocols);
}

void saflok_detected_protocols_reset(SaflokDetectedProtocols* detectedProtocols) {
    furi_assert(detectedProtocols);

    FURI_LOG_D("SaflokDetectedProtocols", "Resetting detected protocols");
    detectedProtocols->protocols_detected_num = 0;
    FURI_LOG_D("SaflokDetectedProtocols", "memsetting detected protocols to 0");
    memset(detectedProtocols->protocols_detected, 0, sizeof(detectedProtocols->protocols_detected));
    FURI_LOG_D("SaflokDetectedProtocols", "Setting selected index to 0");
    detectedProtocols->selected_idx = 0;
}

void saflok_detected_protocols_select(SaflokDetectedProtocols* detectedProtocols, uint32_t idx) {
    furi_assert(detectedProtocols);

    detectedProtocols->selected_idx = idx;
}

void saflok_detected_protocols_set(
    SaflokDetectedProtocols* detectedProtocols,
    const NfcProtocol* types,
    uint32_t count) {
    furi_assert(detectedProtocols);
    furi_assert(types);
    furi_assert(count < NfcProtocolNum);

    memcpy(detectedProtocols->protocols_detected, types, count);
    detectedProtocols->protocols_detected_num = count;
    detectedProtocols->selected_idx = 0;
}

uint32_t saflok_detected_protocols_get_num(SaflokDetectedProtocols* detectedProtocols) {
    furi_assert(detectedProtocols);

    return detectedProtocols->protocols_detected_num;
}

NfcProtocol saflok_detected_protocols_get_protocol(SaflokDetectedProtocols* detectedProtocols, uint32_t idx) {
    furi_assert(detectedProtocols);
    furi_assert(idx < detectedProtocols->protocols_detected_num);

    return detectedProtocols->protocols_detected[idx];
}

void saflok_detected_protocols_fill_all_protocols(SaflokDetectedProtocols* detectedProtocols) {
    furi_assert(detectedProtocols);

    detectedProtocols->protocols_detected_num = NfcProtocolNum;
    for(uint32_t i = 0; i < NfcProtocolNum; i++) {
        detectedProtocols->protocols_detected[i] = i;
    }
}

NfcProtocol saflok_detected_protocols_get_selected(SaflokDetectedProtocols* detectedProtocols) {
    furi_assert(detectedProtocols);

    return detectedProtocols->protocols_detected[detectedProtocols->selected_idx];
}

uint32_t saflok_detected_protocols_get_selected_idx(SaflokDetectedProtocols* detectedProtocols) {
    furi_assert(detectedProtocols);

    return detectedProtocols->selected_idx;
}
