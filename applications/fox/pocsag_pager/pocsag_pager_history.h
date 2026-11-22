
#pragma once

#include <math.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/types.h>

typedef struct PCSGHistory PCSGHistory;

typedef enum {
    PCSGHistoryStateAddKeyUnknown,
    PCSGHistoryStateAddKeyTimeOut,
    PCSGHistoryStateAddKeyNewDada,
    PCSGHistoryStateAddKeyUpdateData,
    PCSGHistoryStateAddKeyOverflow,
} PCSGHistoryStateAddKey;

PCSGHistory* pcsg_history_alloc(void);

void pcsg_history_free(PCSGHistory* instance);

void pcsg_history_reset(PCSGHistory* instance);

uint32_t pcsg_history_get_frequency(PCSGHistory* instance, uint16_t idx);

SubGhzRadioPreset* pcsg_history_get_radio_preset(PCSGHistory* instance, uint16_t idx);

const char* pcsg_history_get_preset(PCSGHistory* instance, uint16_t idx);

uint16_t pcsg_history_get_item(PCSGHistory* instance);

uint8_t pcsg_history_get_type_protocol(PCSGHistory* instance, uint16_t idx);

const char* pcsg_history_get_protocol_name(PCSGHistory* instance, uint16_t idx);

void pcsg_history_get_text_item_menu(PCSGHistory* instance, FuriString* output, uint16_t idx);

bool pcsg_history_get_text_space_left(PCSGHistory* instance, FuriString* output);

PCSGHistoryStateAddKey
    pcsg_history_add_to_history(PCSGHistory* instance, void* context, SubGhzRadioPreset* preset);

FlipperFormat* pcsg_history_get_raw_data(PCSGHistory* instance, uint16_t idx);
