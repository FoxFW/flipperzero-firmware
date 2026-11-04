#include "subghz_txrx_i.h"
#include <core/kernel.h>

#include <math.h>
#include <lib/subghz/protocols/protocol_items.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/blocks/custom_btn.h>
#include <storage/storage.h>

#define TAG "SubGhzTxRx"

/* Unknown-RAW rolling capture depth (LevelDuration pairs). */
/* A single edge longer than this (us) is silence/gap, not part of a coherent
 * burst. Used to reset the burst counter so sparse NOISE never accumulates to
 * the Unknown-RAW burst trigger -- only a continuous real transmission does. 30 ms is
 * well above any intra-burst gap and below inter-signal silence. Tunable. */
#define SUBGHZ_RX_BURST_GAP_US 30000u
#define SUBGHZ_RX_BURST_MIN_PAIRS 128u /* min pairs for a real burst */
#define SUBGHZ_RX_RING_CAP 1024u /* 4KB ring (LevelDuration=4B). Was 4096=16KB; shrunk for RX RAM headroom (free-heap guard is 20KB). Tunable: raise if Unknown RAW misses long captures, lower to 512 if RAM still tight. */

#define FOX_CC1101_EXT_STATUS_PATH EXT_PATH("subghz/.cc1101_ext_status")

static void fox_cc1101_ext_status_write(bool connected) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, FOX_CC1101_EXT_STATUS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char c = connected ? '1' : '0';
        storage_file_write(file, &c, 1);
        storage_file_close(file);
    } else {
        FURI_LOG_W(TAG, "Couldn't write %s", FOX_CC1101_EXT_STATUS_PATH);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void subghz_txrx_radio_device_power_on(SubGhzTxRx* instance) {
    UNUSED(instance);
    uint8_t attempts = 0;
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();

        furi_delay_ms(10);
    }
}

static void subghz_txrx_radio_device_power_off(SubGhzTxRx* instance) {
    UNUSED(instance);
    if(furi_hal_power_is_otg_enabled()) furi_hal_power_disable_otg();
}

static void subghz_txrx_rx_pair_tee(void* context, bool level, uint32_t duration) {
    SubGhzTxRx* instance = context;
    if(instance->rx_ring && !instance->rx_burst_ready) {
        instance->rx_ring[instance->rx_ring_head] = level_duration_make(level, duration);
        instance->rx_ring_head = (instance->rx_ring_head + 1) % instance->rx_ring_cap;
        if(duration > SUBGHZ_RX_BURST_GAP_US) {
            /* Silence/gap -> end of (or no) burst: reset so noise can't pile up.
             * If the burst was long enough to be a real signal, latch its window
             * and freeze the ring (rx_burst_ready) so the receiver-scene tick can
             * grab it as an Unknown RAW before new edges overwrite it. Live decode
             * (subghz_receiver_decode below) keeps running while frozen. */
            if(instance->rx_ring_count >= SUBGHZ_RX_BURST_MIN_PAIRS) {
                instance->rx_burst_head =
                    (instance->rx_ring_head + instance->rx_ring_cap - 1) % instance->rx_ring_cap;
                instance->rx_burst_count = instance->rx_ring_count;
                instance->rx_burst_ready = true;
            }
            instance->rx_ring_count = 0;
        } else if(instance->rx_ring_count < instance->rx_ring_cap) {
            instance->rx_ring_count++;
        }
    }
    if(instance->receiver) {
        if(instance->dual_mutex) furi_mutex_acquire(instance->dual_mutex, FuriWaitForever);
        subghz_receiver_decode(instance->receiver, level, duration);
        if(instance->dual_mutex) furi_mutex_release(instance->dual_mutex);
    }
}

static void subghz_txrx_rx_overrun_tee(void* context) {
    SubGhzTxRx* instance = context;
    if(instance->receiver) {
        if(instance->dual_mutex) furi_mutex_acquire(instance->dual_mutex, FuriWaitForever);
        subghz_receiver_reset(instance->receiver);
        if(instance->dual_mutex) furi_mutex_release(instance->dual_mutex);
    }
}

static void subghz_txrx_dual_pair(void* context, bool level, uint32_t duration) {
    SubGhzTxRx* instance = context;
    furi_mutex_acquire(instance->dual_mutex, FuriWaitForever);
    subghz_receiver_decode(instance->dual_receiver, level, duration);
    furi_mutex_release(instance->dual_mutex);
}

static void subghz_txrx_dual_overrun(void* context) {
    SubGhzTxRx* instance = context;
    furi_mutex_acquire(instance->dual_mutex, FuriWaitForever);
    subghz_receiver_reset(instance->dual_receiver);
    furi_mutex_release(instance->dual_mutex);
}

static void subghz_txrx_rx_ring_alloc(SubGhzTxRx* instance) {
    if(instance->rx_ring || !instance->rx_ring_wanted) return;
    instance->rx_ring_head = 0;
    instance->rx_ring_count = 0;
    instance->rx_burst_ready = false;
    instance->rx_burst_head = 0;
    instance->rx_burst_count = 0;
    instance->rx_ring = malloc(sizeof(LevelDuration) * instance->rx_ring_cap);
}

#define SUBGHZ_CC1101_REG_MDMCFG2        0x12U
#define SUBGHZ_CC1101_MOD_FORMAT_MASK    0x70U
#define SUBGHZ_CC1101_MOD_FORMAT_ASK_OOK 0x30U

static bool subghz_txrx_preset_try_get_register(
    const uint8_t* data,
    size_t size,
    uint8_t reg,
    uint8_t* value) {
    if(!data || !value || (size < 2U)) {
        return false;
    }
    for(size_t i = 0; i + 1U < size; i += 2U) {
        const uint8_t address = data[i];
        const uint8_t reg_data = data[i + 1U];
        if((address == 0x00U) && (reg_data == 0x00U)) {
            break;
        }
        if(address == reg) {
            *value = reg_data;
            return true;
        }
    }
    return false;
}

static SubGhzProtocolFlag subghz_txrx_preset_modulation(const uint8_t* data, size_t size) {
    uint8_t mdmcfg2 = 0U;
    if(!subghz_txrx_preset_try_get_register(data, size, SUBGHZ_CC1101_REG_MDMCFG2, &mdmcfg2)) {
        return 0;
    }
    return ((mdmcfg2 & SUBGHZ_CC1101_MOD_FORMAT_MASK) == SUBGHZ_CC1101_MOD_FORMAT_ASK_OOK) ?
               SubGhzProtocolFlag_AM :
               SubGhzProtocolFlag_FM;
}

