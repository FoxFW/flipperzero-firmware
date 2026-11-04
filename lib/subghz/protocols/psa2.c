#include "psa2.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolPSA"

static const SubGhzBlockConst subghz_protocol_psa_const = {
    .te_short = 250,
    .te_long  = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 128,
};

#define PSA_TE_SHORT_STD   250
#define PSA_TE_SHORT_HALF  125
#define PSA_TE_LONG_STD    500
#define PSA_TE_LONG_HALF   250
#define PSA_TE_END_1000    1000
#define PSA_TE_END_500     500

#define TEA_DELTA  0x9E3779B9U
#define TEA_ROUNDS 32

#define PSA_BF1_START  0x23000000U
#define PSA_BF1_END    0x24000000U

#define PSA_BF1_CONST_U4  0x0E0F5C41U
#define PSA_BF1_CONST_U5  0x0F5C4123U

#define PSA_BF2_START  0xF3000000U
#define PSA_BF2_END    0xF4000000U

#define PSA_VALID_NIBBLE  0xA

#define PSA_BTN_LOCK    0x0
#define PSA_BTN_UNLOCK  0x1
#define PSA_BTN_TRUNK   0x2

#define PSA_KEY1_BITS  64
#define PSA_KEY2_BITS  80
#define PSA_MAX_BITS   121

#define PSA_MODE_UNKNOWN  0x00
#define PSA_MODE_23       0x23
#define PSA_MODE_36       0x36

typedef enum {
    PSADecoderState0 = 0,
    PSADecoderState1,
    PSADecoderState2,
    PSADecoderState3,
    PSADecoderState4,
} PSADecoderState;

struct SubGhzProtocolDecoderPSA {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t state;
    uint32_t prev_duration;

    uint32_t decode_data_low;
    uint32_t decode_data_high;
    uint8_t  decode_count_bit;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t key2_high;

    uint32_t status_flag;

    uint8_t  mode_serialize;

    uint16_t decrypted;

    uint8_t  decrypted_button;
    uint32_t decrypted_serial;
    uint32_t decrypted_counter;
    uint16_t decrypted_crc;
    uint32_t decrypted_seed;
    uint8_t  decrypted_type;

    uint16_t       pattern_counter;
    ManchesterState manchester_state;

    uint32_t last_key1_low;
    uint32_t last_key1_high;

    uint8_t bf_attempted;
};

struct SubGhzProtocolEncoderPSA {
    SubGhzProtocolEncoderBase  base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric         generic;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t counter;
    uint8_t  button;
    uint8_t  type;
    uint8_t  seed;
    uint8_t  mode;
    uint32_t serial;
    uint16_t crc;
    bool     is_running;
};


