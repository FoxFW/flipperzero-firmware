

#define subghz_protocol_encoder_common_free enc_datsun_lada__common_free
#define subghz_protocol_encoder_common_stop enc_datsun_lada__common_stop
#define subghz_protocol_encoder_common_yield enc_datsun_lada__common_yield
#define subghz_protocol_encoder_datsun_lada_alloc enc_datsun_lada__alloc
#define subghz_protocol_encoder_datsun_lada_deserialize enc_datsun_lada__deserialize
#define subghz_protocol_datsun_lada enc_datsun_lada__subghz_protocol_datsun_lada

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/datsun_lada.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define DL_FRAME_BYTES 11
#define DL_COUNTER_MASK 0x3FFFU
#define DL_UPLOAD_CAPACITY 3200
#define DL_TE_SHORT 250U
#define DL_TE_LONG 500U

static const uint8_t DL_BUTTON_CODES[8] = {0x40, 0x80, 0x20, 0x20, 0x72, 0x6A, 0x5A, 0x32};

struct SubGhzProtocolEncoderDatsunLada {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint8_t raw[DL_FRAME_BYTES];
};

static inline uint8_t dl_bit(uint8_t v, uint8_t i) {
    return (v >> i) & 1;
}
static inline uint8_t dl_mask(uint8_t v) {
    return (uint8_t)(1u << (v & 7));
}
static uint8_t dl_predicate(const uint8_t* r) {
    uint8_t acc, flag;
    acc = (uint8_t)(dl_bit(r[10], 5) | (dl_bit(r[10], 4) << 1) | (dl_bit(r[10], 2) << 2));
    flag = (dl_mask(acc) & (dl_bit(r[10], 1) ? 0x2C : 0x79)) ? 1u : 0u;
    acc = (uint8_t)(dl_bit(r[9], 7) | (dl_bit(r[9], 3) << 1) | (dl_bit(r[9], 1) << 2));
    if(dl_mask(acc) & (dl_bit(r[9], 0) ? 0x66 : 0x71)) flag |= 2;
    acc = (uint8_t)(dl_bit(r[8], 6) | (dl_bit(r[8], 2) << 1) | (dl_bit(r[8], 0) << 2));
    if(dl_mask(acc) & (dl_bit(r[7], 5) ? 0x66 : 0x71)) flag |= 4;
    acc = (uint8_t)(dl_bit(r[7], 3) | (dl_bit(r[7], 2) << 1) | (dl_bit(r[7], 0) << 2));
    if(dl_mask(acc) & (dl_bit(r[6], 6) ? 0x66 : 0x71)) flag |= 8;
    acc = (uint8_t)(dl_bit(r[6], 5) | (dl_bit(r[5], 4) << 1) | (dl_bit(r[5], 3) << 2));
    uint8_t slot;
    if(dl_mask(acc) & (dl_bit(r[5], 1) ? 0x2C : 0x79))
        slot = dl_bit(flag, 3) ? 0x79 : 0x07;
    else
        slot = dl_bit(flag, 3) ? 0x28 : 0x7B;
    return (dl_mask(flag) & slot) ? 1u : 0u;
}
static uint8_t dl_loop_carry(const uint8_t* r) {
    uint8_t mixed =
        (uint8_t)((r[10] & 0xB3) ^ (r[9] & 0x80) ^ (r[8] & 0x83) ^ (r[7] & 0x22) ^ (r[5] & 0x73));
    uint8_t carry = (dl_mask(mixed) & (dl_bit(mixed, 3) ? 0x69 : 0x96)) ? 1u : 0u;
    mixed = (uint8_t)(mixed >> 4);
    if(dl_mask(mixed) & (dl_bit(mixed, 3) ? 0x69 : 0x96)) carry ^= 1;
    return carry;
}
static uint32_t dl_read32(const uint8_t* r, int off) {
    return (uint32_t)r[off] | ((uint32_t)r[off + 1] << 8) | ((uint32_t)r[off + 2] << 16) |
           ((uint32_t)r[off + 3] << 24);
}
static void dl_write32(uint8_t* r, int off, uint32_t word) {
    r[off] = (uint8_t)word;
    r[off + 1] = (uint8_t)(word >> 8);
    r[off + 2] = (uint8_t)(word >> 16);
    r[off + 3] = (uint8_t)(word >> 24);
}
static void dl_shift_left(uint8_t* r, int first, int last, uint8_t carry) {
    for(int i = first; i <= last; i++) {
        uint8_t next = (uint8_t)(r[i] >> 7);
        r[i] = (uint8_t)((r[i] << 1) | (carry & 1));
        carry = next;
    }
}
static void dl_shift_right(uint8_t* r) {
    for(int p = 0; p < 2; p++) {
        uint8_t carry = 0;
        for(int i = 6; i >= 1; i--) {
            uint8_t next = (uint8_t)(r[i] & 1);
            r[i] = (uint8_t)((r[i] >> 1) | (carry << 7));
            carry = next;
        }
    }
}
static inline uint32_t dl_rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}
static void dl_rounds(uint8_t* r, uint32_t* stack, uint32_t* state) {
    for(int i = 0; i < 32; i++) {
        uint8_t hit = (uint8_t)(dl_predicate(r) ^ (uint8_t)(*stack >> 31) ^ (uint8_t)(*state >> 31));
        dl_shift_left(r, 5, 10, hit);
        *stack <<= 1;
        *state = dl_rotl32(*state, 1);
    }
    for(int i = 0; i < 32; i++) {
        *stack = (*stack << 1) | dl_predicate(r);
        uint8_t carry = dl_loop_carry(r);
        dl_shift_left(r, 5, 10, carry);
    }
}
static void dl_restore(uint8_t* r, uint32_t stack, uint32_t state, uint8_t saved) {
    dl_write32(r, 1, stack);
    r[5] = (uint8_t)state;
    r[6] = saved;
    dl_shift_left(r, 1, 6, 0);
    dl_shift_left(r, 1, 6, 0);
}
static bool dl_checksum(const uint8_t* f) {
    uint8_t x = 0;
    for(int i = 0; i < 10; i++) x ^= f[i];
    return x == f[10];
}
static bool dl_learn(const uint8_t* frame, uint32_t* state_out) {
    uint8_t r[DL_FRAME_BYTES];
    memcpy(r, frame, DL_FRAME_BYTES);
    dl_shift_right(r);
    uint32_t state = (uint32_t)r[5] | ((uint32_t)(r[6] & 3) << 8) |
                     ((uint32_t)((uint8_t)(r[6] << 2) & 0xF0) << 24);
    uint32_t stack = dl_read32(r, 7);
    r[6] = 0x46;
    r[5] = 6;
    dl_rounds(r, &stack, &state);
    *state_out = state;
    return stack == dl_read32(r, 1);
}
static uint16_t dl_counter_of(uint32_t state) {
    return (uint16_t)((state & 0xFF) | ((state >> 2) & 0x3F00));
}