static SubGhzProtocolFlag subghz_txrx_current_modulation(SubGhzTxRx* instance) {
    if(instance->preset->data) {
        return subghz_txrx_preset_modulation(instance->preset->data, instance->preset->data_size);
    }
    const int index = subghz_setting_get_inx_preset_by_name(
        instance->setting, furi_string_get_cstr(instance->preset->name));
    if(index < 0) return 0;
    return subghz_txrx_preset_modulation(
        subghz_setting_get_preset_data(instance->setting, (size_t)index),
        subghz_setting_get_preset_data_size(instance->setting, (size_t)index));
}

static SubGhzProtocolFlag subghz_txrx_receiver_wanted_modulation(SubGhzTxRx* instance) {
    SubGhzProtocolFlag modulation = subghz_txrx_current_modulation(instance);
    if(modulation && instance->preset_hopper_state != SubGhzPresetHopperStateOFF) {
        size_t count = subghz_setting_get_hopper_preset_count(instance->setting);
        const bool all = count == 0;
        if(all) count = subghz_setting_get_preset_count(instance->setting);
        for(size_t i = 0; i < count && modulation; i++) {
            const size_t index =
                all ? i : subghz_setting_get_hopper_preset_index(instance->setting, i);
            const SubGhzProtocolFlag preset_modulation = subghz_txrx_preset_modulation(
                subghz_setting_get_preset_data(instance->setting, index),
                subghz_setting_get_preset_data_size(instance->setting, index));
            modulation = preset_modulation ? (modulation | preset_modulation) : 0;
        }
    }
    if(modulation == (SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM)) modulation = 0;
    return modulation;
}

static size_t subghz_txrx_receiver_cost(
    SubGhzTxRx* instance,
    SubGhzProtocolFlag modulation,
    SubGhzProtocolFlag exclude) {
    const SubGhzProtocolRegistry* registry =
        subghz_environment_get_protocol_registry(instance->environment);
    size_t total = 0;
    size_t used = 0;
    for(size_t i = 0; i < subghz_protocol_registry_count(registry); i++) {
        const SubGhzProtocol* protocol = subghz_protocol_registry_get_by_index(registry, i);
        if(!protocol->decoder || !protocol->decoder->alloc) continue;
        total++;
        const SubGhzProtocolFlag protocol_modulation =
            protocol->flag & (SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM);
        if(exclude && (protocol->flag & exclude)) continue;
        if(modulation && protocol_modulation && !(protocol_modulation & modulation)) continue;
        used++;
    }
    return total ? (size_t)SUBGHZ_TXRX_RECEIVER_HEAP_COST * used / total : 0;
}

static void subghz_txrx_receiver_setup(SubGhzTxRx* instance, SubGhzReceiver* receiver) {
    if(instance->receiver_filter_set) {
        subghz_receiver_set_filter(receiver, instance->receiver_filter);
    }
    subghz_receiver_set_rx_callback(
        receiver, instance->receiver_rx_callback, instance->receiver_rx_context);
    subghz_receiver_set_protocol_enabled_callback(
        receiver, instance->receiver_enabled_callback, instance->receiver_enabled_context);
}

static void subghz_txrx_drop_standalone_decoder(SubGhzTxRx* instance) {
    if(!instance->decoder_standalone) return;
    if(instance->decoder_result == instance->decoder_standalone) instance->decoder_result = NULL;
    instance->decoder_standalone->protocol->decoder->free(instance->decoder_standalone);
    instance->decoder_standalone = NULL;
}

static SubGhzReceiver* subghz_txrx_receiver_ensure(SubGhzTxRx* instance) {
    const SubGhzProtocolFlag modulation = subghz_txrx_receiver_wanted_modulation(instance);
    const SubGhzProtocolFlag exclude = instance->dual_worker ? SUBGHZ_TXRX_DUAL_EXCLUDE : 0;
    SubGhzProtocolDecoderBase* kept = NULL;
    if(instance->receiver) {
        if((instance->receiver_modulation == modulation && instance->receiver_exclude == exclude) ||
           instance->txrx_state == SubGhzTxRxStateRx) {
            return instance->receiver;
        }
        if(instance->decoder_result && instance->decoder_result != instance->decoder_standalone) {
            if(subghz_receiver_detach_decoder(instance->receiver, instance->decoder_result)) {
                kept = instance->decoder_result;
            } else {
                instance->decoder_result = NULL;
            }
        }
        subghz_receiver_free(instance->receiver);
        instance->receiver = NULL;
    }
    subghz_txrx_rx_ring_alloc(instance);
    instance->receiver =
        subghz_receiver_alloc_init_ex(instance->environment, modulation, exclude);
    instance->receiver_modulation = modulation;
    instance->receiver_exclude = exclude;
    subghz_txrx_receiver_setup(instance, instance->receiver);
    if(kept && !subghz_receiver_adopt_decoder(instance->receiver, kept)) {
        subghz_txrx_drop_standalone_decoder(instance);
        instance->decoder_standalone = kept;
    }
    return instance->receiver;
}

SubGhzTxRx* subghz_txrx_alloc(void) {
    SubGhzTxRx* instance = malloc(sizeof(SubGhzTxRx));
    instance->setting = subghz_setting_alloc();
    subghz_setting_load(instance->setting, EXT_PATH("subghz/assets/setting_user"));

    instance->preset = malloc(sizeof(SubGhzRadioPreset));
    instance->preset->name = furi_string_alloc();
    subghz_txrx_set_default_preset(instance, 0);

    instance->txrx_state = SubGhzTxRxStateSleep;
    instance->frequency_offset = 0;

    subghz_txrx_hopper_set_state(instance, SubGhzHopperStateOFF);
    subghz_txrx_preset_hopper_set_state(instance, SubGhzPresetHopperStateOFF);
    instance->preset_hopper_idx = 0;
    instance->preset_hopper_timeout = 0;
    subghz_txrx_speaker_set_state(instance, SubGhzSpeakerStateDisable);
    subghz_txrx_set_debug_pin_state(instance, false);

    instance->worker = subghz_worker_alloc();
    instance->rx_ring_cap = SUBGHZ_RX_RING_CAP;
    instance->rx_ring = NULL;
    instance->rx_ring_head = 0;
    instance->rx_ring_count = 0;
    instance->rx_ring_wanted = false;
    instance->rx_burst_ready = false;
    instance->rx_burst_head = 0;
    instance->rx_burst_count = 0;
    instance->fff_data = flipper_format_string_alloc();

    instance->environment = subghz_environment_alloc();
    instance->is_database_loaded =
        subghz_environment_load_keystore(instance->environment, SUBGHZ_KEYSTORE_DIR_NAME);
    subghz_environment_load_keystore(instance->environment, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        instance->environment, SUBGHZ_ALUTECH_AT_4N_DIR_NAME);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(
        instance->environment, SUBGHZ_NICE_FLOR_S_DIR_NAME);
    subghz_environment_set_protocol_registry(
        instance->environment, (void*)&subghz_protocol_registry);
    instance->receiver = NULL;
    instance->receiver_modulation = 0;
    instance->receiver_exclude = 0;
    instance->decoder_standalone = NULL;
    instance->decoder_result = NULL;
    instance->dual_worker = NULL;
    instance->dual_receiver = NULL;
    instance->dual_receiver_modulation = 0;
    instance->dual_mutex = NULL;
    instance->dual_device = NULL;
    instance->dual_ext_preset_name = furi_string_alloc();
    instance->dual_ext_frequency = 0;
    instance->dual_ext_preset_data = NULL;
    instance->dual_ext_preset_data_size = 0;
    instance->receiver_filter_set = false;
    instance->receiver_rx_callback = NULL;
    instance->receiver_rx_context = NULL;
    instance->receiver_enabled_callback = NULL;
    instance->receiver_enabled_context = NULL;

    subghz_worker_set_overrun_callback(instance->worker, subghz_txrx_rx_overrun_tee);
    subghz_worker_set_pair_callback(instance->worker, subghz_txrx_rx_pair_tee);
    subghz_worker_set_context(instance->worker, instance);

    subghz_devices_init();
    instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
    instance->radio_device_type =
        subghz_txrx_radio_device_set(instance, SubGhzRadioDeviceTypeExternalCC1101);

    return instance;
}

