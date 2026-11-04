/* enc_psa2.fal : SD encoder for psa2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_psa2_alloc enc_psa2__subghz_protocol_encoder_psa2_alloc
#define subghz_protocol_encoder_psa2_deserialize enc_psa2__subghz_protocol_encoder_psa2_deserialize
#define subghz_protocol_encoder_psa2_free enc_psa2__subghz_protocol_encoder_psa2_free
#define subghz_protocol_encoder_psa2_stop enc_psa2__subghz_protocol_encoder_psa2_stop
#define subghz_protocol_encoder_psa2_yield enc_psa2__subghz_protocol_encoder_psa2_yield
#define subghz_protocol_psa2 enc_psa2__subghz_protocol_psa2
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/psa2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

#define PSA_TE_SHORT_STD   250
#define PSA_TE_SHORT_HALF  125
#define PSA_TE_LONG_STD    500
#define PSA_TE_LONG_HALF   250
#define PSA_TE_END_1000    1000
#define PSA_TE_END_500     500
#define TEA_DELTA  0x9E3779B9U
#define TEA_ROUNDS 32
#define PSA_BF1_START  0x23000000U
#define PSA_BF1_CONST_U4  0x0E0F5C41U
#define PSA_BF1_CONST_U5  0x0F5C4123U
#define PSA_BTN_LOCK    0x0
#define PSA_BTN_UNLOCK  0x1
#define PSA_BTN_TRUNK   0x2
#define PSA_MODE_23       0x23

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

static const uint32_t PSA_BF1_KEY_SCHEDULE[4] = {
    0x4A434915U,
    0xD6743C2BU,
    0x1F29D308U,
    0xE6B79A64U,
};

static uint8_t psa_get_btn_code(void);
static void psa_tea_encrypt(uint32_t* v0, uint32_t* v1, const uint32_t* key);
static uint8_t psa_calculate_tea_crc(uint32_t v0, uint32_t v1);
static void psa_setup_byte_buffer(uint8_t* buf,
                                   uint32_t key1_low, uint32_t key1_high,
                                   uint32_t key2_low);
static void psa_calculate_checksum(uint8_t* buf);
static void psa_second_stage_xor_encrypt(uint8_t* buf);
static void psa_unpack_tea_result(uint8_t* buf, uint32_t v0, uint32_t v1);
static uint32_t psa_parse_hex_str(const char* s);
static void psa_encoder_build_upload(SubGhzProtocolEncoderPSA* inst);

static uint8_t psa_get_btn_code(void) {
    uint8_t custom_btn   = subghz_custom_btn_get();
    uint8_t original_raw = subghz_custom_btn_get_original();
    uint8_t original_btn = (original_raw == 0xFF) ? PSA_BTN_LOCK : original_raw;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_OK)    return original_btn;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_UP)    return PSA_BTN_LOCK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_DOWN)  return PSA_BTN_UNLOCK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_LEFT)  return PSA_BTN_TRUNK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_RIGHT) return PSA_BTN_TRUNK;
    return original_btn;
}

static void psa_tea_encrypt(uint32_t* v0, uint32_t* v1, const uint32_t* key) {
    uint32_t a = *v0, b = *v1, sum = 0;
    for(int i = 0; i < TEA_ROUNDS; i++) {
        uint32_t t = key[sum & 3] + sum;
        sum += TEA_DELTA;
        a += t ^ ((b >> 5 ^ b << 4) + b);
        t  = key[(sum >> 11) & 3] + sum;
        b += t ^ ((a >> 5 ^ a << 4) + a);
    }
    *v0 = a; *v1 = b;
}

static uint8_t psa_calculate_tea_crc(uint32_t v0, uint32_t v1) {
    uint32_t crc = ((v0 >> 24) & 0xFF) + ((v0 >> 16) & 0xFF) +
                   ((v0 >>  8) & 0xFF) + ( v0        & 0xFF);
    crc += ((v1 >> 24) & 0xFF) + ((v1 >> 16) & 0xFF) + ((v1 >> 8) & 0xFF);
    return (uint8_t)(crc & 0xFF);
}

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

static void psa_second_stage_xor_encrypt(uint8_t* buf) {
    uint8_t E6 = buf[8], E7 = buf[9];
    uint8_t P0=buf[2], P1=buf[3], P2=buf[4], P3=buf[5], P4=buf[6], P5=buf[7];
    uint8_t E5 = P5 ^ E7 ^ E6;
    uint8_t E0 = P2 ^ E5;
    uint8_t E2 = P4 ^ E0;
    uint8_t E4 = P3 ^ E2;
    uint8_t E3 = P0 ^ E5;
    uint8_t E1 = P1 ^ E3;
    buf[2]=E0; buf[3]=E1; buf[4]=E2; buf[5]=E3; buf[6]=E4; buf[7]=E5;
}

static void psa_unpack_tea_result(uint8_t* buf, uint32_t v0, uint32_t v1) {
    buf[2] = (v0 >> 24) & 0xFF;
    buf[3] = (v0 >> 16) & 0xFF;
    buf[4] = (v0 >>  8) & 0xFF;
    buf[5] =  v0        & 0xFF;
    buf[6] = (v1 >> 24) & 0xFF;
    buf[7] = (v1 >> 16) & 0xFF;
    buf[8] = (v1 >>  8) & 0xFF;
    buf[9] =  v1        & 0xFF;
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

static void psa_encoder_build_upload(SubGhzProtocolEncoderPSA* inst) {
    uint32_t te, te_long_sync, end_dur;
    if(inst->mode == PSA_MODE_23) {
        te           = PSA_TE_SHORT_STD;
        te_long_sync = PSA_TE_LONG_STD;
        end_dur      = PSA_TE_END_1000;
    } else {
        te           = PSA_TE_SHORT_HALF;
        te_long_sync = PSA_TE_LONG_HALF;
        end_dur      = PSA_TE_END_500;
    }

    uint8_t buf[48] = {0};
    uint8_t preserve[2] = {0};
    uint8_t* pptr = NULL;
    if(inst->key1_low || inst->key1_high) {
        uint8_t orig[48] = {0};
        psa_setup_byte_buffer(orig, inst->key1_low, inst->key1_high, inst->key2_low);
        preserve[0] = orig[0]; preserve[1] = orig[1];
        pptr = preserve;
    }

    if(inst->mode == PSA_MODE_23) {

        memset(buf, 0, 48);
        buf[2] = (inst->serial >> 16) & 0xFF;
        buf[3] = (inst->serial >>  8) & 0xFF;
        buf[4] =  inst->serial        & 0xFF;
        buf[5] = (inst->counter >> 8) & 0xFF;
        buf[6] =  inst->counter       & 0xFF;
        buf[7] =  inst->crc           & 0xFF;
        buf[8] =  inst->button        & 0xF;
        buf[9] =  inst->key2_low      & 0xFF;
        psa_second_stage_xor_encrypt(buf);
        psa_calculate_checksum(buf);
        buf[8] = (buf[8] & 0x0F) | (buf[11] & 0xF0);
        buf[13] = buf[9] ^ buf[8];
        buf[0] = pptr ? pptr[0] : buf[2] ^ buf[6];
        buf[1] = pptr ? pptr[1] : buf[3] ^ buf[7];
    } else {

        memset(buf, 0, 48);
        uint32_t v0 = ((inst->serial & 0xFFFFFF) << 8) |
                      ((inst->button & 0xF) << 4) |
                      ((inst->counter >> 24) & 0x0F);
        uint32_t v1 = ((inst->counter & 0xFFFFFF) << 8) | (inst->crc & 0xFF);
        uint8_t  crc8 = psa_calculate_tea_crc(v0, v1);
        v1 = (v1 & 0xFFFFFF00) | crc8;
        uint32_t bf_counter = PSA_BF1_START | (inst->serial & 0xFFFFFF);
        uint32_t wk2 = PSA_BF1_CONST_U4, wk3 = bf_counter;
        psa_tea_encrypt(&wk2, &wk3, PSA_BF1_KEY_SCHEDULE);
        uint32_t wk0 = (bf_counter << 8) | 0x0E, wk1 = PSA_BF1_CONST_U5;
        psa_tea_encrypt(&wk0, &wk1, PSA_BF1_KEY_SCHEDULE);
        uint32_t wkey[4] = {wk0, wk1, wk2, wk3};
        psa_tea_encrypt(&v0, &v1, wkey);
        psa_unpack_tea_result(buf, v0, v1);
        buf[0] = pptr ? pptr[0] : buf[2] ^ buf[6];
        buf[1] = pptr ? pptr[1] : buf[3] ^ buf[7];
    }

    uint32_t k1h = ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|((uint32_t)buf[2]<<8)|buf[3];
    uint32_t k1l = ((uint32_t)buf[4]<<24)|((uint32_t)buf[5]<<16)|((uint32_t)buf[6]<<8)|buf[7];
    uint16_t vf  = ((uint16_t)buf[8]<<8) | buf[9];

    size_t idx = 0;

    for(int i = 0; i < 80 && idx < inst->encoder.size_upload - 2; i++) {
        inst->encoder.upload[idx++] = level_duration_make(true,  te);
        inst->encoder.upload[idx++] = level_duration_make(false, te);
    }

    if(idx < inst->encoder.size_upload - 3) {
        inst->encoder.upload[idx++] = level_duration_make(false, te);
        inst->encoder.upload[idx++] = level_duration_make(true,  te_long_sync);
        inst->encoder.upload[idx++] = level_duration_make(false, te);
    }

    uint64_t k1data = ((uint64_t)k1h << 32) | k1l;
    for(int bit = 63; bit >= 0 && idx < inst->encoder.size_upload - 2; bit--) {
        bool b = (k1data >> bit) & 1;
        inst->encoder.upload[idx++] = level_duration_make(b,  te);
        inst->encoder.upload[idx++] = level_duration_make(!b, te);
    }

    for(int bit = 15; bit >= 0 && idx < inst->encoder.size_upload - 2; bit--) {
        bool b = (vf >> bit) & 1;
        inst->encoder.upload[idx++] = level_duration_make(b,  te);
        inst->encoder.upload[idx++] = level_duration_make(!b, te);
    }

    if(idx < inst->encoder.size_upload - 1) {
        inst->encoder.upload[idx++] = level_duration_make(true,  end_dur);
        inst->encoder.upload[idx++] = level_duration_make(false, end_dur);
    }

    inst->encoder.size_upload = idx;
    inst->encoder.front       = 0;
    inst->encoder.repeat      = 10;
    inst->key1_high           = k1h;
    inst->key1_low            = k1l;
    inst->key2_low            = vf;
}

void* subghz_protocol_encoder_psa2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderPSA* inst = malloc(sizeof(SubGhzProtocolEncoderPSA));
    if(inst) {
        memset(inst, 0, sizeof(SubGhzProtocolEncoderPSA));
        inst->base.protocol      = &subghz_protocol_psa2;
        inst->generic.protocol_name = inst->base.protocol->name;
        inst->encoder.size_upload   = 600;
        inst->encoder.upload        = malloc(600 * sizeof(LevelDuration));
        inst->encoder.repeat        = 10;
    }
    return inst;
}

void subghz_protocol_encoder_psa2_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* inst = context;
    if(inst->encoder.upload) free(inst->encoder.upload);
    free(inst);
}

SubGhzProtocolStatus subghz_protocol_encoder_psa2_deserialize(
    void* context, FlipperFormat* ff) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* inst = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    FuriString* tmp = furi_string_alloc();

    do {
        flipper_format_rewind(ff);
        ret = subghz_block_generic_deserialize(&inst->generic, ff);
        if(ret != SubGhzProtocolStatusOk) break;

        uint64_t k1 = inst->generic.data;
        inst->key1_low  = (uint32_t)(k1 & 0xFFFFFFFF);
        inst->key1_high = (uint32_t)(k1 >> 32);

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "Key_2", tmp)) break;
        uint64_t k2 = 0;
        const char* ks = furi_string_get_cstr(tmp);
        for(size_t i = 0; i < strlen(ks); i++) {
            char c = ks[i]; if(c==' ') continue;
            uint8_t n=(c>='0'&&c<='9')?c-'0':(c>='A'&&c<='F')?c-'A'+10:(c>='a'&&c<='f')?c-'a'+10:0;
            k2 = (k2 << 4) | n;
        }
        inst->key2_low        = (uint32_t)(k2 & 0xFFFFFFFF);
        inst->validation_field = (uint16_t)(k2 & 0xFFFF);

        bool has_data = true;
        uint32_t serial=0, counter=0, seed=0;
        uint16_t crc=0;
        uint8_t  button=0, type=0;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Serial", tmp))
            serial = psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Cnt", tmp))
            counter = psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Btn", tmp))
            button = (uint8_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Type", tmp))
            type = (uint8_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "CRC", tmp))
            crc = (uint16_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Seed", tmp))
            seed = psa_parse_hex_str(furi_string_get_cstr(tmp));
        UNUSED(seed);

        if(!has_data || type == 0) { ret = SubGhzProtocolStatusErrorParserOthers; break; }

        inst->serial  = serial;
        inst->counter = counter;
        inst->button  = button;
        inst->type    = type;
        inst->crc     = crc;
        inst->mode    = type;

        subghz_custom_btn_set_original(button == 0 ? 0xFF : button);
        subghz_custom_btn_set_max(4);
        inst->button = psa_get_btn_code();

        uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
        inst->counter = (inst->counter + mult) & 0xFFFFFFFF;

        psa_encoder_build_upload(inst);

        inst->generic.data   = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
        inst->generic.cnt    = inst->counter;
        inst->generic.serial = inst->serial;
        inst->generic.btn    = inst->button;

        flipper_format_rewind(ff);
        char s[32];
        if(inst->type == PSA_MODE_23)
            snprintf(s,sizeof(s),"%02X %02X",
                     (unsigned int)((inst->counter>>8)&0xFF), (unsigned int)(inst->counter&0xFF));
        else
            snprintf(s,sizeof(s),"%02X %02X %02X %02X",
                     (unsigned int)((inst->counter>>24)&0x0F), (unsigned int)((inst->counter>>16)&0xFF),
                     (unsigned int)((inst->counter>>8)&0xFF),  (unsigned int)(inst->counter&0xFF));
        flipper_format_update_string_cstr(ff, "Cnt", s);

        uint64_t k1out = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
        snprintf(s,sizeof(s),"%02X %02X %02X %02X %02X %02X %02X %02X",
                 (uint8_t)(k1out>>56),(uint8_t)(k1out>>48),(uint8_t)(k1out>>40),(uint8_t)(k1out>>32),
                 (uint8_t)(k1out>>24),(uint8_t)(k1out>>16),(uint8_t)(k1out>>8),(uint8_t)k1out);
        flipper_format_update_string_cstr(ff, "Key", s);

        snprintf(s,sizeof(s),"00 00 00 00 00 00 %02X %02X",
                 (unsigned int)((inst->key2_low>>8)&0xFF), (unsigned int)(inst->key2_low&0xFF));
        flipper_format_update_string_cstr(ff, "Key_2", s);

        inst->is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    furi_string_free(tmp);
    return ret;
}

void subghz_protocol_encoder_psa2_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* inst = context;
    inst->is_running         = false;
    inst->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_psa2_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* inst = context;
    if(!inst->is_running || inst->encoder.size_upload == 0) {
        inst->is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = inst->encoder.upload[inst->encoder.front++];
    if(inst->encoder.front >= inst->encoder.size_upload) {
        inst->encoder.front = 0;
        if(--inst->encoder.repeat <= 0) inst->is_running = false;
    }
    return ret;
}

static const SubGhzProtocolEncoder enc_psa2_vt = {
    .alloc = subghz_protocol_encoder_psa2_alloc,
    .free = subghz_protocol_encoder_psa2_free,
    .deserialize = subghz_protocol_encoder_psa2_deserialize,
    .stop = subghz_protocol_encoder_psa2_stop,
    .yield = subghz_protocol_encoder_psa2_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_psa2 = {
    .name = SUBGHZ_PROTOCOL_PSA2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM |
               SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_psa2_vt,
};

static const SubGhzEncodePluginEntry enc_psa2_entry = {.name = SUBGHZ_PROTOCOL_PSA2_NAME, .encoder = &enc_psa2_vt};
static const FlipperAppPluginDescriptor enc_psa2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_psa2_entry};
const FlipperAppPluginDescriptor* enc_psa2_ep(void) { return &enc_psa2_descriptor; }