static uint8_t dl_build(const uint8_t* learned, uint32_t state, uint16_t counter, uint8_t selector,
                        uint8_t* out) {
    uint8_t r[DL_FRAME_BYTES];
    memset(r, 0, sizeof(r));
    for(int i = 7; i < DL_FRAME_BYTES; i++) r[i] = learned[i];
    uint8_t code = DL_BUTTON_CODES[selector & 7];
    uint8_t button = (uint8_t)((learned[6] & 0x0F) | code);
    r[6] = button;
    state = (state & 0x00FF0000U) | (uint32_t)(counter & 0xFF) |
            (((uint32_t)((counter >> 6) & 0xFC) | (uint32_t)((learned[6] >> 2) & 3)) << 8) |
            ((((state >> 24) & 0x0F) | (uint32_t)code) << 24);
    dl_shift_right(r);
    uint8_t saved = r[6];
    uint32_t stack = dl_read32(r, 7);
    r[6] = 0x46;
    r[5] = 6;
    dl_rounds(r, &stack, &state);
    dl_restore(r, stack, state, saved);
    if(stack == dl_read32(r, 1)) dl_restore(r, stack, state, saved);
    for(int i = 7; i < DL_FRAME_BYTES; i++) r[i] = learned[i];
    r[6] = button;
    r[1] = (uint8_t)((r[1] & 0xFE) | 2);
    uint8_t x = 0;
    for(int i = 1; i < DL_FRAME_BYTES; i++) x ^= r[i];
    r[0] = x;
    memcpy(out, r, DL_FRAME_BYTES);
    return (uint8_t)state;
}

static void dl_emit(struct SubGhzProtocolEncoderDatsunLada* instance, bool level, uint32_t dur) {
    size_t* idx = &instance->encoder.size_upload;
    if(*idx > 0) {
        LevelDuration* last = &instance->encoder.upload[*idx - 1];
        if(level_duration_get_level(*last) == level) {
            *last = level_duration_make(level, level_duration_get_duration(*last) + dur);
            return;
        }
    }
    if(*idx < DL_UPLOAD_CAPACITY) instance->encoder.upload[(*idx)++] = level_duration_make(level, dur);
}

