/* enc_urmet.fal : SD encoder for urmet (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_urmet_alloc enc_urmet__subghz_protocol_encoder_urmet_alloc
#define subghz_protocol_encoder_urmet_deserialize enc_urmet__subghz_protocol_encoder_urmet_deserialize
#define subghz_protocol_encoder_urmet_free enc_urmet__subghz_protocol_encoder_urmet_free
#define subghz_protocol_encoder_urmet_stop enc_urmet__subghz_protocol_encoder_urmet_stop
#define subghz_protocol_encoder_urmet_yield enc_urmet__subghz_protocol_encoder_urmet_yield
#define subghz_protocol_urmet enc_urmet__subghz_protocol_urmet
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/urmet.h>
#include <string.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolUrmet"
#define URMET_TE              500
#define URMET_PAUSE_TE        68 // ~34 ms trailing low on TX
#define URMET_MIN_BYTES       16
#define URMET_MAX_BYTES       40
#define URMET_MAX_GAPS        192
#define URMET_BITBUF          512
#define URMET_UPLOAD_MAX      (8 + URMET_MAX_GAPS * 2)
#define URMET_RAW_FIELD       "Raw"
#define URMET_GAPS_FIELD      "Gaps"

struct SubGhzProtocolEncoderUrmet {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_bytes[URMET_MAX_BYTES];
    uint8_t byte_count;

    uint8_t gap_count;
    uint8_t gaps[URMET_MAX_GAPS];
};

static uint64_t urmet_bytes_to_key(const uint8_t* bytes, uint8_t count);
static void urmet_key_to_bytes(uint64_t key, uint8_t* bytes, uint8_t count);
static bool urmet_byte_count_ok(uint8_t byte_count);
// Build gap list from USART bytes (fallback when Gaps field missing).
static uint8_t urmet_gaps_from_bytes(const uint8_t* bytes, uint8_t byte_count, uint8_t* gaps);
static bool urmet_load_from_ff(
    FlipperFormat* flipper_format,
    uint8_t* raw_bytes,
    uint8_t* byte_count,
    uint8_t* gaps,
    uint8_t* gap_count,
    uint16_t data_count_bit,
    uint64_t key);
static void urmet_upload_add(
    LevelDuration* upload,
    size_t* index,
    size_t max,
    bool level,
    uint32_t te_count);
static bool subghz_protocol_encoder_urmet_get_upload(SubGhzProtocolEncoderUrmet* instance);

static uint64_t urmet_bytes_to_key(const uint8_t* bytes, uint8_t count) {
    uint64_t key = 0;
    uint8_t n = count < 8 ? count : 8;
    for(uint8_t i = 0; i < n; i++) {
        key = (key << 8) | bytes[i];
    }
    if(n < 8) {
        key <<= (uint8_t)((8 - n) * 8);
    }
    return key;
}

static void urmet_key_to_bytes(uint64_t key, uint8_t* bytes, uint8_t count) {
    uint8_t n = count < 8 ? count : 8;
    for(uint8_t i = 0; i < n; i++) {
        bytes[i] = (uint8_t)(key >> (56 - i * 8));
    }
}

static bool urmet_byte_count_ok(uint8_t byte_count) {
    return byte_count >= URMET_MIN_BYTES && byte_count <= URMET_MAX_BYTES;
}

// Build gap list from USART bytes (fallback when Gaps field missing).
static uint8_t urmet_gaps_from_bytes(const uint8_t* bytes, uint8_t byte_count, uint8_t* gaps) {
    // Bit stream -> RLE lows after each high run of 1*te (highs always split to 1).
    uint8_t tmp_bits[URMET_BITBUF];
    uint16_t bit_cnt = 0;
    // Leading idle not stored as a gap; first edge is start high of byte 0.
    for(uint8_t bi = 0; bi < byte_count && bit_cnt + 10 < URMET_BITBUF; bi++) {
        uint8_t val = bytes[bi];
        tmp_bits[bit_cnt++] = 1; // start
        for(uint8_t b = 0; b < 8; b++) {
            bool logical = (val >> b) & 1;
            tmp_bits[bit_cnt++] = logical ? 0 : 1;
        }
        tmp_bits[bit_cnt++] = 0; // stop
    }

    uint8_t gap_count = 0;
    uint16_t i = 0;
    while(i < bit_cnt && gap_count < URMET_MAX_GAPS) {
        if(tmp_bits[i] != 1) {
            i++;
            continue;
        }
        // Consume exactly one high te (USART highs are 1).
        i++;
        uint8_t low = 0;
        while(i < bit_cnt && tmp_bits[i] == 0 && low < 20) {
            low++;
            i++;
        }
        if(low == 0) low = 1;
        gaps[gap_count++] = low;
    }
    return gap_count;
}

static bool urmet_load_from_ff(
    FlipperFormat* flipper_format,
    uint8_t* raw_bytes,
    uint8_t* byte_count,
    uint8_t* gaps,
    uint8_t* gap_count,
    uint16_t data_count_bit,
    uint64_t key) {
    uint32_t raw_len = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, URMET_RAW_FIELD, &raw_len) && raw_len > 0) {
        if(raw_len > URMET_MAX_BYTES) {
            FURI_LOG_E(TAG, "Raw too long: %lu", (unsigned long)raw_len);
            return false;
        }
        memset(raw_bytes, 0, URMET_MAX_BYTES);
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, URMET_RAW_FIELD, raw_bytes, raw_len)) {
            FURI_LOG_E(TAG, "Failed to read Raw");
            return false;
        }
        *byte_count = (uint8_t)raw_len;
    } else {
        if(data_count_bit < URMET_MIN_BYTES * 8 || data_count_bit > URMET_MAX_BYTES * 8 ||
           (data_count_bit % 8) != 0) {
            return false;
        }
        *byte_count = (uint8_t)(data_count_bit / 8);
        memset(raw_bytes, 0, URMET_MAX_BYTES);
        urmet_key_to_bytes(key, raw_bytes, *byte_count);
    }

    if(!urmet_byte_count_ok(*byte_count)) return false;

    uint32_t gaps_len = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, URMET_GAPS_FIELD, &gaps_len) &&
       gaps_len > 0) {
        if(gaps_len > URMET_MAX_GAPS) {
            FURI_LOG_E(TAG, "Gaps too long: %lu", (unsigned long)gaps_len);
            return false;
        }
        memset(gaps, 0, URMET_MAX_GAPS);
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, URMET_GAPS_FIELD, gaps, gaps_len)) {
            FURI_LOG_E(TAG, "Failed to read Gaps");
            return false;
        }
        *gap_count = (uint8_t)gaps_len;
    } else {
        *gap_count = urmet_gaps_from_bytes(raw_bytes, *byte_count, gaps);
    }

    return *gap_count >= 2;
}

static void urmet_upload_add(
    LevelDuration* upload,
    size_t* index,
    size_t max,
    bool level,
    uint32_t te_count) {
    if(te_count == 0 || *index >= max) return;
    uint32_t duration = te_count * (uint32_t)URMET_TE;
    if(*index > 0) {
        LevelDuration* last = &upload[*index - 1];
        if(level_duration_get_level(*last) == level) {
            *last = level_duration_make(level, level_duration_get_duration(*last) + duration);
            return;
        }
    }
    upload[(*index)++] = level_duration_make(level, duration);
}

static bool subghz_protocol_encoder_urmet_get_upload(SubGhzProtocolEncoderUrmet* instance) {
    furi_assert(instance);
    LevelDuration* up = instance->encoder.upload;
    size_t index = 0;

    if(instance->gap_count < 2) {
        FURI_LOG_E(TAG, "Missing Gaps for TX");
        return false;
    }

    // Exact PPM shape: fixed high 1*te + captured low gap te-multiples.
    for(uint8_t i = 0; i < instance->gap_count; i++) {
        uint8_t g = instance->gaps[i];
        if(g < 1) g = 1;
        if(g > 20) g = 20;
        urmet_upload_add(up, &index, URMET_UPLOAD_MAX, true, 1);
        urmet_upload_add(up, &index, URMET_UPLOAD_MAX, false, g);
    }

    // Terminating high pulse. On air every frame ends with one more 1*te high
    // immediately before the inter-frame silence; the decoder consumes that
    // last high as the end-of-frame marker (pending_high dropped at the end
    // gap), so it is never stored in the gap list. Without re-emitting it the
    // TX is one pulse short of the captured frame and the receiver rejects it.
    urmet_upload_add(up, &index, URMET_UPLOAD_MAX, true, 1);

    // Trailing inter-frame pause (~34 ms).
    urmet_upload_add(up, &index, URMET_UPLOAD_MAX, false, URMET_PAUSE_TE);

    if(index == 0 || index > URMET_UPLOAD_MAX) {
        FURI_LOG_E(
            TAG, "Upload build failed (index=%u max=%u)", (unsigned)index, URMET_UPLOAD_MAX);
        return false;
    }

    instance->encoder.size_upload = index;
    return true;
}

// ---------------------------------------------------------------- Encoder

void* subghz_protocol_encoder_urmet_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderUrmet* instance = malloc(sizeof(SubGhzProtocolEncoderUrmet));

    instance->base.protocol = &subghz_protocol_urmet;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->byte_count = 0;
    instance->gap_count = 0;
    memset(instance->raw_bytes, 0, sizeof(instance->raw_bytes));
    memset(instance->gaps, 0, sizeof(instance->gaps));

    instance->encoder.repeat = 5;
    instance->encoder.size_upload = URMET_UPLOAD_MAX;
    instance->encoder.upload = malloc(URMET_UPLOAD_MAX * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_urmet_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderUrmet* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_urmet_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderUrmet* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;

        if(!urmet_load_from_ff(
               flipper_format,
               instance->raw_bytes,
               &instance->byte_count,
               instance->gaps,
               &instance->gap_count,
               instance->generic.data_count_bit,
               instance->generic.data)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        instance->generic.data_count_bit = (uint16_t)instance->byte_count * 8;
        instance->generic.data = urmet_bytes_to_key(instance->raw_bytes, instance->byte_count);

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_urmet_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
        instance->encoder.front = 0;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_urmet_stop(void* context) {
    SubGhzProtocolEncoderUrmet* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_urmet_yield(void* context) {
    SubGhzProtocolEncoderUrmet* instance = context;

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

static const SubGhzProtocolEncoder enc_urmet_vt = {
    .alloc = subghz_protocol_encoder_urmet_alloc,
    .free = subghz_protocol_encoder_urmet_free,
    .deserialize = subghz_protocol_encoder_urmet_deserialize,
    .stop = subghz_protocol_encoder_urmet_stop,
    .yield = subghz_protocol_encoder_urmet_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_urmet = {
    .name = SUBGHZ_PROTOCOL_URMET_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_urmet_vt,
};

static const SubGhzEncodePluginEntry enc_urmet_entry = {.name = SUBGHZ_PROTOCOL_URMET_NAME, .encoder = &enc_urmet_vt};
static const FlipperAppPluginDescriptor enc_urmet_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_urmet_entry};
const FlipperAppPluginDescriptor* enc_urmet_ep(void) { return &enc_urmet_descriptor; }
