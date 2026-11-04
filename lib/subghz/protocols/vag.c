#include "vag.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include "aut64.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>
#include <string.h>

#define TAG "VAGProtocol"

static const SubGhzBlockConst subghz_protocol_vag_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 80,
    .min_count_bit_for_found = 80,
};

#define VAG_T12_TE_SHORT     300u
#define VAG_T12_TE_LONG      600u
#define VAG_T12_TE_DELTA     100u
#define VAG_T12_GAP_DELTA    200u
#define VAG_T12_PREAMBLE_MIN 151u

#define VAG_T34_TE_SHORT     500u
#define VAG_T34_TE_LONG      1000u
#define VAG_T34_TE_DELTA     100u
#define VAG_T34_LONG_DELTA   200u
#define VAG_T34_SYNC         750u
#define VAG_T34_SYNC_DELTA   150u
#define VAG_T34_PREAMBLE_MIN 31u
#define VAG_T34_SYNC_PAIRS   3u

#define VAG_DATA_GAP_MIN    4001u
#define VAG_TOTAL_BITS      80u
#define VAG_KEY1_BITS       64u
#define VAG_PREFIX_BITS     15u
#define VAG_BIT_LIMIT       96u
#define VAG_FRAME_PREFIX_T1 0x2F3Fu
#define VAG_FRAME_PREFIX_T2 0x2F1Cu

#define VAG_KEYS_COUNT 3
#define VAG_ENCODER_UPLOAD_MAX_SIZE 2560

static const uint8_t vag_keys_packed[VAG_KEYS_COUNT][AUT64_KEY_STRUCT_PACKED_SIZE] = {
    {0x01, 0x37, 0x6C, 0x86, 0xAD, 0xAB, 0xCC, 0x43, 0x07, 0x4D, 0xE8, 0x59, 0xC1, 0x2F, 0x36, 0xAB},
    {0x02, 0x37, 0x7C, 0x65, 0xCE, 0xDC, 0x42, 0xEA, 0xA4, 0x53, 0xE8, 0x61, 0xD9, 0xB7, 0x20, 0xFC},
    {0x03, 0x8A, 0xA3, 0x7B, 0x1E, 0x56, 0x1F, 0x83, 0x84, 0xB6, 0x19, 0xC5, 0x2E, 0x0A, 0x3F, 0xD7}
};

static struct aut64_key protocol_vag_keys[VAG_KEYS_COUNT];
static int8_t protocol_vag_keys_loaded = -1;

static void protocol_vag_load_keys(void) {
    if(protocol_vag_keys_loaded >= 0) {
        return;
    }

    for(uint8_t i = 0; i < VAG_KEYS_COUNT; i++) {
        aut64_unpack(&protocol_vag_keys[i], vag_keys_packed[i]);
    }

    protocol_vag_keys_loaded = 0;
}

static struct aut64_key* protocol_vag_get_key(uint8_t index) {
    for(uint8_t i = 0; i < VAG_KEYS_COUNT; i++) {
        if(protocol_vag_keys[i].index == index) {
            return &protocol_vag_keys[i];
        }
    }

    return NULL;
}

#define VAG_TEA_DELTA  0x9E3779B9U
#define VAG_TEA_ROUNDS 32

static const uint32_t vag_tea_key_schedule[] = {0x0B46502D, 0x5E253718, 0x2BF93A19, 0x622C1206};

static uint8_t vag_btn_to_custom(uint8_t btn) {
    switch(btn) {
        case 0x10: return 2;
        case 0x20: return 1;
        case 0x40:
        case 0x80: return 3;
        default: return 1;
    }
}

typedef struct SubGhzProtocolDecoderVAG {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t data_low;
    uint32_t data_high;
    uint8_t bit_count;
    uint32_t key1_low;
    uint32_t key1_high;
    uint32_t key2_low;
    uint32_t key2_high;
    uint16_t data_count_bit;
    uint8_t vag_type;
    uint16_t header_count;
    uint8_t mid_count;
    ManchesterState manchester_state;

    uint32_t serial;
    uint32_t cnt;
    uint8_t btn;
    uint8_t check_byte;
    uint8_t key_idx;
    bool decrypted;

    uint32_t last_valid_serial;
    uint32_t last_valid_cnt;
} SubGhzProtocolDecoderVAG;