const SubGhzProtocolDecoder subghz_protocol_psa2_decoder = {
    .alloc        = subghz_protocol_decoder_psa2_alloc,
    .free         = subghz_protocol_decoder_common_free,
    .feed         = subghz_protocol_decoder_psa2_feed,
    .reset        = subghz_protocol_decoder_psa2_reset,
    .get_hash_data = subghz_protocol_decoder_psa2_get_hash_data,
    .serialize    = subghz_protocol_decoder_psa2_serialize,
    .deserialize  = subghz_protocol_decoder_psa2_deserialize,
    .get_string   = subghz_protocol_decoder_psa2_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_psa2_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_psa2");

const SubGhzProtocol subghz_protocol_psa2 = {
    .name    = SUBGHZ_PROTOCOL_PSA2_NAME,
    .type    = SubGhzProtocolTypeDynamic,
    .flag    = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM |
               SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_psa2_decoder,
    .encoder = &subghz_protocol_psa2_encoder_ext.vtable,
};

static void psa_setup_byte_buffer(uint8_t* buf,
                                   uint32_t key1_low, uint32_t key1_high,
                                   uint32_t key2_low) {

    for(int i = 0; i < 8; i++) {
        int shift = i * 8;
        uint8_t b;
        if(shift < 32)
            b = (uint8_t)(key1_low  >> shift);
        else
            b = (uint8_t)(key1_high >> (shift - 32));
        buf[7 - i] = b;
    }
    buf[9] = (uint8_t)(key2_low & 0xFF);
    buf[8] = (uint8_t)((key2_low >> 8) & 0xFF);
}

static void psa_calculate_checksum(uint8_t* buf) {
    uint32_t sum = 0;
    for(int i = 2; i < 8; i++)
        sum += (buf[i] & 0xF) + ((buf[i] >> 4) & 0xF);
    buf[11] = (uint8_t)((sum * 0x10) & 0xFF);
}

static void psa_copy_reverse(uint8_t* temp, const uint8_t* src) {
    temp[0] = src[5]; temp[1] = src[4];
    temp[2] = src[3]; temp[3] = src[2];
    temp[4] = src[9]; temp[5] = src[8];
    temp[6] = src[7]; temp[7] = src[6];
}

static void psa_second_stage_xor_decrypt(uint8_t* buf) {
    uint8_t t[8];
    psa_copy_reverse(t, buf);
    buf[2] = t[0] ^ t[6];
    buf[3] = t[2] ^ t[0];
    buf[4] = t[6] ^ t[3];
    buf[5] = t[7] ^ t[1];
    buf[6] = t[3] ^ t[1];
    buf[7] = t[6] ^ t[4] ^ t[5];
}

static void psa_extract_fields_mode23(uint8_t* buf, SubGhzProtocolDecoderPSA* inst) {
    inst->decrypted_button  = buf[8] & 0xF;
    inst->decrypted_serial  = ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 8) | buf[4];
    inst->decrypted_counter = (uint32_t)buf[6] | ((uint32_t)buf[5] << 8);
    inst->decrypted_crc     = buf[7];
    inst->decrypted_type    = PSA_MODE_23;
    inst->decrypted_seed    = inst->decrypted_serial;
}

static bool psa_direct_xor_decrypt(SubGhzProtocolDecoderPSA* inst) {
    uint8_t buf[48] = {0};
    psa_setup_byte_buffer(buf, inst->key1_low, inst->key1_high, inst->key2_low);
    psa_calculate_checksum(buf);

    uint8_t checksum  = buf[11];
    uint8_t key2_high = buf[8];
    uint8_t validation = (checksum ^ key2_high) & 0xF0;

    if(validation == 0) {

        buf[8] = (buf[8] & 0x0F) | (checksum & 0xF0);
        buf[13] = buf[9] ^ buf[8];
        psa_second_stage_xor_decrypt(buf);
        psa_extract_fields_mode23(buf, inst);
        return true;
    }
    return false;
}

static void psa_decrypt_fast(SubGhzProtocolDecoderPSA* inst) {
    if(psa_direct_xor_decrypt(inst)) {
        inst->mode_serialize = PSA_MODE_23;
        inst->decrypted = 0x50;
    } else {
        inst->decrypted      = 0x00;
        inst->mode_serialize = PSA_MODE_36;
    }
}

void* subghz_protocol_decoder_psa2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPSA* inst = malloc(sizeof(SubGhzProtocolDecoderPSA));
    if(inst) {
        memset(inst, 0, sizeof(SubGhzProtocolDecoderPSA));
        inst->base.protocol = &subghz_protocol_psa2;
        inst->generic.protocol_name = inst->base.protocol->name;
        inst->manchester_state = ManchesterStateMid1;
    }
    return inst;
}



void subghz_protocol_decoder_psa2_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;
    inst->state            = PSADecoderState0;
    inst->status_flag      = 0;
    inst->mode_serialize   = 0;
    inst->key1_low         = 0; inst->key1_high = 0;
    inst->key2_low         = 0; inst->key2_high = 0;
    inst->validation_field = 0;
    inst->decode_data_low  = 0; inst->decode_data_high = 0;
    inst->decode_count_bit = 0;
    inst->pattern_counter  = 0;
    inst->manchester_state = ManchesterStateMid1;
    inst->prev_duration    = 0;
    inst->decrypted        = 0;
    inst->decrypted_button = 0; inst->decrypted_serial  = 0;
    inst->decrypted_counter= 0; inst->decrypted_crc     = 0;
    inst->decrypted_seed   = 0; inst->decrypted_type    = 0;
    inst->bf_attempted     = 0;
}