void subghz_txrx_free(SubGhzTxRx* instance) {
    furi_assert(instance);

    subghz_txrx_dual_rx_stop(instance, instance->radio_device_type);

    if(instance->radio_device_type != SubGhzRadioDeviceTypeInternal) {
        subghz_txrx_radio_device_power_off(instance);
        subghz_devices_end(instance->radio_device);
    }

    subghz_devices_deinit();

    subghz_worker_free(instance->worker);
    if(instance->rx_ring) free(instance->rx_ring);
    subghz_txrx_drop_standalone_decoder(instance);
    if(instance->receiver) subghz_receiver_free(instance->receiver);
    furi_string_free(instance->dual_ext_preset_name);

    furi_kernel_lock();
    subghz_environment_free(instance->environment);
    flipper_format_free(instance->fff_data);
    furi_string_free(instance->preset->name);
    subghz_setting_free(instance->setting);

    free(instance->preset);
    free(instance);
    furi_kernel_unlock();
}

bool subghz_txrx_is_database_loaded(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->is_database_loaded;
}

void subghz_txrx_set_preset(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    furi_assert(instance);
    furi_string_set(instance->preset->name, preset_name);

    SubGhzRadioPreset* preset = instance->preset;
    preset->frequency = frequency;
    preset->data = preset_data;
    preset->data_size = preset_data_size;
}

uint8_t*
    subghz_txrx_set_tx_power(uint8_t* preset_data, size_t preset_data_size, uint8_t tx_power) {
#define PRESET_POWER_OFFSET_FM 8
#define PRESET_POWER_OFFSET_AM 7
#define TX_PATABLE_OFFSET_AM   8
#define TX_PATABLE_COUNT       17

    const uint8_t tx_pa_table[TX_PATABLE_COUNT] = {
        0,
        0xC0,
        0xCD,
        0x86,
        0x50,
        0x26,
        0x1D,
        0x17,
        0x03,
        0xC0,
        0xC8,
        0x84,
        0x60,
        0x34,
        0x1D,
        0x0E,
        0x12,
    };

    uint8_t fm_byte = preset_data[preset_data_size - PRESET_POWER_OFFSET_FM];
    uint8_t am_byte = preset_data[preset_data_size - PRESET_POWER_OFFSET_AM];

    if(fm_byte && !am_byte) {

        if(tx_power) {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_FM] =
                tx_pa_table[TX_PATABLE_OFFSET_AM + tx_power];
        } else {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_FM] =
                tx_pa_table[1];
        }
    } else if(am_byte && !fm_byte) {

        if(tx_power) {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_AM] = tx_pa_table[tx_power];
        } else {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_AM] =
                tx_pa_table[1];
        }
    }

    return preset_data;
}

const char* subghz_txrx_get_preset_name(SubGhzTxRx* instance, const char* preset) {
    UNUSED(instance);
    const char* preset_name = "";
    if(!strcmp(preset, "FuriHalSubGhzPresetOok270Async")) {
        preset_name = "AM270";
    } else if(!strcmp(preset, "FuriHalSubGhzPresetOok650Async")) {
        preset_name = "AM650";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev238Async")) {
        preset_name = "FM238";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev12KAsync")) {
        preset_name = "FM12K";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev476Async")) {
        preset_name = "FM476";
    } else if(!strcmp(preset, "FuriHalSubGhzPresetCustom")) {
        preset_name = "CUSTOM";
    } else {
        FURI_LOG_E(TAG, "Unknown preset");
    }
    return preset_name;
}

SubGhzRadioPreset subghz_txrx_get_preset(SubGhzTxRx* instance) {
    furi_assert(instance);
    return *instance->preset;
}

void subghz_txrx_get_frequency_and_modulation(
    SubGhzTxRx* instance,
    FuriString* frequency,
    FuriString* modulation,
    bool long_name) {
    furi_assert(instance);
    SubGhzRadioPreset* preset = instance->preset;
    if(frequency != NULL) {
        furi_string_printf(
            frequency,
            "%03ld.%02ld",
            preset->frequency / 1000000 % 1000,
            preset->frequency / 10000 % 100);
    }
    if(modulation != NULL) {
        if(long_name) {
            furi_string_printf(modulation, "%s", furi_string_get_cstr(preset->name));
        } else {
            furi_string_printf(modulation, "%.2s", furi_string_get_cstr(preset->name));
        }
    }
}

static void subghz_txrx_begin(SubGhzTxRx* instance, uint8_t* preset_data) {
    furi_assert(instance);
    if(preset_data == NULL) {
        FURI_LOG_E(TAG, "Unknown preset");
        return;
    }
    subghz_devices_reset(instance->radio_device);
    subghz_devices_idle(instance->radio_device);
    subghz_devices_load_preset(instance->radio_device, FuriHalSubGhzPresetCustom, preset_data);
    instance->txrx_state = SubGhzTxRxStateIDLE;
}

static uint32_t subghz_txrx_rx(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    furi_assert(
        instance->txrx_state != SubGhzTxRxStateRx && instance->txrx_state != SubGhzTxRxStateSleep);

    subghz_devices_idle(instance->radio_device);

    subghz_txrx_receiver_apply_modulation_filter(instance);

    uint32_t value = subghz_devices_set_frequency(
        instance->radio_device, (uint32_t)((int64_t)frequency + instance->frequency_offset));
    subghz_devices_flush_rx(instance->radio_device);
    subghz_txrx_speaker_on(instance);

    subghz_devices_start_async_rx(
        instance->radio_device, subghz_worker_rx_callback, instance->worker);
    subghz_worker_start(instance->worker);
    instance->txrx_state = SubGhzTxRxStateRx;
    return value;
}