typedef enum {
    VAGDecoderStepReset = 0,
    VAGDecoderStepPreamble1 = 1,
    VAGDecoderStepData1 = 2,
    VAGDecoderStepPreamble2 = 3,
    VAGDecoderStepSync2A = 4,
    VAGDecoderStepSync2B = 5,
    VAGDecoderStepSync2C = 6,
    VAGDecoderStepData2 = 7,
} VAGDecoderStep;

static void vag_tea_decrypt(uint32_t* v0, uint32_t* v1, const uint32_t* key_schedule) {
    uint32_t sum = VAG_TEA_DELTA * VAG_TEA_ROUNDS;
    for(int i = 0; i < VAG_TEA_ROUNDS; i++) {
        *v1 -= (((*v0 << 4) ^ (*v0 >> 5)) + *v0) ^ (sum + key_schedule[(sum >> 11) & 3]);
        sum -= VAG_TEA_DELTA;
        *v0 -= (((*v1 << 4) ^ (*v1 >> 5)) + *v1) ^ (sum + key_schedule[sum & 3]);
    }
}

static bool vag_dispatch_type_1_2(uint8_t dispatch) {
    return (dispatch == 0x2A || dispatch == 0x1C || dispatch == 0x46);
}

static bool vag_dispatch_type_3_4(uint8_t dispatch) {
    return (dispatch == 0x2B || dispatch == 0x1D || dispatch == 0x47);
}

static bool vag_button_valid(const uint8_t* dec) {
    uint8_t dec_byte = dec[7];
    uint8_t dec_btn = (dec_byte >> 4) & 0xF;

    if(dec_btn == 1 || dec_btn == 2 || dec_btn == 4) {
        return true;
    }
    if(dec_byte == 0) {
        return true;
    }
    return false;
}

static bool vag_button_matches(const uint8_t* dec, uint8_t dispatch_byte) {
    uint8_t expected_btn = (dispatch_byte >> 4) & 0xF;
    uint8_t dec_btn = (dec[7] >> 4) & 0xF;

    if(dec_btn == expected_btn) {
        return true;
    }
    if(dec[7] == 0 && expected_btn == 2) {
        return true;
    }
    return false;
}

static void vag_fill_from_decrypted(
    SubGhzProtocolDecoderVAG* instance,
    const uint8_t* dec,
    uint8_t dispatch_byte) {
    uint32_t serial_raw = (uint32_t)dec[0] | ((uint32_t)dec[1] << 8) | ((uint32_t)dec[2] << 16) |
                          ((uint32_t)dec[3] << 24);
    instance->serial = (serial_raw << 24) | ((serial_raw & 0xFF00) << 8) |
                       ((serial_raw >> 8) & 0xFF00) | (serial_raw >> 24);

    instance->cnt = (uint32_t)dec[4] | ((uint32_t)dec[5] << 8) | ((uint32_t)dec[6] << 16);

    instance->btn = (dec[7] >> 4) & 0xF;
    instance->check_byte = dispatch_byte;
    instance->decrypted = true;
}

static bool vag_aut64_decrypt(uint8_t* block, int key_index) {
    struct aut64_key* key = protocol_vag_get_key(key_index + 1);
    if(!key) {
        return false;
    }
    aut64_decrypt(*key, block);
    return true;
}