static void dl_tx_preamble(struct SubGhzProtocolEncoderDatsunLada* instance, int toggles) {
    dl_emit(instance, false, 2000);
    for(int i = 0; i < toggles; i++) dl_emit(instance, (i & 1) == 0, DL_TE_SHORT);
    dl_emit(instance, false, DL_TE_LONG);
    dl_emit(instance, true, DL_TE_SHORT);
}
static void dl_tx_window(struct SubGhzProtocolEncoderDatsunLada* instance, const uint8_t* frame,
                         int bytes) {
    for(int i = 0; i < bytes * 8; i++) {
        uint8_t b = frame[bytes - 1 - (i >> 3)];
        bool value = ((b >> (7 - (i & 7))) & 1) != 0;
        dl_emit(instance, !value, DL_TE_SHORT);
        dl_emit(instance, value, DL_TE_SHORT);
    }
}
static void dl_transmit(struct SubGhzProtocolEncoderDatsunLada* instance, const uint8_t* air,
                        uint8_t tail_seed, uint8_t selector) {
    instance->encoder.size_upload = 0;
    dl_emit(instance, true, 200);
    for(int f = 0; f < 2; f++) {
        dl_tx_preamble(instance, 125);
        dl_tx_window(instance, air, DL_FRAME_BYTES);
        dl_emit(instance, true, 4000);
    }
    int limit = (selector == 3) ? 128 : 16;
    for(int step = 0; step < limit; step += 4) {
        uint8_t t2 = (uint8_t)(((air[6] >> 2) & 3) + step);
        uint8_t t1 = tail_seed;
        uint8_t t0 = (uint8_t)(t1 ^ t2);
        uint8_t tail[3] = {t0, t1, t2};
        dl_tx_preamble(instance, 29);
        dl_tx_window(instance, tail, 3);
        dl_emit(instance, true, 36000);
    }
}

void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}
LevelDuration subghz_protocol_encoder_common_yield(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
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

void* subghz_protocol_encoder_datsun_lada_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    struct SubGhzProtocolEncoderDatsunLada* instance =
        malloc(sizeof(struct SubGhzProtocolEncoderDatsunLada));
    instance->base.protocol = &subghz_protocol_datsun_lada;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = malloc(DL_UPLOAD_CAPACITY * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    return instance;
}

static int dl_selector_from_custom(uint8_t custom) {
    switch(custom) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 3:
        return 3;
    case 4:
        return 5;
    default:
        return -1;
    }
}
static int dl_selector_from_code(uint8_t code) {
    switch(code) {
    case 0x40:
        return 0;
    case 0x80:
        return 1;
    case 0x20:
        return 3;
    case 0x60:
        return 5;
    default:
        return -1;
    }
}

SubGhzProtocolStatus subghz_protocol_encoder_datsun_lada_deserialize(void* context,
                                                                     FlipperFormat* flipper_format) {
    furi_assert(context);
    struct SubGhzProtocolEncoderDatsunLada* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;

        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, "Frame", instance->raw, DL_FRAME_BYTES)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!dl_checksum(instance->raw)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint32_t state = 0;
        if(!dl_learn(instance->raw, &state)) {

            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        uint8_t captured_code = (uint8_t)(instance->raw[6] & 0xF0);
        uint8_t original_custom =
            (captured_code == 0x40) ?
                1 :
                (captured_code == 0x80) ? 2 : (captured_code == 0x20) ? 3 : (captured_code == 0x60) ? 4 : 1;
        if(subghz_custom_btn_get_original() == 0) subghz_custom_btn_set_original(original_custom);
        subghz_custom_btn_set_max(4);

        uint8_t sel_custom;
        if(subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK)
            sel_custom = subghz_custom_btn_get_original();
        else
            sel_custom = subghz_custom_btn_get();

        int selector = dl_selector_from_custom(sel_custom);
        if(selector < 0) selector = dl_selector_from_code(captured_code);
        if(selector < 0) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        uint16_t base = instance->generic.cnt ? (uint16_t)(instance->generic.cnt & DL_COUNTER_MASK) :
                                                dl_counter_of(state);
        uint16_t counter = (uint16_t)((base + 1) & DL_COUNTER_MASK);

        uint8_t air[DL_FRAME_BYTES];
        uint8_t tail_seed = dl_build(instance->raw, state, counter, (uint8_t)selector, air);
        dl_transmit(instance, air, tail_seed, (uint8_t)selector);

        instance->generic.btn = (uint8_t)(air[6] & 0xF0);
        instance->generic.cnt = counter;
        instance->encoder.front = 0;
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_datsun_lada_vt = {
    .alloc = subghz_protocol_encoder_datsun_lada_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_datsun_lada_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_datsun_lada = {
    .name = SUBGHZ_PROTOCOL_DATSUN_LADA_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_datsun_lada_vt,
};

static const SubGhzEncodePluginEntry enc_datsun_lada_entry = {
    .name = SUBGHZ_PROTOCOL_DATSUN_LADA_NAME,
    .encoder = &enc_datsun_lada_vt};
static const FlipperAppPluginDescriptor enc_datsun_lada_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_datsun_lada_entry};
const FlipperAppPluginDescriptor* enc_datsun_lada_ep(void) {
    return &enc_datsun_lada_descriptor;
}
