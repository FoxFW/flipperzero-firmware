#include "code_alarm.h"
#include "protocols_common.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

/*
 * Code Alarm (FRDPC2002 / FRDPC2000) car remote decoder.
 *
 * Ported from the rtl_433 device "code_alarm_car_remote.c".
 *
 * Physical layer (rtl_433 r_device):
 *   .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT
 *   .short_width = 550us   (half-bit / Manchester clock)
 *   .long_width  = 1100us  (full bit)
 *   .reset_limit = 1600us  (inter-frame gap / row reset)
 *   .tolerance   = 100us
 *   Modulation on the Flipper: OOK / AM650.
 *
 * Frame: exactly 60 Manchester bits.
 *   bits  0..15 : preamble, must be 0x0000
 *   bits 16..19 : unknown nibble
 *   bits 20..23 : button flags
 *   bits 24..55 : 32-bit raw rolling code region
 *   bits 56..59 : unknown nibble
 *
 * rtl_433 extractions (bit offsets into the 60-bit row):
 *   bytes[5] <- extract 40 bits @ offset 19   (checksum + button live here)
 *   code[5]  <- extract 36 bits @ offset 23   (rolling code)
 *
 *   button    = bytes[0] >> 4
 *   id (24b)  = ((code[0]^code[1])<<16) | ((code[1]^code[2])<<8) | (code[2]^code[3])
 *   checksum  = add_bytes(bytes, 5); valid when (sum != 0 && sum < 0xFF*5)
 *
 * Because the Flipper receiver hands us raw LevelDuration pulses (no
 * pre-demodulated bitstream like rtl_433), feed() re-implements the Manchester
 * demod as a pulse-duration state machine using the shared manchester_decoder
 * toolbox, then applies the exact rtl_433 field slicing + sanity checksum. The
 * checksum gate is what keeps random 60-bit Manchester runs from registering as
 * false Code Alarm frames.
 */

#define TAG "CodeAlarm"

#define CODE_ALARM_FRAME_BITS 60U

static const SubGhzBlockConst subghz_protocol_code_alarm_const = {
    .te_short = 550,
    .te_long = 1100,
    .te_delta = 200,
    .min_count_bit_for_found = CODE_ALARM_FRAME_BITS,
};

typedef struct SubGhzProtocolDecoderCodeAlarm {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
} SubGhzProtocolDecoderCodeAlarm;

typedef enum {
    CodeAlarmDecoderStepReset = 0,
    CodeAlarmDecoderStepData,
} CodeAlarmDecoderStep;

// =============================================================================
// PROTOCOL INTERFACE
// =============================================================================

const SubGhzProtocolDecoder subghz_protocol_code_alarm_decoder = {
    .alloc = subghz_protocol_decoder_code_alarm_alloc,
    .free = pp_decoder_free_default,
    .feed = subghz_protocol_decoder_code_alarm_feed,
    .reset = subghz_protocol_decoder_code_alarm_reset,
    .get_hash_data = pp_decoder_hash_blocks,
    .serialize = subghz_protocol_decoder_code_alarm_serialize,
    .deserialize = subghz_protocol_decoder_code_alarm_deserialize,
    .get_string = subghz_protocol_decoder_code_alarm_get_string,
};

// No TX support: replay-only stub encoder (rolling code is encrypted).
const SubGhzProtocolEncoder subghz_protocol_code_alarm_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_code_alarm = {
    .name = CODE_ALARM_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_code_alarm_decoder,
    .encoder = &subghz_protocol_code_alarm_encoder,
};

// =============================================================================
// FIELD DECODE (mirrors rtl_433 code_alarm_car_remote_decode)
// =============================================================================

static const char* code_alarm_button_name(uint8_t button) {
    // Order matches the rtl_433 button_map: first match wins for shared codes.
    switch(button) {
    case 0x7:
        return "Multiple";
    case 0x6:
    case 0x4:
        return "Lock";
    case 0x1:
    case 0x3:
        return "Panic";
    case 0x0:
        return "Start";
    case 0x5:
        return "Unlock";
    default:
        return "Unknown";
    }
}

/*
 * Validate + decode a completed 60-bit frame held in `data` (bit 0 = MSB, i.e.
 * first received bit at weight 2^59). Fills instance->generic on success.
 * Returns true if the frame passes the rtl_433 sanity gates.
 */