static void vag_parse_data(SubGhzProtocolDecoderVAG* instance) {
    furi_assert(instance);

    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;

    uint8_t dispatch_byte = (uint8_t)(instance->key2_low & 0xFF);
    uint8_t key2_high = (uint8_t)((instance->key2_low >> 8) & 0xFF);

    uint8_t key1_bytes[8];
    uint32_t key1_low = instance->key1_low;
    uint32_t key1_high = instance->key1_high;

    key1_bytes[0] = (uint8_t)(key1_high >> 24);
    key1_bytes[1] = (uint8_t)(key1_high >> 16);
    key1_bytes[2] = (uint8_t)(key1_high >> 8);
    key1_bytes[3] = (uint8_t)(key1_high);
    key1_bytes[4] = (uint8_t)(key1_low >> 24);
    key1_bytes[5] = (uint8_t)(key1_low >> 16);
    key1_bytes[6] = (uint8_t)(key1_low >> 8);
    key1_bytes[7] = (uint8_t)(key1_low);

    uint8_t block[8];
    block[0] = key1_bytes[1];
    block[1] = key1_bytes[2];
    block[2] = key1_bytes[3];
    block[3] = key1_bytes[4];
    block[4] = key1_bytes[5];
    block[5] = key1_bytes[6];
    block[6] = key1_bytes[7];
    block[7] = key2_high;

    switch(instance->vag_type) {
    case 1:
        if(!vag_dispatch_type_1_2(dispatch_byte)) {
            break;
        }
        {
            uint8_t block_copy[8];

            for(int key_idx = 0; key_idx < 3; key_idx++) {
                memcpy(block_copy, block, 8);
                if(!vag_aut64_decrypt(block_copy, key_idx)) {
                    continue;
                }

                if(vag_button_valid(block_copy)) {
                    instance->serial = ((uint32_t)block_copy[0] << 24) |
                                       ((uint32_t)block_copy[1] << 16) |
                                       ((uint32_t)block_copy[2] << 8) | (uint32_t)block_copy[3];
                    instance->cnt = (uint32_t)block_copy[4] | ((uint32_t)block_copy[5] << 8) |
                                    ((uint32_t)block_copy[6] << 16);

                    instance->btn = block_copy[7];
                    instance->check_byte = dispatch_byte;
                    instance->key_idx = key_idx;
                    instance->decrypted = true;
                    return;
                }
            }
        }
        break;

    case 2:
        if(!vag_dispatch_type_1_2(dispatch_byte)) {
            break;
        }
        {
            uint32_t v0_orig = ((uint32_t)block[0] << 24) | ((uint32_t)block[1] << 16) |
                               ((uint32_t)block[2] << 8) | (uint32_t)block[3];
            uint32_t v1_orig = ((uint32_t)block[4] << 24) | ((uint32_t)block[5] << 16) |
                               ((uint32_t)block[6] << 8) | (uint32_t)block[7];

            {
                uint32_t v0 = v0_orig;
                uint32_t v1 = v1_orig;

                vag_tea_decrypt(&v0, &v1, vag_tea_key_schedule);

                uint8_t tea_dec[8];
                tea_dec[0] = (uint8_t)(v0 >> 24);
                tea_dec[1] = (uint8_t)(v0 >> 16);
                tea_dec[2] = (uint8_t)(v0 >> 8);
                tea_dec[3] = (uint8_t)(v0);
                tea_dec[4] = (uint8_t)(v1 >> 24);
                tea_dec[5] = (uint8_t)(v1 >> 16);
                tea_dec[6] = (uint8_t)(v1 >> 8);
                tea_dec[7] = (uint8_t)(v1);

                if(!vag_button_matches(tea_dec, dispatch_byte)) {
                    break;
                }

                vag_fill_from_decrypted(instance, tea_dec, dispatch_byte);
                instance->key_idx = 0xFF;

                return;
            }
        }
        break;

    case 3: {
        uint8_t block_copy[8];

        memcpy(block_copy, block, 8);
        if(vag_aut64_decrypt(block_copy, 2) && vag_button_valid(block_copy)) {
            instance->vag_type = 4;
            instance->key_idx = 2;
            vag_fill_from_decrypted(instance, block_copy, dispatch_byte);
            return;
        }

        memcpy(block_copy, block, 8);
        if(vag_aut64_decrypt(block_copy, 1) && vag_button_valid(block_copy)) {
            instance->key_idx = 1;
            vag_fill_from_decrypted(instance, block_copy, dispatch_byte);
            return;
        }

        memcpy(block_copy, block, 8);
        if(vag_aut64_decrypt(block_copy, 0) && vag_button_valid(block_copy)) {
            instance->key_idx = 0;
            vag_fill_from_decrypted(instance, block_copy, dispatch_byte);
            return;
        }

    } break;

    case 4:
        if(!vag_dispatch_type_3_4(dispatch_byte)) {
            break;
        }
        {
            uint8_t block_copy[8];
            memcpy(block_copy, block, 8);

            if(!vag_aut64_decrypt(block_copy, 2)) {
                break;
            }
            if(!vag_button_matches(block_copy, dispatch_byte)) {
                break;
            }
            instance->key_idx = 2;
            vag_fill_from_decrypted(instance, block_copy, dispatch_byte);
        }
        return;

    default:
        break;
    }

    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;
    instance->check_byte = 0;
}