static void subghz_txrx_idle(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->txrx_state != SubGhzTxRxStateSleep) {
        subghz_devices_idle(instance->radio_device);
        subghz_txrx_speaker_off(instance);
        instance->txrx_state = SubGhzTxRxStateIDLE;
    }
}

static void subghz_txrx_rx_end(SubGhzTxRx* instance) {
    furi_assert(instance);
    furi_assert(instance->txrx_state == SubGhzTxRxStateRx);

    if(subghz_worker_is_running(instance->worker)) {
        subghz_worker_stop(instance->worker);
        subghz_devices_stop_async_rx(instance->radio_device);
    }
    subghz_devices_idle(instance->radio_device);
    subghz_txrx_speaker_off(instance);
    instance->txrx_state = SubGhzTxRxStateIDLE;
}

void subghz_txrx_sleep(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_devices_sleep(instance->radio_device);
    instance->txrx_state = SubGhzTxRxStateSleep;
}

static bool subghz_txrx_tx(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    furi_assert(instance->txrx_state != SubGhzTxRxStateSleep);

    subghz_devices_idle(instance->radio_device);
    subghz_devices_set_frequency(
        instance->radio_device, (uint32_t)((int64_t)frequency + instance->frequency_offset));

    bool ret = subghz_devices_set_tx(instance->radio_device);
    if(ret) {
        subghz_txrx_speaker_on(instance);
        instance->txrx_state = SubGhzTxRxStateTx;
    }

    return ret;
}

SubGhzTxRxStartTxState subghz_txrx_tx_start(SubGhzTxRx* instance, FlipperFormat* flipper_format) {
    furi_assert(instance);
    furi_assert(flipper_format);

    subghz_txrx_stop(instance);

    SubGhzTxRxStartTxState ret = SubGhzTxRxStartTxStateErrorParserOthers;
    FuriString* temp_str = furi_string_alloc();
    do {
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            break;
        }
        ret = SubGhzTxRxStartTxStateOk;

        SubGhzRadioPreset* preset = instance->preset;
        instance->transmitter =
            subghz_transmitter_alloc_init(instance->environment, furi_string_get_cstr(temp_str));

        if(instance->transmitter) {
            if(subghz_transmitter_deserialize(instance->transmitter, flipper_format) ==
               SubGhzProtocolStatusOk) {
                if(strcmp(furi_string_get_cstr(preset->name), "") != 0) {
                    subghz_txrx_begin(
                        instance,
                        subghz_setting_get_preset_data_by_name(
                            instance->setting, furi_string_get_cstr(preset->name)));
                    if(preset->frequency) {
                        if(!subghz_txrx_tx(instance, preset->frequency)) {
                            FURI_LOG_E(TAG, "Only Rx");
                            ret = SubGhzTxRxStartTxStateErrorOnlyRx;
                        }
                    } else {
                        ret = SubGhzTxRxStartTxStateErrorParserOthers;
                    }

                } else {
                    FURI_LOG_E(
                        TAG, "Unknown name preset \" %s \"", furi_string_get_cstr(preset->name));
                    ret = SubGhzTxRxStartTxStateErrorParserOthers;
                }

                if(ret == SubGhzTxRxStartTxStateOk) {

                    subghz_devices_start_async_tx(
                        instance->radio_device, subghz_transmitter_yield, instance->transmitter);
                }
            } else {
                ret = SubGhzTxRxStartTxStateErrorParserOthers;
            }
        } else {
            ret = SubGhzTxRxStartTxStateErrorParserOthers;
        }
        if(ret != SubGhzTxRxStartTxStateOk) {
            if(instance->transmitter) {
                subghz_transmitter_free(instance->transmitter);
                instance->transmitter = NULL;
            }
            if(instance->txrx_state != SubGhzTxRxStateIDLE) {
                subghz_txrx_idle(instance);
            }
        }

    } while(false);
    furi_string_free(temp_str);
    return ret;
}

void subghz_txrx_rx_start(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_txrx_stop(instance);
    subghz_txrx_begin(
        instance,
        subghz_setting_get_preset_data_by_name(
            subghz_txrx_get_setting(instance), furi_string_get_cstr(instance->preset->name)));
    subghz_txrx_rx(instance, instance->preset->frequency);
}

void subghz_txrx_set_need_save_callback(
    SubGhzTxRx* instance,
    SubGhzTxRxNeedSaveCallback callback,
    void* context) {
    furi_assert(instance);
    instance->need_save_callback = callback;
    instance->need_save_context = context;
}

static void subghz_txrx_tx_stop(SubGhzTxRx* instance) {
    furi_assert(instance);
    furi_assert(instance->txrx_state == SubGhzTxRxStateTx);

    subghz_devices_stop_async_tx(instance->radio_device);
    subghz_transmitter_stop(instance->transmitter);
    subghz_transmitter_free(instance->transmitter);

    if(instance->decoder_result &&
       instance->decoder_result->protocol->type == SubGhzProtocolTypeDynamic) {
        if(instance->need_save_callback) {
            instance->need_save_callback(instance->need_save_context);
        }
    }
    subghz_txrx_idle(instance);
    subghz_txrx_speaker_off(instance);
}

FlipperFormat* subghz_txrx_get_fff_data(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->fff_data;
}

SubGhzSetting* subghz_txrx_get_setting(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->setting;
}

void subghz_txrx_stop(SubGhzTxRx* instance) {
    furi_assert(instance);

    switch(instance->txrx_state) {
    case SubGhzTxRxStateTx:
        subghz_txrx_tx_stop(instance);
        subghz_txrx_speaker_unmute(instance);
        break;
    case SubGhzTxRxStateRx:
        subghz_txrx_rx_end(instance);
        subghz_txrx_speaker_mute(instance);
        break;

    default:
        break;
    }
}

