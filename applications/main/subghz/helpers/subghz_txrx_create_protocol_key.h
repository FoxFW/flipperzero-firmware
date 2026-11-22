#pragma once
#include "subghz_types.h"
#include "subghz_txrx.h"

bool subghz_txrx_gen_data_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    const char* protocol_name,
    uint64_t key,
    uint32_t bit);

bool subghz_txrx_gen_data_protocol_and_te(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    const char* protocol_name,
    uint64_t key,
    uint32_t bit,
    uint32_t te);

bool subghz_txrx_gen_keeloq_protocol(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    const char* manufacture_name);

bool subghz_txrx_gen_keeloq_bft_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    uint32_t seed,
    const char* manufacture_name);

bool subghz_txrx_gen_nice_flor_s_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    bool nice_one);

bool subghz_txrx_gen_faac_slh_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    uint32_t seed,
    const char* manufacture_name);

bool subghz_txrx_gen_alutech_at_4n_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt);

bool subghz_txrx_gen_somfy_telis_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt);

bool subghz_txrx_gen_kinggates_stylo_4k_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt);

bool subghz_txrx_gen_beninca_arc_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt);

bool subghz_txrx_gen_jarolift_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt);

bool subghz_txrx_gen_came_atomo_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint16_t cnt);

bool subghz_txrx_gen_phoenix_v2_protocol(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint32_t serial,
    uint16_t cnt);

bool subghz_txrx_gen_secplus_v2_protocol(
    SubGhzTxRx* instance,
    const char* name_preset,
    uint32_t frequency,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt);

bool subghz_txrx_gen_secplus_v1_protocol(
    SubGhzTxRx* instance,
    const char* name_preset,
    uint32_t frequency);

void subghz_txrx_gen_serial_gangqi(uint64_t* result_key);

void subghz_txrx_gen_key_marantec(uint64_t* result_key);