const SubGhzProtocolDecoder subghz_protocol_vag_decoder = {
    .alloc = subghz_protocol_decoder_vag_alloc,
    .free = subghz_protocol_decoder_vag_free,
    .feed = subghz_protocol_decoder_vag_feed,
    .reset = subghz_protocol_decoder_vag_reset,
    .get_hash_data = subghz_protocol_decoder_vag_get_hash_data,
    .serialize = subghz_protocol_decoder_vag_serialize,
    .deserialize = subghz_protocol_decoder_vag_deserialize,
    .get_string = subghz_protocol_decoder_vag_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    __typeof__(&aut64_decrypt) aut64_decrypt;
    __typeof__(&aut64_encrypt) aut64_encrypt;
    __typeof__(&aut64_unpack) aut64_unpack;
    void* protocol_vag_keys;
    void* protocol_vag_keys_loaded;
} enc_vag_host_t;
static const enc_vag_host_t enc_vag_host = {
    .aut64_decrypt = aut64_decrypt,
    .aut64_encrypt = aut64_encrypt,
    .aut64_unpack = aut64_unpack,
    .protocol_vag_keys = (void*)&protocol_vag_keys,
    .protocol_vag_keys_loaded = (void*)&protocol_vag_keys_loaded,
};
static const SubGhzEncoderExternal subghz_protocol_vag_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_vag", &enc_vag_host);

const SubGhzProtocol subghz_protocol_vag = {
    .name = VAG_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_vag_decoder,
    .encoder = &subghz_protocol_vag_encoder_ext.vtable,
};

void* subghz_protocol_decoder_vag_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderVAG* instance = malloc(sizeof(SubGhzProtocolDecoderVAG));
    instance->base.protocol = &subghz_protocol_vag;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;
    instance->check_byte = 0;
    instance->key_idx = 0xFF;
    instance->last_valid_serial = 0;
    instance->last_valid_cnt = 0;

    protocol_vag_load_keys();

    return instance;
}

void subghz_protocol_decoder_vag_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;
    free(instance);
}

void subghz_protocol_decoder_vag_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;
    instance->decoder.parser_step = VAGDecoderStepReset;
    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;
    instance->check_byte = 0;
    instance->key_idx = 0xFF;
}