#define PSA_FIRE_CALLBACK_IF_NEW(inst)                                      \
    do {                                                                     \
        bool _dup = ((inst)->key1_low  == (inst)->last_key1_low  &&         \
                     (inst)->key1_high == (inst)->last_key1_high);          \
        if(!_dup) {                                                          \
            (inst)->last_key1_low  = (inst)->key1_low;                      \
            (inst)->last_key1_high = (inst)->key1_high;                     \
            if((inst)->base.callback)                                        \
                (inst)->base.callback(&(inst)->base, (inst)->base.context); \
        }                                                                    \
    } while(0)

static inline bool psa_duration_near(uint32_t dur, uint32_t target, uint32_t tol) {
    return (dur >= target - tol) && (dur <= target + tol);
}

void subghz_protocol_decoder_psa2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;

    uint32_t te_s = subghz_protocol_psa_const.te_short;
    uint32_t te_l = subghz_protocol_psa_const.te_long;
    uint32_t tol  = subghz_protocol_psa_const.te_delta;

    uint32_t te_hs = PSA_TE_SHORT_HALF;
    uint32_t te_hl = PSA_TE_LONG_HALF;
    uint32_t tol_h = 50;

    switch(inst->state) {

    case PSADecoderState0:
        if(!level) return;
        inst->decode_data_low  = 0;
        inst->decode_data_high = 0;
        inst->pattern_counter  = 0;
        inst->decode_count_bit = 0;
        inst->mode_serialize   = 0;
        inst->prev_duration    = duration;
        manchester_advance(inst->manchester_state, ManchesterEventReset,
                           &inst->manchester_state, NULL);

        if(psa_duration_near(duration, te_s, tol)) {
            inst->state = PSADecoderState1;
        } else if(psa_duration_near(duration, te_hs, tol_h)) {
            inst->state = PSADecoderState3;
        }

        break;

    case PSADecoderState1:
        if(level) return;
        if(psa_duration_near(duration, te_s, tol)) {
            if(psa_duration_near(inst->prev_duration, te_s, tol))
                inst->pattern_counter++;
            inst->prev_duration = duration;
            return;
        }
        if(psa_duration_near(duration, te_l, tol)) {
            if(inst->pattern_counter > 0x46) {
                inst->decode_data_low  = 0;
                inst->decode_data_high = 0;
                inst->decode_count_bit = 0;
                manchester_advance(inst->manchester_state, ManchesterEventReset,
                                   &inst->manchester_state, NULL);
                inst->state = PSADecoderState2;
            }
            inst->pattern_counter = 0;
            inst->prev_duration   = duration;
            return;
        }
        inst->state           = PSADecoderState0;
        inst->pattern_counter = 0;
        break;

    case PSADecoderState2: {
        if(inst->decode_count_bit >= PSA_MAX_BITS) {
            inst->state = PSADecoderState0;
            break;
        }

        if(level && inst->decode_count_bit == PSA_KEY2_BITS) {
            if(psa_duration_near(duration, PSA_TE_END_1000, 199)) {
                goto psa_s2_packet_complete;
            }
        }

        uint8_t mc_event;
        bool process = false;
        if(psa_duration_near(duration, te_s, tol)) {
            mc_event = ((level ^ 1) & 0x7F) << 1;
            process  = true;
        } else if(psa_duration_near(duration, te_l, tol)) {
            mc_event = level ? 4 : 6;
            process  = true;
        } else {
            if(!level && psa_duration_near(duration, PSA_TE_END_1000, 199)) {
                if(inst->decode_count_bit == PSA_KEY2_BITS &&
                   (inst->validation_field & 0xF) == PSA_VALID_NIBBLE) {
                    goto psa_s2_packet_complete_noend;
                }
            }
            return;
        }

        if(process && inst->decode_count_bit < PSA_KEY2_BITS) {
            bool bit = false;
            if(manchester_advance(inst->manchester_state, (ManchesterEvent)mc_event,
                                  &inst->manchester_state, &bit)) {
                uint32_t carry = (inst->decode_data_low >> 31) & 1;
                inst->decode_data_low  = (inst->decode_data_low << 1) | (bit ? 1 : 0);
                inst->decode_data_high = (inst->decode_data_high << 1) | carry;
                inst->decode_count_bit++;
                if(inst->decode_count_bit == PSA_KEY1_BITS) {
                    inst->key1_low         = inst->decode_data_low;
                    inst->key1_high        = inst->decode_data_high;
                    inst->decode_data_low  = 0;
                    inst->decode_data_high = 0;
                }
            }
        }
        if(!level) return;
        break;

psa_s2_packet_complete:
psa_s2_packet_complete_noend:
        inst->validation_field = (uint16_t)(inst->decode_data_low & 0xFFFF);
        inst->key2_low         = inst->decode_data_low;
        inst->key2_high        = inst->decode_data_high;
        inst->mode_serialize   = 1;
        inst->status_flag      = 0x80;

        psa_decrypt_fast(inst);

        if(inst->decrypted != 0x50 && (inst->validation_field & 0xF) != PSA_VALID_NIBBLE) {
            inst->decode_data_low  = 0;
            inst->decode_data_high = 0;
            inst->decode_count_bit = 0;
            inst->state            = PSADecoderState0;
            return;
        }

        inst->generic.data           = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
        inst->generic.data_count_bit = 64;
        inst->decoder.decode_data    = inst->generic.data;
        inst->decoder.decode_count_bit = 64;

        PSA_FIRE_CALLBACK_IF_NEW(inst);

        inst->decode_data_low  = 0;
        inst->decode_data_high = 0;
        inst->decode_count_bit = 0;
        inst->state            = PSADecoderState0;
        return;
    }

    case PSADecoderState3:
        if(level) return;
        if(psa_duration_near(duration, te_hs, tol_h)) {
            if(psa_duration_near(inst->prev_duration, te_hs, tol_h))
                inst->pattern_counter++;
            else
                inst->pattern_counter = 0;
            inst->prev_duration = duration;
            return;
        }
        if(duration >= te_hl && duration < 0x12C) {
            if(inst->pattern_counter > 0x45) {
                inst->decode_data_low  = 0;
                inst->decode_data_high = 0;
                inst->decode_count_bit = 0;
                manchester_advance(inst->manchester_state, ManchesterEventReset,
                                   &inst->manchester_state, NULL);
                inst->state = PSADecoderState4;
            }
            inst->pattern_counter = 0;
            inst->prev_duration   = duration;
            return;
        }
        inst->state = PSADecoderState0;
        break;

    case PSADecoderState4: {
        if(inst->decode_count_bit >= PSA_MAX_BITS) {
            inst->state = PSADecoderState0;
            break;
        }

        if(!level) {
            uint8_t mc_event;
            if(psa_duration_near(duration, te_hs, tol_h)) {
                mc_event = ((level ^ 1) & 0x7F) << 1;
            } else if(duration >= te_hl && duration < 0x12C) {
                mc_event = level ? 4 : 6;
            } else {
                return;
            }

            bool bit = false;
            if(manchester_advance(inst->manchester_state, (ManchesterEvent)mc_event,
                                  &inst->manchester_state, &bit)) {
                uint32_t carry = (inst->decode_data_low >> 31) & 1;
                inst->decode_data_low  = (inst->decode_data_low << 1) | (bit ? 1 : 0);
                inst->decode_data_high = (inst->decode_data_high << 1) | carry;
                inst->decode_count_bit++;
                if(inst->decode_count_bit == PSA_KEY1_BITS) {
                    inst->key1_low         = inst->decode_data_low;
                    inst->key1_high        = inst->decode_data_high;
                    inst->decode_data_low  = 0;
                    inst->decode_data_high = 0;
                }
            }
        } else {

            if(psa_duration_near(duration, PSA_TE_END_500, 99)) {
                if(inst->decode_count_bit != PSA_KEY2_BITS) return;

                inst->validation_field = (uint16_t)(inst->decode_data_low & 0xFFFF);
                inst->key2_low         = inst->decode_data_low;
                inst->key2_high        = inst->decode_data_high;
                inst->mode_serialize   = 2;
                inst->status_flag      = 0x80;

                psa_decrypt_fast(inst);

                if(inst->decrypted != 0x50 && (inst->validation_field & 0xF) != PSA_VALID_NIBBLE) {
                    inst->decode_data_low  = 0;
                    inst->decode_data_high = 0;
                    inst->decode_count_bit = 0;
                    inst->state            = PSADecoderState0;
                    return;
                }

                inst->generic.data             = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
                inst->generic.data_count_bit   = 64;
                inst->decoder.decode_data      = inst->generic.data;
                inst->decoder.decode_count_bit = 64;

                PSA_FIRE_CALLBACK_IF_NEW(inst);

                inst->decode_data_low  = 0;
                inst->decode_data_high = 0;
                inst->decode_count_bit = 0;
                inst->state            = PSADecoderState0;
                return;
            }
        }
        break;
    }
    }

    inst->prev_duration = duration;
}

