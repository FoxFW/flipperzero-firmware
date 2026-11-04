#pragma once

#include "subghz_txrx.h"
#include <lib/toolbox/level_duration.h>

struct SubGhzTxRx {
    SubGhzWorker* worker;

    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzProtocolFlag receiver_modulation;
    SubGhzProtocolFlag receiver_exclude;
    SubGhzProtocolDecoderBase* decoder_standalone;
    SubGhzProtocolFlag receiver_filter;
    bool receiver_filter_set;
    SubGhzReceiverCallback receiver_rx_callback;
    void* receiver_rx_context;
    SubGhzReceiverProtocolEnabledCallback receiver_enabled_callback;
    void* receiver_enabled_context;
    /* Rolling capture for Unknown RAW: bounded ring of recent RX samples. */
    LevelDuration* rx_ring;
    size_t rx_ring_cap;
    size_t rx_ring_head;
    size_t rx_ring_count;
    bool rx_ring_wanted;
    bool rx_burst_ready; /* a real burst ended; ring frozen until tick captures it */
    size_t rx_burst_head; /* latched ring end (exclusive) of that burst */
    size_t rx_burst_count; /* latched pair count of that burst */
    SubGhzTransmitter* transmitter;
    SubGhzProtocolDecoderBase* decoder_result;
    FlipperFormat* fff_data;

    SubGhzRadioPreset* preset;
    SubGhzSetting* setting;

    uint8_t hopper_timeout;
    uint8_t hopper_idx_frequency;
    bool is_database_loaded;
    SubGhzHopperState hopper_state;

    uint8_t preset_hopper_timeout;
    size_t preset_hopper_idx;
    SubGhzPresetHopperState preset_hopper_state;

    int32_t frequency_offset;

    SubGhzTxRxState txrx_state;
    SubGhzSpeakerState speaker_state;
    const SubGhzDevice* radio_device;
    SubGhzRadioDeviceType radio_device_type;

    SubGhzTxRxNeedSaveCallback need_save_callback;
    void* need_save_context;

    bool debug_pin_state;

    SubGhzWorker* dual_worker;
    SubGhzReceiver* dual_receiver;
    SubGhzProtocolFlag dual_receiver_modulation;
    FuriMutex* dual_mutex;
    const SubGhzDevice* dual_device;
    FuriString* dual_ext_preset_name;
    uint32_t dual_ext_frequency;
    uint8_t* dual_ext_preset_data;
    size_t dual_ext_preset_data_size;
};