void subghz_txrx_hopper_update(SubGhzTxRx* instance, float stay_threshold) {
    furi_assert(instance);

    switch(instance->hopper_state) {
    case SubGhzHopperStateOFF:
    case SubGhzHopperStatePause:
        return;
    case SubGhzHopperStateRSSITimeOut:
        if(instance->hopper_timeout != 0) {
            instance->hopper_timeout--;
            return;
        }
        break;
    default:
        break;
    }

    if(instance->hopper_state != SubGhzHopperStateRSSITimeOut) {

        float rssi = subghz_devices_get_rssi(instance->radio_device);

        if(rssi > stay_threshold) {
            instance->hopper_timeout = 10;
            instance->hopper_state = SubGhzHopperStateRSSITimeOut;
            return;
        }
    } else {
        instance->hopper_state = SubGhzHopperStateRunning;
    }

    if(instance->hopper_idx_frequency <
       subghz_setting_get_hopper_frequency_count(instance->setting) - 1) {
        instance->hopper_idx_frequency++;
    } else {
        instance->hopper_idx_frequency = 0;
    }

    if(instance->txrx_state == SubGhzTxRxStateRx) {
        subghz_txrx_rx_end(instance);
    }
    if(instance->txrx_state == SubGhzTxRxStateIDLE) {
        subghz_receiver_reset(subghz_txrx_receiver_ensure(instance));
        instance->preset->frequency =
            subghz_setting_get_hopper_frequency(instance->setting, instance->hopper_idx_frequency);
        subghz_txrx_rx(instance, instance->preset->frequency);
    }
}

SubGhzHopperState subghz_txrx_hopper_get_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->hopper_state;
}

void subghz_txrx_hopper_set_state(SubGhzTxRx* instance, SubGhzHopperState state) {
    furi_assert(instance);
    instance->hopper_state = state;
}

void subghz_txrx_hopper_unpause(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->hopper_state == SubGhzHopperStatePause) {
        instance->hopper_state = SubGhzHopperStateRunning;
    }
}

void subghz_txrx_hopper_pause(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->hopper_state == SubGhzHopperStateRunning) {
        instance->hopper_state = SubGhzHopperStatePause;
    }
}

void subghz_txrx_preset_hopper_update(SubGhzTxRx* instance, float stay_threshold) {
    furi_assert(instance);

    switch(instance->preset_hopper_state) {
    case SubGhzPresetHopperStateOFF:
    case SubGhzPresetHopperStatePause:
        return;
    case SubGhzPresetHopperStateRSSITimeOut:
        if(instance->preset_hopper_timeout != 0) {
            instance->preset_hopper_timeout--;
            return;
        }
        break;
    default:
        break;
    }

    if(instance->preset_hopper_state != SubGhzPresetHopperStateRSSITimeOut) {
        float rssi = subghz_devices_get_rssi(instance->radio_device);

        if(rssi > stay_threshold) {
            instance->preset_hopper_timeout = 20;
            instance->preset_hopper_state = SubGhzPresetHopperStateRSSITimeOut;
            return;
        }
    } else {
        instance->preset_hopper_state = SubGhzPresetHopperStateRunning;
    }

    size_t hopper_preset_count = subghz_setting_get_hopper_preset_count(instance->setting);

    if(hopper_preset_count > 0) {
        if(instance->preset_hopper_idx < hopper_preset_count - 1) {
            instance->preset_hopper_idx++;
        } else {
            instance->preset_hopper_idx = 0;
        }

        size_t actual_preset_idx = subghz_setting_get_hopper_preset_index(
            instance->setting, instance->preset_hopper_idx);

        if(instance->txrx_state == SubGhzTxRxStateRx) {
            subghz_txrx_rx_end(instance);
        }
        if(instance->txrx_state == SubGhzTxRxStateIDLE) {
            const char* old_preset_name = furi_string_get_cstr(instance->preset->name);

            const char* preset_name =
                subghz_setting_get_preset_name(instance->setting, actual_preset_idx);
            subghz_txrx_set_preset_internal(
                instance, instance->preset->frequency, actual_preset_idx, 0);

            bool old_is_am = (strstr(old_preset_name, "AM") != NULL);
            bool new_is_am = (strstr(preset_name, "AM") != NULL);
            bool modulation_changed = (old_is_am != new_is_am);

            if(modulation_changed) {
                subghz_devices_reset(instance->radio_device);
                subghz_devices_load_preset(
                    instance->radio_device,
                    FuriHalSubGhzPresetCustom,
                    instance->preset->data);
            } else {
                subghz_devices_load_preset(
                    instance->radio_device,
                    FuriHalSubGhzPresetCustom,
                    instance->preset->data);
            }

            subghz_txrx_rx(instance, instance->preset->frequency);
        }
    } else {
        size_t preset_count = subghz_setting_get_preset_count(instance->setting);
        if(instance->preset_hopper_idx < preset_count - 1) {
            instance->preset_hopper_idx++;
        } else {
            instance->preset_hopper_idx = 0;
        }

        if(instance->txrx_state == SubGhzTxRxStateRx) {
            subghz_txrx_rx_end(instance);
        }
        if(instance->txrx_state == SubGhzTxRxStateIDLE) {
            const char* old_preset_name = furi_string_get_cstr(instance->preset->name);

            const char* preset_name =
                subghz_setting_get_preset_name(instance->setting, instance->preset_hopper_idx);
            subghz_txrx_set_preset_internal(
                instance, instance->preset->frequency, instance->preset_hopper_idx, 0);

            bool old_is_am = (strstr(old_preset_name, "AM") != NULL);
            bool new_is_am = (strstr(preset_name, "AM") != NULL);
            bool modulation_changed = (old_is_am != new_is_am);

            if(modulation_changed) {
                subghz_devices_reset(instance->radio_device);
                subghz_devices_load_preset(
                    instance->radio_device,
                    FuriHalSubGhzPresetCustom,
                    instance->preset->data);
            } else {
                subghz_devices_load_preset(
                    instance->radio_device,
                    FuriHalSubGhzPresetCustom,
                    instance->preset->data);
            }

            subghz_txrx_rx(instance, instance->preset->frequency);
        }
    }
}

SubGhzPresetHopperState subghz_txrx_preset_hopper_get_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->preset_hopper_state;
}

void subghz_txrx_preset_hopper_set_state(SubGhzTxRx* instance, SubGhzPresetHopperState state) {
    furi_assert(instance);
    instance->preset_hopper_state = state;

    if(state == SubGhzPresetHopperStateRunning) {
        subghz_devices_reset(instance->radio_device);
        subghz_devices_load_preset(
            instance->radio_device,
            FuriHalSubGhzPresetCustom,
            instance->preset->data);
    }
}

void subghz_txrx_preset_hopper_unpause(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->preset_hopper_state == SubGhzPresetHopperStatePause) {
        instance->preset_hopper_state = SubGhzPresetHopperStateRunning;
    }
}

void subghz_txrx_preset_hopper_pause(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->preset_hopper_state == SubGhzPresetHopperStateRunning) {
        instance->preset_hopper_state = SubGhzPresetHopperStatePause;
    }
}

void subghz_txrx_preset_hopper_reset_index(SubGhzTxRx* instance, size_t index) {
    furi_assert(instance);
    instance->preset_hopper_idx = index;
}

void subghz_txrx_speaker_on(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_ibutton);
    }

    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_acquire(30)) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_speaker);
            }
        } else {
            instance->speaker_state = SubGhzSpeakerStateDisable;
        }
    }
}