static bool code_alarm_check_and_decode(SubGhzProtocolDecoderCodeAlarm* instance, uint64_t data) {
    // Preamble: bits 0..15 must be zero -> top 16 bits of the 60-bit word.
    if((data >> 44) != 0) {
        return false;
    }

    // bytes[5] = 40 bits @ offset 19  ->  bits 19..58  ->  (data >> 1) & 40 ones
    uint64_t payload40 = (data >> 1) & 0xFFFFFFFFFFULL;
    uint8_t bytes[5];
    bytes[0] = (uint8_t)(payload40 >> 32);
    bytes[1] = (uint8_t)(payload40 >> 24);
    bytes[2] = (uint8_t)(payload40 >> 16);
    bytes[3] = (uint8_t)(payload40 >> 8);
    bytes[4] = (uint8_t)(payload40);

    // add_bytes(bytes, 5) sanity gate: reject all-zero and all-ones payloads.
    uint16_t bytes_sum =
        (uint16_t)bytes[0] + bytes[1] + bytes[2] + bytes[3] + bytes[4];
    if(bytes_sum == 0 || bytes_sum >= (0xFFU * 5U)) {
        return false;
    }

    // code[5] = 36 bits @ offset 23  ->  bits 23..58  ->  (data >> 1) & 36 ones
    uint64_t code36 = payload40 & 0xFFFFFFFFFULL;
    uint8_t code[4];
    code[0] = (uint8_t)(code36 >> 28);
    code[1] = (uint8_t)(code36 >> 20);
    code[2] = (uint8_t)(code36 >> 12);
    code[3] = (uint8_t)(code36 >> 4);

    uint32_t id = ((uint32_t)(code[0] ^ code[1]) << 16) |
                  ((uint32_t)(code[1] ^ code[2]) << 8) | (uint32_t)(code[2] ^ code[3]);

    uint8_t button = bytes[0] >> 4;

    instance->generic.data = data;
    instance->generic.data_count_bit = CODE_ALARM_FRAME_BITS;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = 0; // rolling code is encrypted; no plain counter

    FURI_LOG_D(TAG, "Frame ok: Id:%06lX Btn:%X", (unsigned long)id, button);
    return true;
}

// =============================================================================
// DECODER
// =============================================================================

void* subghz_protocol_decoder_code_alarm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderCodeAlarm* instance = calloc(1, sizeof(SubGhzProtocolDecoderCodeAlarm));
    instance->base.protocol = &subghz_protocol_code_alarm;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_code_alarm_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderCodeAlarm* instance = context;
    instance->decoder.parser_step = CodeAlarmDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_code_alarm_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderCodeAlarm* instance = context;

    ManchesterEvent event =
        pp_manchester_event(duration, level, &subghz_protocol_code_alarm_const);

    switch(instance->decoder.parser_step) {
    case CodeAlarmDecoderStepReset:
        // The inter-frame gap (>= reset_limit) shows up as an out-of-range
        // duration -> ManchesterEventReset. That marks the start boundary: begin
        // accumulating the next frame from a clean Manchester state.
        if(event == ManchesterEventReset) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
            instance->decoder.parser_step = CodeAlarmDecoderStepData;
        }
        break;

    case CodeAlarmDecoderStepData:
        if(event == ManchesterEventReset) {
            // End of frame (or noise): evaluate what we have, then resync.
            if(instance->decoder.decode_count_bit == CODE_ALARM_FRAME_BITS) {
                if(code_alarm_check_and_decode(instance, instance->decoder.decode_data)) {
                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
            }
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        } else {
            bool data_bit;
            if(manchester_advance(
                   instance->manchester_state,
                   event,
                   &instance->manchester_state,
                   &data_bit)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, data_bit ? 1 : 0);

                // Guard against runaway accumulation on a noisy Manchester run.
                if(instance->decoder.decode_count_bit > CODE_ALARM_FRAME_BITS + 4U) {
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                    manchester_advance(
                        instance->manchester_state,
                        ManchesterEventReset,
                        &instance->manchester_state,
                        NULL);
                }
            }
        }
        break;
    }
}

// =============================================================================
// SERIALIZE / DESERIALIZE / STRING
// =============================================================================

SubGhzProtocolStatus subghz_protocol_decoder_code_alarm_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderCodeAlarm* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        ret = pp_serialize_fields(
            flipper_format,
            PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
            instance->generic.serial,
            instance->generic.btn,
            instance->generic.cnt,
            0);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_code_alarm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderCodeAlarm* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_code_alarm_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t serial = instance->generic.serial;
        uint32_t btn = instance->generic.btn;
        uint32_t cnt = instance->generic.cnt;
        pp_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);
        instance->generic.serial = serial;
        instance->generic.btn = btn;
        instance->generic.cnt = cnt;
    }

    return ret;
}

void subghz_protocol_decoder_code_alarm_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderCodeAlarm* instance = context;

    uint32_t data_hi = (uint32_t)(instance->generic.data >> 32);
    uint32_t data_lo = (uint32_t)(instance->generic.data & 0xFFFFFFFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Data:%03lX%08lX\r\n"
        "Id:%06lX\r\n"
        "Btn:%X - %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(data_hi & 0xFFF),
        (unsigned long)data_lo,
        (unsigned long)instance->generic.serial,
        (unsigned int)instance->generic.btn,
        code_alarm_button_name((uint8_t)instance->generic.btn));
}