void subghz_protocol_decoder_vag_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;

    switch(instance->decoder.parser_step) {
    case VAGDecoderStepReset:
        if(!level) break;
        if(DURATION_DIFF(duration, VAG_T12_TE_SHORT) < VAG_T12_TE_DELTA) {
            instance->decoder.parser_step = VAGDecoderStepPreamble1;
        } else if(DURATION_DIFF(duration, VAG_T34_TE_SHORT) < VAG_T34_TE_DELTA) {
            instance->decoder.parser_step = VAGDecoderStepPreamble2;
        } else {
            break;
        }
        instance->data_low = 0;
        instance->data_high = 0;
        instance->header_count = 0;
        instance->mid_count = 0;
        instance->bit_count = 0;
        instance->vag_type = 0;
        instance->decoder.te_last = duration;
        manchester_advance(
            instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
        break;

    case VAGDecoderStepPreamble1:
        if(level) break;
        if(DURATION_DIFF(duration, VAG_T12_TE_SHORT) < VAG_T12_TE_DELTA) {
            if(DURATION_DIFF(instance->decoder.te_last, VAG_T12_TE_SHORT) < VAG_T12_TE_DELTA) {
                instance->decoder.te_last = duration;
                instance->header_count++;
            } else {
                instance->decoder.parser_step = VAGDecoderStepReset;
            }
            break;
        }
        instance->decoder.parser_step = VAGDecoderStepReset;
        if(instance->header_count < VAG_T12_PREAMBLE_MIN) break;
        if(DURATION_DIFF(duration, VAG_T12_TE_LONG) >= VAG_T12_GAP_DELTA) break;
        if(DURATION_DIFF(instance->decoder.te_last, VAG_T12_TE_SHORT) >= VAG_T12_TE_DELTA) break;
        instance->decoder.parser_step = VAGDecoderStepData1;
        break;

    case VAGDecoderStepData1: {
        if(instance->bit_count >= VAG_BIT_LIMIT) {
            instance->decoder.parser_step = VAGDecoderStepReset;
            break;
        }

        bool bit_value = false;
        ManchesterEvent event = ManchesterEventReset;
        bool got_pulse = false;

        if(DURATION_DIFF(duration, VAG_T12_TE_SHORT) < VAG_T12_TE_DELTA) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            got_pulse = true;
        } else if(
            duration > VAG_T12_TE_SHORT + VAG_T12_TE_DELTA &&
            DURATION_DIFF(duration, VAG_T12_TE_LONG) < VAG_T12_GAP_DELTA) {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
            got_pulse = true;
        }

        if(got_pulse) {
            if(manchester_advance(
                   instance->manchester_state, event, &instance->manchester_state, &bit_value)) {
                uint32_t carry = (instance->data_low >> 31) & 1;
                instance->data_low = (instance->data_low << 1) | (bit_value ? 1 : 0);
                instance->data_high = (instance->data_high << 1) | carry;
                instance->bit_count++;

                if(instance->bit_count == VAG_PREFIX_BITS) {
                    if(instance->data_low == VAG_FRAME_PREFIX_T1 && instance->data_high == 0) {
                        instance->data_low = 0;
                        instance->data_high = 0;
                        instance->bit_count = 0;
                        instance->vag_type = 1;
                    } else if(instance->data_low == VAG_FRAME_PREFIX_T2 && instance->data_high == 0) {
                        instance->data_low = 0;
                        instance->data_high = 0;
                        instance->bit_count = 0;
                        instance->vag_type = 2;
                    }
                } else if(instance->bit_count == VAG_KEY1_BITS) {
                    instance->key1_low = ~instance->data_low;
                    instance->key1_high = ~instance->data_high;
                    instance->data_low = 0;
                    instance->data_high = 0;
                }
            }
            break;
        }

        if(level) break;
        if(duration < VAG_DATA_GAP_MIN) break;
        if(instance->bit_count == VAG_TOTAL_BITS) {
            instance->key2_low = (~instance->data_low) & 0xFFFF;
            instance->key2_high = 0;
            instance->data_count_bit = VAG_TOTAL_BITS;

            vag_parse_data(instance);

            if(instance->base.callback && instance->decrypted) {
                bool is_valid = true;
                if(instance->last_valid_serial != 0) {
                    if(instance->serial == instance->last_valid_serial) {
                        uint32_t cnt_diff = instance->cnt > instance->last_valid_cnt ?
                            instance->cnt - instance->last_valid_cnt :
                            instance->last_valid_cnt - instance->cnt;
                        if(cnt_diff > 100) {
                            is_valid = false;
                        }
                    }
                }
                if(is_valid) {
                    instance->last_valid_serial = instance->serial;
                    instance->last_valid_cnt = instance->cnt;
                    instance->base.callback(&instance->base, instance->base.context);
                }
            }
        }
        instance->data_low = 0;
        instance->data_high = 0;
        instance->bit_count = 0;
        instance->decoder.parser_step = VAGDecoderStepReset;
        break;
    }

    case VAGDecoderStepPreamble2:
        if(!level) {
            if(DURATION_DIFF(duration, VAG_T34_TE_SHORT) < VAG_T34_TE_DELTA &&
               DURATION_DIFF(instance->decoder.te_last, VAG_T34_TE_SHORT) < VAG_T34_TE_DELTA) {
                instance->decoder.te_last = duration;
                instance->header_count++;
            } else {
                instance->decoder.parser_step = VAGDecoderStepReset;
            }
            break;
        }
        if(instance->header_count < VAG_T34_PREAMBLE_MIN) break;
        if(DURATION_DIFF(duration, VAG_T34_TE_LONG) >= VAG_T34_LONG_DELTA) break;
        if(DURATION_DIFF(instance->decoder.te_last, VAG_T34_TE_SHORT) >= VAG_T34_TE_DELTA) break;
        instance->decoder.te_last = duration;
        instance->decoder.parser_step = VAGDecoderStepSync2A;
        break;

    case VAGDecoderStepSync2A:
        if(!level &&
           DURATION_DIFF(duration, VAG_T34_TE_SHORT) < VAG_T34_TE_DELTA &&
           DURATION_DIFF(instance->decoder.te_last, VAG_T34_TE_LONG) < VAG_T34_LONG_DELTA) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = VAGDecoderStepSync2B;
        } else {
            instance->decoder.parser_step = VAGDecoderStepReset;
        }
        break;

    case VAGDecoderStepSync2B:
        if(level && DURATION_DIFF(duration, VAG_T34_SYNC) < VAG_T34_SYNC_DELTA) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = VAGDecoderStepSync2C;
        } else {
            instance->decoder.parser_step = VAGDecoderStepReset;
        }
        break;

    case VAGDecoderStepSync2C:
        if(!level &&
           DURATION_DIFF(duration, VAG_T34_SYNC) < VAG_T34_SYNC_DELTA &&
           DURATION_DIFF(instance->decoder.te_last, VAG_T34_SYNC) < VAG_T34_SYNC_DELTA) {
            instance->mid_count++;
            instance->decoder.parser_step = VAGDecoderStepSync2B;
            if(instance->mid_count == VAG_T34_SYNC_PAIRS) {
                instance->data_low = 1;
                instance->data_high = 0;
                instance->bit_count = 1;
                manchester_advance(
                    instance->manchester_state,
                    ManchesterEventReset,
                    &instance->manchester_state,
                    NULL);
                instance->decoder.parser_step = VAGDecoderStepData2;
            }
        } else {
            instance->decoder.parser_step = VAGDecoderStepReset;
        }
        break;

    case VAGDecoderStepData2: {
        bool bit_value = false;
        ManchesterEvent event = ManchesterEventReset;
        bool got_pulse = false;

        if(DURATION_DIFF(duration, VAG_T34_TE_SHORT) < VAG_T34_TE_DELTA) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            got_pulse = true;
        } else if(DURATION_DIFF(duration, VAG_T34_TE_LONG) < VAG_T34_LONG_DELTA) {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
            got_pulse = true;
        }

        if(got_pulse) {
            if(manchester_advance(
                   instance->manchester_state, event, &instance->manchester_state, &bit_value)) {
                uint32_t carry = (instance->data_low >> 31) & 1;
                instance->data_low = (instance->data_low << 1) | (bit_value ? 1 : 0);
                instance->data_high = (instance->data_high << 1) | carry;
                instance->bit_count++;

                if(instance->bit_count == VAG_KEY1_BITS) {
                    instance->key1_low = instance->data_low;
                    instance->key1_high = instance->data_high;
                    instance->data_low = 0;
                    instance->data_high = 0;
                }
            }
        }

        if(instance->bit_count != VAG_TOTAL_BITS) break;
        instance->key2_low = instance->data_low & 0xFFFF;
        instance->key2_high = 0;
        instance->data_count_bit = VAG_TOTAL_BITS;
        instance->vag_type = 3;
        vag_parse_data(instance);
        if(instance->base.callback && instance->decrypted) {
            bool is_valid = true;
            if(instance->last_valid_serial != 0) {
                if(instance->serial == instance->last_valid_serial) {
                    uint32_t cnt_diff = instance->cnt > instance->last_valid_cnt ?
                        instance->cnt - instance->last_valid_cnt :
                        instance->last_valid_cnt - instance->cnt;
                    if(cnt_diff > 100) {
                        is_valid = false;
                    }
                }
            }
            if(is_valid) {
                instance->last_valid_serial = instance->serial;
                instance->last_valid_cnt = instance->cnt;
                instance->base.callback(&instance->base, instance->base.context);
            }
        }
        instance->data_low = 0;
        instance->data_high = 0;
        instance->bit_count = 0;
        instance->decoder.parser_step = VAGDecoderStepReset;
        break;
    }

    default:
        instance->decoder.parser_step = VAGDecoderStepReset;
        break;
    }
}