void subghz_txrx_speaker_off(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
    }
    if(instance->speaker_state != SubGhzSpeakerStateDisable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
            }
            furi_hal_speaker_release();
            if(instance->speaker_state == SubGhzSpeakerStateShutdown)
                instance->speaker_state = SubGhzSpeakerStateDisable;
        }
    }
}

void subghz_txrx_speaker_mute(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
    }
    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
            }
        }
    }
}

void subghz_txrx_speaker_unmute(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_ibutton);
    }
    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_speaker);
            }
        }
    }
}

void subghz_txrx_speaker_set_state(SubGhzTxRx* instance, SubGhzSpeakerState state) {
    furi_assert(instance);
    instance->speaker_state = state;
}

SubGhzSpeakerState subghz_txrx_speaker_get_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->speaker_state;
}

bool subghz_txrx_load_decoder_by_name_protocol(SubGhzTxRx* instance, const char* name_protocol) {
    furi_assert(instance);
    furi_assert(name_protocol);
    instance->decoder_result = NULL;
    subghz_txrx_drop_standalone_decoder(instance);
    instance->decoder_result = subghz_receiver_search_decoder_base_by_name(
        subghz_txrx_receiver_ensure(instance), name_protocol);
    if(!instance->decoder_result) {
        const SubGhzProtocol* protocol = subghz_protocol_registry_get_by_name(
            subghz_environment_get_protocol_registry(instance->environment), name_protocol);
        if(protocol && protocol->decoder && protocol->decoder->alloc) {
            instance->decoder_standalone = protocol->decoder->alloc(instance->environment);
            instance->decoder_result = instance->decoder_standalone;
        }
    }
    return instance->decoder_result != NULL;
}

SubGhzProtocolDecoderBase* subghz_txrx_get_decoder(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->decoder_result;
}

bool subghz_txrx_protocol_is_serializable(SubGhzTxRx* instance) {
    furi_assert(instance);
    return (instance->decoder_result->protocol->flag & SubGhzProtocolFlag_Save) ==
           SubGhzProtocolFlag_Save;
}

bool subghz_txrx_protocol_is_transmittable(SubGhzTxRx* instance, bool check_type) {
    furi_assert(instance);
    const SubGhzProtocol* protocol = instance->decoder_result->protocol;
    if(check_type) {
        return ((protocol->flag & SubGhzProtocolFlag_Send) == SubGhzProtocolFlag_Send) &&
               protocol->encoder->deserialize && protocol->type == SubGhzProtocolTypeStatic;
    }
    return ((protocol->flag & SubGhzProtocolFlag_Send) == SubGhzProtocolFlag_Send) &&
           protocol->encoder->deserialize;
}

uint8_t subghz_txrx_get_raw_tx_progress(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->transmitter) {
        return subghz_transmitter_get_raw_progress(instance->transmitter);
    }
    return 255;
}

void subghz_txrx_receiver_apply_modulation_filter(SubGhzTxRx* instance) {
    furi_assert(instance);
    SubGhzProtocolFlag modulation = subghz_txrx_current_modulation(instance);
    if(!modulation) {
        FURI_LOG_W(TAG, "Preset missing MDMCFG2, modulation gate disabled");
    }
    subghz_receiver_set_modulation_filter(subghz_txrx_receiver_ensure(instance), modulation);
}

void subghz_txrx_receiver_set_filter(SubGhzTxRx* instance, SubGhzProtocolFlag filter) {
    furi_assert(instance);
    instance->receiver_filter = filter;
    instance->receiver_filter_set = true;
    if(instance->receiver) subghz_receiver_set_filter(instance->receiver, filter);
    if(instance->dual_receiver) subghz_receiver_set_filter(instance->dual_receiver, filter);
}

void subghz_txrx_set_protocol_enabled_callback(
    SubGhzTxRx* instance,
    SubGhzReceiverProtocolEnabledCallback callback,
    void* context) {
    furi_assert(instance);
    instance->receiver_enabled_callback = callback;
    instance->receiver_enabled_context = context;
    if(instance->receiver)
        subghz_receiver_set_protocol_enabled_callback(instance->receiver, callback, context);
    if(instance->dual_receiver)
        subghz_receiver_set_protocol_enabled_callback(instance->dual_receiver, callback, context);
}

void subghz_txrx_set_rx_callback(
    SubGhzTxRx* instance,
    SubGhzReceiverCallback callback,
    void* context) {
    instance->receiver_rx_callback = callback;
    instance->receiver_rx_context = context;
    if(instance->receiver) subghz_receiver_set_rx_callback(instance->receiver, callback, context);
    if(instance->dual_receiver)
        subghz_receiver_set_rx_callback(instance->dual_receiver, callback, context);
}

void subghz_txrx_set_raw_file_encoder_worker_callback_end(
    SubGhzTxRx* instance,
    SubGhzProtocolEncoderRAWCallbackEnd callback,
    void* context) {
    subghz_protocol_raw_file_encoder_worker_set_callback_end(
        (SubGhzProtocolEncoderRAW*)subghz_transmitter_get_protocol_instance(instance->transmitter),
        callback,
        context);
}

bool subghz_txrx_radio_device_is_external_connected(SubGhzTxRx* instance, const char* name) {
    furi_assert(instance);

    bool is_connect = false;
    bool is_otg_enabled = furi_hal_power_is_otg_enabled();

    if(!is_otg_enabled) {
        subghz_txrx_radio_device_power_on(instance);
    }

    const SubGhzDevice* device = subghz_devices_get_by_name(name);
    if(device) {
        is_connect = subghz_devices_is_connect(device);
    }

    if(!is_otg_enabled) {
        subghz_txrx_radio_device_power_off(instance);
    }
    return is_connect;
}

SubGhzRadioDeviceType
    subghz_txrx_radio_device_set(SubGhzTxRx* instance, SubGhzRadioDeviceType radio_device_type) {
    furi_assert(instance);

    if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 &&
       subghz_txrx_radio_device_is_external_connected(instance, SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
        subghz_txrx_radio_device_power_on(instance);
        instance->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        subghz_devices_begin(instance->radio_device);
        instance->radio_device_type = SubGhzRadioDeviceTypeExternalCC1101;
        fox_cc1101_ext_status_write(true);
    } else {
        subghz_txrx_radio_device_power_off(instance);
        if(instance->radio_device_type != SubGhzRadioDeviceTypeInternal) {
            subghz_devices_end(instance->radio_device);
        }
        instance->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
        if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101) {

            fox_cc1101_ext_status_write(false);
        }
    }

    return instance->radio_device_type;
}

SubGhzRadioDeviceType subghz_txrx_radio_device_get(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->radio_device_type;
}