uint8_t subghz_protocol_decoder_psa2_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;
    return subghz_protocol_blocks_get_hash_data(
        &inst->decoder, (inst->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_psa2_serialize(
    void* context, FlipperFormat* ff, SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;

    if(inst->decrypted != 0x50 && inst->status_flag == 0x80) {
        psa_decrypt_fast(inst);
        if(inst->decrypted == 0x50) {
            inst->generic.cnt    = inst->decrypted_counter;
            inst->generic.serial = inst->decrypted_serial;
            inst->generic.btn    = inst->decrypted_button;
        }
    }

    inst->generic.data_count_bit = 128;
    SubGhzProtocolStatus ret = subghz_block_generic_serialize(&inst->generic, ff, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    do {
        char key2_str[32];
        snprintf(key2_str, sizeof(key2_str),
                 "%02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned int)((inst->key2_high >> 24) & 0xFF), (unsigned int)((inst->key2_high >> 16) & 0xFF),
                 (unsigned int)((inst->key2_high >>  8) & 0xFF), (unsigned int)(inst->key2_high        & 0xFF),
                 (unsigned int)((inst->key2_low  >> 24) & 0xFF), (unsigned int)((inst->key2_low  >> 16) & 0xFF),
                 (unsigned int)((inst->key2_low  >>  8) & 0xFF), (unsigned int)(inst->key2_low         & 0xFF));
        if(!flipper_format_write_string_cstr(ff, "Key_2", key2_str)) { ret = SubGhzProtocolStatusError; break; }

        if(inst->decrypted == 0x50 && inst->decrypted_type != 0) {
            char s[32];
            snprintf(s, sizeof(s), "%02X %02X %02X",
                     (unsigned int)((inst->decrypted_serial >> 16) & 0xFF),
                     (unsigned int)((inst->decrypted_serial >>  8) & 0xFF),
                     (unsigned int)(inst->decrypted_serial        & 0xFF));
            if(!flipper_format_write_string_cstr(ff, "Serial", s)) { ret = SubGhzProtocolStatusError; break; }

            if(inst->decrypted_type == PSA_MODE_23)
                snprintf(s, sizeof(s), "%02X %02X",
                         (unsigned int)((inst->decrypted_counter >> 8) & 0xFF),
                         (unsigned int)(inst->decrypted_counter       & 0xFF));
            else
                snprintf(s, sizeof(s), "%02X %02X %02X %02X",
                         (unsigned int)((inst->decrypted_counter >> 24) & 0x0F),
                         (unsigned int)((inst->decrypted_counter >> 16) & 0xFF),
                         (unsigned int)((inst->decrypted_counter >>  8) & 0xFF),
                         (unsigned int)(inst->decrypted_counter        & 0xFF));
            if(!flipper_format_write_string_cstr(ff, "Cnt", s)) { ret = SubGhzProtocolStatusError; break; }

            snprintf(s, sizeof(s), "%02X", inst->decrypted_button);
            if(!flipper_format_write_string_cstr(ff, "Btn", s)) { ret = SubGhzProtocolStatusError; break; }

            snprintf(s, sizeof(s), "%02X", inst->decrypted_type);
            if(!flipper_format_write_string_cstr(ff, "Type", s)) { ret = SubGhzProtocolStatusError; break; }

            if(inst->decrypted_type == PSA_MODE_23)
                snprintf(s, sizeof(s), "%02X", inst->decrypted_crc & 0xFF);
            else
                snprintf(s, sizeof(s), "%02X %02X",
                         (inst->decrypted_crc >> 8) & 0xFF,
                          inst->decrypted_crc       & 0xFF);
            if(!flipper_format_write_string_cstr(ff, "CRC", s)) { ret = SubGhzProtocolStatusError; break; }

            snprintf(s, sizeof(s), "%02X %02X %02X",
                     (unsigned int)((inst->decrypted_seed >> 16) & 0xFF),
                     (unsigned int)((inst->decrypted_seed >>  8) & 0xFF),
                     (unsigned int)(inst->decrypted_seed        & 0xFF));
            if(!flipper_format_write_string_cstr(ff, "Seed", s)) { ret = SubGhzProtocolStatusError; break; }
        }
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

static uint32_t psa_parse_hex_str(const char* s) {
    uint32_t v = 0;
    for(size_t i = 0; i < strlen(s); i++) {
        char c = s[i];
        if(c == ' ') continue;
        uint8_t n = (c>='0'&&c<='9') ? c-'0' :
                    (c>='A'&&c<='F') ? c-'A'+10 :
                    (c>='a'&&c<='f') ? c-'a'+10 : 0;
        v = (v << 4) | n;
    }
    return v;
}

SubGhzProtocolStatus subghz_protocol_decoder_psa2_deserialize(
    void* context, FlipperFormat* ff) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    FuriString* tmp = furi_string_alloc();

    do {
        ret = subghz_block_generic_deserialize(&inst->generic, ff);
        if(ret != SubGhzProtocolStatusOk) break;

        uint64_t k1 = inst->generic.data;
        inst->key1_low  = (uint32_t)(k1 & 0xFFFFFFFF);
        inst->key1_high = (uint32_t)(k1 >> 32);

        if(!flipper_format_read_string(ff, "Key_2", tmp)) break;
        uint64_t k2 = 0;
        const char* ks = furi_string_get_cstr(tmp);
        for(size_t i = 0; i < strlen(ks); i++) {
            char c = ks[i]; if(c==' ') continue;
            uint8_t n=(c>='0'&&c<='9')?c-'0':(c>='A'&&c<='F')?c-'A'+10:(c>='a'&&c<='f')?c-'a'+10:0;
            k2 = (k2 << 4) | n;
        }
        inst->key2_low  = (uint32_t)(k2 & 0xFFFFFFFF);
        inst->key2_high = (uint32_t)(k2 >> 32);

        inst->generic.data           = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
        inst->generic.data_count_bit = 128;
        inst->status_flag            = 0x80;

        bool has_data = true;
        uint32_t serial=0, counter=0, seed=0;
        uint16_t crc=0;
        uint8_t  button=0, type=0;

        if(flipper_format_read_string(ff, "Serial", tmp))
            serial = psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "Cnt", tmp))
            counter = psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "Btn", tmp))
            button = (uint8_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "Type", tmp))
            type = (uint8_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "CRC", tmp))
            crc = (uint16_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "Seed", tmp))
            seed = psa_parse_hex_str(furi_string_get_cstr(tmp));

        if(has_data && type != 0) {
            inst->decrypted_serial  = serial;
            inst->decrypted_counter = counter;
            inst->decrypted_button  = button;
            inst->decrypted_type    = type;
            inst->decrypted_crc     = crc;
            inst->decrypted_seed    = seed;
            inst->decrypted         = 0x50;
            inst->mode_serialize    = type;
            inst->generic.cnt       = counter;
            inst->generic.serial    = serial;
            inst->generic.btn       = button;
        } else {
            psa_decrypt_fast(inst);
            inst->generic.cnt    = inst->decrypted_counter;
            inst->generic.serial = inst->decrypted_serial;
            inst->generic.btn    = inst->decrypted_button;
        }
        ret = SubGhzProtocolStatusOk;
    } while(false);

    furi_string_free(tmp);
    return ret;
}

void subghz_protocol_decoder_psa2_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderPSA* inst = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &inst->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(inst->generic.protocol_name, &input, output);
}