uint8_t subghz_protocol_decoder_vag_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;
    uint8_t hash = 0;
    hash ^= (instance->key1_low & 0xFF);
    hash ^= ((instance->key1_low >> 8) & 0xFF);
    hash ^= ((instance->key1_low >> 16) & 0xFF);
    hash ^= ((instance->key1_low >> 24) & 0xFF);
    hash ^= (instance->key1_high & 0xFF);
    hash ^= ((instance->key1_high >> 8) & 0xFF);
    hash ^= ((instance->key1_high >> 16) & 0xFF);
    hash ^= ((instance->key1_high >> 24) & 0xFF);
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_vag_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;

    if(!instance->decrypted && instance->data_count_bit >= 80) {
        vag_parse_data(instance);
    }

    uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    uint16_t key2_16bit = (uint16_t)(instance->key2_low & 0xFFFF);

    instance->generic.data = key1;
    instance->generic.data_count_bit = instance->data_count_bit;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint8_t key2_bytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        key2_bytes[6] = (uint8_t)((key2_16bit >> 8) & 0xFF);
        key2_bytes[7] = (uint8_t)(key2_16bit & 0xFF);
        flipper_format_write_hex(flipper_format, "Key2", key2_bytes, 8);

        uint32_t type = instance->vag_type;
        flipper_format_write_uint32(flipper_format, "Type", &type, 1);

        if(instance->decrypted && instance->key_idx != 0xFF) {
            uint32_t key_idx_temp = instance->key_idx;
            flipper_format_write_uint32(flipper_format, "KeyIdx", &key_idx_temp, 1);
        }
        if(instance->decrypted) {
            uint32_t cnt_tmp = instance->cnt;
            flipper_format_write_uint32(flipper_format, "Cnt", &cnt_tmp, 1);
            uint32_t serial_tmp = instance->serial;
            flipper_format_write_uint32(flipper_format, "Serial", &serial_tmp, 1);
        }
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_vag_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_vag_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        uint64_t key1 = instance->generic.data;
        instance->key1_low = (uint32_t)key1;
        instance->key1_high = (uint32_t)(key1 >> 32);

        uint8_t key2_bytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(flipper_format, "Key2", key2_bytes, 8)) {
            uint16_t key2_16bit = ((uint16_t)key2_bytes[6] << 8) | (uint16_t)key2_bytes[7];
            instance->key2_low = (uint32_t)key2_16bit & 0xFFFF;
            instance->key2_high = 0;
        }

        uint32_t type = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Type", &type, 1)) {
            instance->vag_type = (uint8_t)type;
        }

        instance->data_count_bit = instance->generic.data_count_bit;

        instance->decrypted = false;
        vag_parse_data(instance);

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(vag_btn_to_custom(instance->btn));
        }
        subghz_custom_btn_set_max(4);
    }

    return ret;
}

typedef struct SubGhzProtocolEncoderVAG {
    SubGhzProtocolEncoderBase base;
    SubGhzBlockGeneric generic;

    uint32_t key1_low;
    uint32_t key1_high;
    uint32_t key2_low;
    uint32_t key2_high;
    uint32_t serial;
    uint32_t cnt;
    uint8_t vag_type;
    uint8_t btn;
    uint8_t dispatch_byte;
    uint8_t key_idx;

    size_t repeat;
    size_t front;
    size_t size_upload;
    LevelDuration* upload;
    bool is_running;
} SubGhzProtocolEncoderVAG;

void subghz_protocol_decoder_vag_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;
    // Detect/decode split (FoxFW2.0 v2.1): AUT64 decrypt uses the runtime-loaded
    // key table, so it stays internal; the SD plugin only formats.
    if(!instance->decrypted && instance->data_count_bit >= 80) {
        vag_parse_data(instance);
    }
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}