float subghz_txrx_radio_device_get_rssi(SubGhzTxRx* instance) {
    furi_assert(instance);
    return subghz_devices_get_rssi(instance->radio_device);
}

const char* subghz_txrx_radio_device_get_name(SubGhzTxRx* instance) {
    furi_assert(instance);
    return subghz_devices_get_name(instance->radio_device);
}

bool subghz_txrx_radio_device_is_frequency_valid(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    return subghz_devices_is_frequency_valid(instance->radio_device, frequency);
}

bool subghz_txrx_radio_device_is_tx_allowed(SubGhzTxRx* instance, uint32_t frequency) {

    furi_assert(instance);
    UNUSED(frequency);

    return true;
}

void subghz_txrx_set_debug_pin_state(SubGhzTxRx* instance, bool state) {
    furi_assert(instance);
    instance->debug_pin_state = state;
}

bool subghz_txrx_get_debug_pin_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->debug_pin_state;
}

void subghz_txrx_set_frequency_offset(SubGhzTxRx* instance, int32_t offset_hz) {
    furi_assert(instance);
    instance->frequency_offset = offset_hz;
}

int32_t subghz_txrx_get_frequency_offset(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->frequency_offset;
}

void subghz_txrx_reset_dynamic_and_custom_btns(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_environment_reset_keeloq(instance->environment);

    faac_slh_reset_prog_mode();

    subghz_custom_btns_reset();
}

SubGhzEnvironment* subghz_txrx_get_environment(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->environment;
}

SubGhzReceiver* subghz_txrx_get_receiver(SubGhzTxRx* instance) {
    furi_assert(instance);
    return subghz_txrx_receiver_ensure(instance);
}

size_t subghz_txrx_receiver_heap_needed(SubGhzTxRx* instance) {
    furi_assert(instance);
    const SubGhzProtocolFlag modulation = subghz_txrx_receiver_wanted_modulation(instance);
    if(instance->receiver && instance->receiver_modulation == modulation &&
       instance->receiver_exclude == 0) {
        return 0;
    }
    return subghz_txrx_receiver_cost(instance, modulation, 0);
}

void subghz_txrx_receiver_release(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->txrx_state == SubGhzTxRxStateRx) return;
    instance->decoder_result = NULL;
    subghz_txrx_drop_standalone_decoder(instance);
    if(!instance->receiver) return;
    subghz_receiver_free(instance->receiver);
    instance->receiver = NULL;
    instance->receiver_modulation = 0;
    instance->receiver_exclude = 0;
    if(instance->rx_ring) {
        free(instance->rx_ring);
        instance->rx_ring = NULL;
    }
    instance->rx_ring_head = 0;
    instance->rx_ring_count = 0;
    instance->rx_burst_ready = false;
    instance->rx_burst_count = 0;
}

void subghz_txrx_set_default_preset(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);

    const char* default_modulation = "AM650";
    if(frequency == 0) {
        frequency = subghz_setting_get_default_frequency(subghz_txrx_get_setting(instance));
    }
    subghz_txrx_set_preset(instance, default_modulation, frequency, NULL, 0);
}

const char* subghz_txrx_set_preset_internal(
    SubGhzTxRx* instance,
    uint32_t frequency,
    uint8_t index,
    uint8_t tx_power) {
    furi_assert(instance);

    SubGhzSetting* setting = subghz_txrx_get_setting(instance);
    const char* preset_name = subghz_setting_get_preset_name(setting, index);
    subghz_setting_set_default_frequency(setting, frequency);

    uint8_t* preset_data = subghz_setting_get_preset_data(setting, index);
    size_t preset_data_size = subghz_setting_get_preset_data_size(setting, index);

    subghz_txrx_set_tx_power(preset_data, preset_data_size, tx_power);

    subghz_txrx_set_preset(instance, preset_name, frequency, preset_data, preset_data_size);

    return preset_name;
}

void subghz_txrx_rx_ring_reset(SubGhzTxRx* instance) {
    furi_assert(instance);
    instance->rx_ring_head = 0;
    instance->rx_ring_count = 0;
    instance->rx_burst_ready = false; /* unfreeze */
}

void subghz_txrx_rx_ring_set_wanted(SubGhzTxRx* instance, bool wanted) {
    furi_assert(instance);
    instance->rx_ring_wanted = wanted;
    if(instance->receiver) subghz_txrx_rx_ring_alloc(instance);
}

bool subghz_txrx_burst_ready(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->rx_burst_ready; /* peek; cleared by subghz_txrx_rx_ring_reset */
}

FlipperFormat* subghz_txrx_rx_ring_build_raw_ff(SubGhzTxRx* instance) {
    furi_assert(instance);
    /* Build from the LATCHED burst window (rx_burst_head/count), which the ring
     * freeze keeps intact. Standard RAW .sub wire format,
     * in-memory (no Storage). Caller owns the returned ff. NULL if nothing latched. */
    if(!instance->rx_ring || instance->rx_burst_count == 0) return NULL;
    FlipperFormat* ff = flipper_format_string_alloc();
    int32_t* chunk = NULL;
    bool ok = false;
    do {
        if(!flipper_format_write_header_cstr(ff, "Flipper SubGhz RAW File", 1)) break;
        uint32_t freq = instance->preset->frequency;
        if(!flipper_format_write_uint32(ff, "Frequency", &freq, 1)) break;
        const char* preset_name = furi_string_get_cstr(instance->preset->name);
        if(!flipper_format_write_string_cstr(ff, "Preset", preset_name)) break;
        if(furi_string_equal_str(instance->preset->name, "FuriHalSubGhzPresetCustom")) {
            if(!flipper_format_write_string_cstr(ff, "Custom_preset_module", "CC1101")) break;
            if(!flipper_format_write_hex(
                   ff, "Custom_preset_data", instance->preset->data, instance->preset->data_size))
                break;
        }
        if(!flipper_format_write_string_cstr(ff, "Protocol", "RAW")) break;
        chunk = malloc(sizeof(int32_t) * 512);
        if(!chunk) break;
        size_t start =
            (instance->rx_burst_head + instance->rx_ring_cap - instance->rx_burst_count) %
            instance->rx_ring_cap;
        size_t n = 0;
        bool werr = false;
        for(size_t i = 0; i < instance->rx_burst_count; i++) {
            LevelDuration ld = instance->rx_ring[(start + i) % instance->rx_ring_cap];
            int32_t d = (int32_t)level_duration_get_duration(ld);
            chunk[n++] = level_duration_get_level(ld) ? d : -d;
            if(n == 512) {
                if(!flipper_format_write_int32(ff, "RAW_Data", chunk, n)) {
                    werr = true;
                    break;
                }
                n = 0;
            }
        }
        if(!werr && n > 0 && !flipper_format_write_int32(ff, "RAW_Data", chunk, n)) werr = true;
        ok = !werr;
    } while(false);
    if(chunk) free(chunk);
    if(!ok) {
        flipper_format_free(ff);
        return NULL;
    }
    return ff;
}

