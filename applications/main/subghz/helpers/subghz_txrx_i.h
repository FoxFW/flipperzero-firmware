#pragma once

#include "subghz_txrx.h"
#include <lib/toolbox/level_duration.h>

typedef struct {
    uint32_t last_tick;
    bool     open;
    bool     floor_valid;
    int16_t  floor_db;
    uint8_t  low_hits;
    int16_t  manual_floor;
    bool     adaptive;
} SubGhzRssiGate;

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

    LevelDuration* rx_ring;
    size_t rx_ring_cap;
    size_t rx_ring_head;
    size_t rx_ring_count;
    bool rx_ring_wanted;
    bool rx_burst_ready;
    size_t rx_burst_head;
    size_t rx_burst_count;
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
    uint8_t preset_hopper_mode;
    uint8_t preset_hopper_group;
    uint16_t preset_hopper_group_tick;

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
    SubGhzRssiGate dual_gate_main;
    SubGhzRssiGate dual_gate_ext;
    int16_t dual_int_floor_db;
    int16_t dual_ext_floor_db;
    bool dual_rssi_adaptive;
    FuriString* dual_ext_preset_name;
    uint32_t dual_ext_frequency;
    uint8_t* dual_ext_preset_data;
    size_t dual_ext_preset_data_size;

    bool dual_int_jamming;
    bool dual_ext_jamming;
    uint32_t dual_int_jam_frequency;
    uint8_t* dual_int_jam_preset_data;
    size_t dual_int_jam_preset_data_size;
    uint32_t dual_ext_jam_frequency;
    uint8_t* dual_ext_jam_preset_data;
    size_t dual_ext_jam_preset_data_size;
    bool dual_int_jam_active;
    bool dual_ext_jam_active;
    bool dual_jam_level;
};
