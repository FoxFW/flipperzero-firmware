/* enc_porsche_cayenne.fal : SD encoder for porsche_cayenne (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_porsche_cayenne_alloc enc_porsche_cayenne__subghz_protocol_encoder_porsche_cayenne_alloc
#define subghz_protocol_encoder_porsche_cayenne_deserialize enc_porsche_cayenne__subghz_protocol_encoder_porsche_cayenne_deserialize
#define subghz_protocol_encoder_porsche_cayenne_free enc_porsche_cayenne__subghz_protocol_encoder_porsche_cayenne_free
#define subghz_protocol_encoder_porsche_cayenne_stop enc_porsche_cayenne__subghz_protocol_encoder_porsche_cayenne_stop
#define subghz_protocol_encoder_porsche_cayenne_yield enc_porsche_cayenne__subghz_protocol_encoder_porsche_cayenne_yield
#define subghz_protocol_porsche_cayenne enc_porsche_cayenne__subghz_protocol_porsche_cayenne
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/porsche_cayenne.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "PorscheCayenneProtocol"
#define PC_TE_GAP    5930u
#define PC_SYNC_COUNT  73
#define PC_UPLOAD_SIZE 1300
#define ROTATE24(rh, rm, rl) do { \
        uint8_t _ch = (uint8_t)(((rh) >> 7) & 1u); \
        uint8_t _cm = (uint8_t)(((rm) >> 7) & 1u); \
        uint8_t _cl = (uint8_t)(((rl) >> 7) & 1u); \
        (rh) = (uint8_t)(((rh) << 1) | _cm); \
        (rm) = (uint8_t)(((rm) << 1) | _cl); \
        (rl) = (uint8_t)(((rl) << 1) | _ch); \
    } while(0)

typedef struct {
    SubGhzProtocolEncoderBase  base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric         generic;
} SubGhzProtocolEncoderPorscheCayenne;

static const SubGhzBlockConst subghz_protocol_porsche_cayenne_const = {
    .te_short = 1680,
    .te_long  = 3370,
    .te_delta =  500,
    .min_count_bit_for_found = 64,
};

static void porsche_cayenne_compute_frame(
    uint32_t serial24,
    uint8_t btn,
    uint16_t counter,
    uint8_t frame_type,
    uint8_t* pkt);
static uint8_t porsche_cayenne_custom_to_btn(uint8_t custom_btn, uint8_t fallback_btn);
static uint8_t porsche_cayenne_btn_to_custom(uint8_t btn);
static uint8_t porsche_cayenne_get_btn_code(void);
static void porsche_cayenne_build_upload(SubGhzProtocolEncoderPorscheCayenne* instance);

static void porsche_cayenne_compute_frame(
    uint32_t serial24,
    uint8_t btn,
    uint16_t counter,
    uint8_t frame_type,
    uint8_t* pkt) {

    uint8_t b0 = (uint8_t)((btn << 4) | (frame_type & 0x07));
    uint8_t b1 = (serial24 >> 16) & 0xFF;
    uint8_t b2 = (serial24 >> 8) & 0xFF;
    uint8_t b3 = serial24 & 0xFF;

    uint16_t cnt = counter + 1;
    uint8_t cnt_lo = cnt & 0xFF;
    uint8_t cnt_hi = (cnt >> 8) & 0xFF;

    uint8_t r_h = b3;
    uint8_t r_m = b1;
    uint8_t r_l = b2;

#define ROTATE24(rh, rm, rl) do { \
        uint8_t _ch = (uint8_t)(((rh) >> 7) & 1u); \
        uint8_t _cm = (uint8_t)(((rm) >> 7) & 1u); \
        uint8_t _cl = (uint8_t)(((rl) >> 7) & 1u); \
        (rh) = (uint8_t)(((rh) << 1) | _cm); \
        (rm) = (uint8_t)(((rm) << 1) | _cl); \
        (rl) = (uint8_t)(((rl) << 1) | _ch); \
    } while(0)

    for(int i = 0; i < 4; i++) ROTATE24(r_h, r_m, r_l);

    for(int j = 0; j < cnt_lo; j++) ROTATE24(r_h, r_m, r_l);

#undef ROTATE24

    uint8_t a9A = r_h ^ b0;

    uint8_t nb9B_p1 = (uint8_t)((~cnt_lo << 2) & 0xFC) ^ r_m; nb9B_p1 &= 0xCC;
    uint8_t nb9B_p2 = (uint8_t)((~cnt_hi << 2) & 0xFC) ^ r_m; nb9B_p2 &= 0x30;
    uint8_t nb9B_p3 = (uint8_t)((~cnt_hi >> 6) & 0x03) ^ r_m; nb9B_p3 &= 0x03;
    uint8_t a9B = nb9B_p1 | nb9B_p2 | nb9B_p3;

    uint8_t nb9C_p1 = (uint8_t)((~cnt_lo >> 2) & 0x3F) ^ r_l; nb9C_p1 &= 0x33;
    uint8_t nb9C_p2 = (uint8_t)((~cnt_hi & 0x03) << 6) ^ r_l; nb9C_p2 &= 0xC0;
    uint8_t nb9C_p3 = (uint8_t)((~cnt_hi >> 2) & 0x3F) ^ r_l; nb9C_p3 &= 0x0C;
    uint8_t a9C = nb9C_p1 | nb9C_p2 | nb9C_p3;

    pkt[0] = b0;
    pkt[1] = b1;
    pkt[2] = b2;
    pkt[3] = b3;

    pkt[4] = (uint8_t)(((a9A >> 2) & 0x3F) | ((~cnt_lo & 0x03u) << 6));

    pkt[5] = (uint8_t)(
        (~cnt_lo & 0xC0u) |
        ((a9A & 0x03u) << 4) |
        (a9B & 0x0Cu) |
        ((~cnt_lo >> 2) & 0x03u));

    pkt[6] = (uint8_t)(
        ((a9B & 0x03u) << 6) |
        ((a9C >> 2) & 0x3Cu) |
        ((~cnt_lo >> 4) & 0x03u));

    pkt[7] = (uint8_t)(((a9B >> 4) & 0x0Fu) | ((a9C & 0x0Fu) << 4));
}

static uint8_t porsche_cayenne_custom_to_btn(uint8_t custom_btn, uint8_t fallback_btn) {
    switch(custom_btn) {
    case SUBGHZ_CUSTOM_BTN_UP: return 0x01;
    case SUBGHZ_CUSTOM_BTN_DOWN: return 0x02;
    case SUBGHZ_CUSTOM_BTN_LEFT: return 0x04;
    case SUBGHZ_CUSTOM_BTN_RIGHT: return 0x08;
    default: return fallback_btn;
    }
}

static uint8_t porsche_cayenne_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case 0x01: return SUBGHZ_CUSTOM_BTN_UP;
    case 0x02: return SUBGHZ_CUSTOM_BTN_DOWN;
    case 0x04: return SUBGHZ_CUSTOM_BTN_LEFT;
    case 0x08: return SUBGHZ_CUSTOM_BTN_RIGHT;
    default: return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static uint8_t porsche_cayenne_get_btn_code(void) {
    uint8_t override_btn = 0;
    if(subghz_block_generic_global_button_override_get(&override_btn)) {
        return override_btn;
    }

    uint8_t custom_btn  = subghz_custom_btn_get();
    uint8_t original_btn = porsche_cayenne_custom_to_btn(
        subghz_custom_btn_get_original(), 0x01);
    if(custom_btn == SUBGHZ_CUSTOM_BTN_OK) return original_btn;
    return porsche_cayenne_custom_to_btn(custom_btn, original_btn);
}

static void porsche_cayenne_build_upload(SubGhzProtocolEncoderPorscheCayenne* instance) {

    static const uint8_t frame_types[4] = {0b010, 0b001, 0b100, 0b100};

    const uint32_t te_s = subghz_protocol_porsche_cayenne_const.te_short;
    const uint32_t te_l = subghz_protocol_porsche_cayenne_const.te_long;

    uint32_t serial = instance->generic.serial & 0xFFFFFF;
    uint8_t  btn    = porsche_cayenne_get_btn_code();
    uint16_t cnt    = (uint16_t)instance->generic.cnt;
    instance->generic.btn = btn;

    size_t idx = 0;
    LevelDuration* up = instance->encoder.upload;

    for(int f = 0; f < 4; f++) {
        uint8_t pkt[8];

        porsche_cayenne_compute_frame(serial, btn, (uint16_t)(cnt + (uint16_t)f), frame_types[f], pkt);

        for(int s = 0; s < PC_SYNC_COUNT; s++) {
            up[idx++] = level_duration_make(false, te_l);
            up[idx++] = level_duration_make(true,  te_l);
        }

        up[idx++] = level_duration_make(false, PC_TE_GAP);
        up[idx++] = level_duration_make(true,  PC_TE_GAP);

        for(int byte = 0; byte < 8; byte++) {
            for(int bit = 7; bit >= 0; bit--) {
                bool b = (pkt[byte] >> bit) & 1;
                if(b) {

                    up[idx++] = level_duration_make(false, te_l);
                    up[idx++] = level_duration_make(true,  te_s);
                } else {

                    up[idx++] = level_duration_make(false, te_s);
                    up[idx++] = level_duration_make(true,  te_l);
                }
            }
        }
    }

    instance->encoder.size_upload = idx;
    instance->encoder.front       = 0;

    if(furi_hal_subghz_get_rolling_counter_mult() != 0) {
        instance->generic.cnt = (uint16_t)(cnt + 4);
    }
}

void* subghz_protocol_encoder_porsche_cayenne_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderPorscheCayenne* instance =
        malloc(sizeof(SubGhzProtocolEncoderPorscheCayenne));

    instance->base.protocol          = &subghz_protocol_porsche_cayenne;
    instance->generic.protocol_name  = instance->base.protocol->name;
    instance->encoder.repeat         = 1;
    instance->encoder.size_upload    = PC_UPLOAD_SIZE;
    instance->encoder.upload         = malloc(PC_UPLOAD_SIZE * sizeof(LevelDuration));
    instance->encoder.is_running     = false;
    instance->encoder.front          = 0;

    return instance;
}

void subghz_protocol_encoder_porsche_cayenne_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderPorscheCayenne* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus subghz_protocol_encoder_porsche_cayenne_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderPorscheCayenne* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front      = 0;
    instance->encoder.repeat     = 1;

    do {
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(
               flipper_format, "Serial", &instance->generic.serial, 1)) {
            FURI_LOG_E(TAG, "Missing Serial");
            break;
        }

        flipper_format_rewind(flipper_format);
        uint32_t tmp = 0;
        if(!flipper_format_read_uint32(flipper_format, "Btn", &tmp, 1)) {
            FURI_LOG_E(TAG, "Missing Btn");
            break;
        }
        instance->generic.btn = (uint8_t)tmp;

        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(flipper_format, "Counter", &instance->generic.cnt, 1)) {
            FURI_LOG_E(TAG, "Missing Counter");
            break;
        }

        uint32_t override_cnt = 0;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            instance->generic.cnt = override_cnt & 0xFFFF;
        }

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(porsche_cayenne_btn_to_custom(instance->generic.btn));
        }
        subghz_custom_btn_set_max(4);

        porsche_cayenne_build_upload(instance);

        flipper_format_rewind(flipper_format);
        uint32_t new_cnt = instance->generic.cnt;
        flipper_format_insert_or_update_uint32(flipper_format, "Counter", &new_cnt, 1);

        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &new_cnt, 1);

        flipper_format_rewind(flipper_format);
        uint32_t new_btn = instance->generic.btn;
        flipper_format_insert_or_update_uint32(flipper_format, "Btn", &new_btn, 1);

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_porsche_cayenne_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderPorscheCayenne* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_porsche_cayenne_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderPorscheCayenne* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

static const SubGhzProtocolEncoder enc_porsche_cayenne_vt = {
    .alloc = subghz_protocol_encoder_porsche_cayenne_alloc,
    .free = subghz_protocol_encoder_porsche_cayenne_free,
    .deserialize = subghz_protocol_encoder_porsche_cayenne_deserialize,
    .stop = subghz_protocol_encoder_porsche_cayenne_stop,
    .yield = subghz_protocol_encoder_porsche_cayenne_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_porsche_cayenne = {
    .name = SUBGHZ_PROTOCOL_PORSCHE_CAYENNE_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM  | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_porsche_cayenne_vt,
};

static const SubGhzEncodePluginEntry enc_porsche_cayenne_entry = {.name = SUBGHZ_PROTOCOL_PORSCHE_CAYENNE_NAME, .encoder = &enc_porsche_cayenne_vt};
static const FlipperAppPluginDescriptor enc_porsche_cayenne_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_porsche_cayenne_entry};
const FlipperAppPluginDescriptor* enc_porsche_cayenne_ep(void) { return &enc_porsche_cayenne_descriptor; }