void subghz_txrx_dual_set_ext_preset(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    furi_assert(instance);
    furi_string_set(instance->dual_ext_preset_name, preset_name);
    instance->dual_ext_frequency = frequency;
    instance->dual_ext_preset_data = preset_data;
    instance->dual_ext_preset_data_size = preset_data_size;
}

SubGhzRadioPreset subghz_txrx_dual_get_ext_preset(SubGhzTxRx* instance) {
    furi_assert(instance);
    SubGhzRadioPreset preset = {
        .name = instance->dual_ext_preset_name,
        .frequency = instance->dual_ext_frequency,
        .data = instance->dual_ext_preset_data,
        .data_size = instance->dual_ext_preset_data_size,
    };
    return preset;
}

bool subghz_txrx_dual_is_ext_connected(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->dual_worker || instance->radio_device_type != SubGhzRadioDeviceTypeInternal) {
        return true;
    }
    return subghz_txrx_radio_device_is_external_connected(instance, SUBGHZ_DEVICE_CC1101_EXT_NAME);
}

size_t subghz_txrx_dual_heap_needed(SubGhzTxRx* instance) {
    furi_assert(instance);
    size_t needed = subghz_txrx_receiver_cost(
                        instance,
                        subghz_txrx_current_modulation(instance),
                        SUBGHZ_TXRX_DUAL_EXCLUDE) +
                    subghz_txrx_receiver_cost(
                        instance,
                        subghz_txrx_preset_modulation(
                            instance->dual_ext_preset_data, instance->dual_ext_preset_data_size),
                        SUBGHZ_TXRX_DUAL_EXCLUDE);
    if(!instance->dual_worker) needed += SUBGHZ_TXRX_DUAL_WORKER_COST;
    return needed;
}

static void subghz_txrx_dual_ext_end(SubGhzTxRx* instance) {
    if(subghz_worker_is_running(instance->dual_worker)) {
        subghz_worker_stop(instance->dual_worker);
        subghz_devices_stop_async_rx(instance->dual_device);
    }
    subghz_devices_idle(instance->dual_device);
}

bool subghz_txrx_dual_rx_start(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(!instance->dual_ext_preset_data) return false;
    subghz_txrx_stop(instance);

    if(!instance->dual_worker) {
        const SubGhzDevice* ext = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        if(!ext) return false;
        if(instance->radio_device_type == SubGhzRadioDeviceTypeInternal) {
            if(!subghz_txrx_radio_device_is_external_connected(
                   instance, SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
                return false;
            }
            subghz_txrx_radio_device_power_on(instance);
            subghz_devices_begin(ext);
        } else {
            instance->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
            instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
        }
        subghz_txrx_receiver_release(instance);
        instance->dual_device = ext;
        instance->dual_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        instance->dual_worker = subghz_worker_alloc_ex(SUBGHZ_TXRX_DUAL_STREAM_SAMPLES);
        subghz_worker_set_overrun_callback(instance->dual_worker, subghz_txrx_dual_overrun);
        subghz_worker_set_pair_callback(instance->dual_worker, subghz_txrx_dual_pair);
        subghz_worker_set_context(instance->dual_worker, instance);
    } else {
        subghz_txrx_dual_ext_end(instance);
    }

    const SubGhzProtocolFlag ext_modulation = subghz_txrx_preset_modulation(
        instance->dual_ext_preset_data, instance->dual_ext_preset_data_size);
    if(!instance->dual_receiver || instance->dual_receiver_modulation != ext_modulation) {
        if(instance->dual_receiver) subghz_receiver_free(instance->dual_receiver);
        instance->dual_receiver = subghz_receiver_alloc_init_ex(
            instance->environment, ext_modulation, SUBGHZ_TXRX_DUAL_EXCLUDE);
        instance->dual_receiver_modulation = ext_modulation;
        subghz_txrx_receiver_setup(instance, instance->dual_receiver);
    }
    subghz_receiver_set_modulation_filter(instance->dual_receiver, ext_modulation);
    subghz_receiver_reset(instance->dual_receiver);

    subghz_txrx_begin(
        instance,
        subghz_setting_get_preset_data_by_name(
            instance->setting, furi_string_get_cstr(instance->preset->name)));
    subghz_txrx_rx(instance, instance->preset->frequency);

    const SubGhzDevice* ext = instance->dual_device;
    subghz_devices_reset(ext);
    subghz_devices_idle(ext);
    subghz_devices_load_preset(ext, FuriHalSubGhzPresetCustom, instance->dual_ext_preset_data);
    subghz_devices_set_frequency(ext, instance->dual_ext_frequency);
    subghz_devices_flush_rx(ext);
    subghz_devices_start_async_rx(ext, subghz_worker_rx_callback, instance->dual_worker);
    subghz_worker_start(instance->dual_worker);
    return true;
}

void subghz_txrx_dual_rx_stop(SubGhzTxRx* instance, SubGhzRadioDeviceType main_device) {
    furi_assert(instance);
    if(!instance->dual_worker) return;

    subghz_txrx_dual_ext_end(instance);
    subghz_txrx_stop(instance);

    subghz_worker_free(instance->dual_worker);
    instance->dual_worker = NULL;
    subghz_receiver_free(instance->dual_receiver);
    instance->dual_receiver = NULL;
    instance->dual_receiver_modulation = 0;
    furi_mutex_free(instance->dual_mutex);
    instance->dual_mutex = NULL;
    subghz_txrx_receiver_release(instance);

    if(main_device == SubGhzRadioDeviceTypeExternalCC1101) {
        instance->radio_device = instance->dual_device;
        instance->radio_device_type = SubGhzRadioDeviceTypeExternalCC1101;
    } else {
        subghz_devices_sleep(instance->dual_device);
        subghz_devices_end(instance->dual_device);
        subghz_txrx_radio_device_power_off(instance);
    }
    instance->dual_device = NULL;
}

bool subghz_txrx_dual_is_ext_receiver(SubGhzTxRx* instance, SubGhzReceiver* receiver) {
    furi_assert(instance);
    if(instance->dual_receiver) return receiver == instance->dual_receiver;
    return instance->radio_device_type != SubGhzRadioDeviceTypeInternal;
}

float subghz_txrx_dual_get_rssi_ext(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(!instance->dual_device) return -127.0f;
    return subghz_devices_get_rssi(instance->dual_device);
}

void subghz_txrx_dual_lock(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->dual_mutex) furi_mutex_acquire(instance->dual_mutex, FuriWaitForever);
}

void subghz_txrx_dual_unlock(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->dual_mutex) furi_mutex_release(instance->dual_mutex);
}
