

#define subghz_protocol_audi_v1 enc_audi_v1__subghz_protocol_audi_v1
#define subghz_protocol_encoder_audi_v1_alloc enc_audi_v1__subghz_protocol_encoder_audi_v1_alloc
#define subghz_protocol_encoder_audi_v1_deserialize enc_audi_v1__subghz_protocol_encoder_audi_v1_deserialize
#define subghz_protocol_encoder_common_free enc_audi_v1__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_audi_v1__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_audi_v1__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/audi_v1.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolAudiV1"
#define AUDI_V1_TE_SYNC         3000
#define AUDI_V1_TE_HEADER       2000
#define AUDI_V1_DATA_BITS       88
#define AUDI_V1_BTN_LOCK   0x0E
#define AUDI_V1_BTN_UNLOCK 0x0B

struct SubGhzProtocolEncoderAudiV1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t hop;
    uint8_t btn_byte;
    uint32_t serial;
};

static const SubGhzBlockConst subghz_protocol_audi_v1_const = {
    .te_short = 260,
    .te_long = 520,
    .te_delta = 130,
    .min_count_bit_for_found = 88,
};

static const uint64_t AUDI_V1_HOP_PREDICT[256] = {
    0x0374560849F1ULL,
    0x05545A2AC979ULL,
    0x0B5ECBE721C9ULL,
    0x0D7EC2268187ULL,
    0x13544B1069E4ULL,
    0x1574C01AE5CFULL,
    0x1B7CDB6E0D4EULL,
    0x1D5C5F2D41E3ULL,
    0x23744E49A564ULL,
    0x2554406C61ADULL,
    0x2B5ED97B0543ULL,
    0x2D7ED87AF595ULL,
    0x33545CDA4D3DULL,
    0x3574D8DA4597ULL,
    0x3B74CC3C153BULL,
    0x3D546F2C79C2ULL,
    0x4354C81B0154ULL,
    0x45744A10956FULL,
    0x4B7C59CE0938ULL,
    0x4D5CDC84C911ULL,
    0x537442A9015CULL,
    0x5554EEA08D53ULL,
    0x5B5EDE4F0D08ULL,
    0x5D7E7726F112ULL,
    0x6354DA5A5573ULL,
    0x65747E52218DULL,
    0x6B7449866D2CULL,
    0x6D54CB96996CULL,
    0x737458CC9597ULL,
    0x7554D7EC31BAULL,
    0x7B5ECE5B0120ULL,
    0x7D7E4F72A906ULL,
    0x8375D71C05A9ULL,
    0x8575D22FE975ULL,
    0x8B5F5F85AD32ULL,
    0x8D5F7A240518ULL,
    0x93555D00F5FFULL,
    0x95555B4A6D2DULL,
    0x9B7DDC0CE542ULL,
    0x9D7DDC0EA506ULL,
    0xA355CE696DADULL,
    0xA555C97AFD2FULL,
    0xAB7E4F40115AULL,
    0xAD7E4A68CDADULL,
    0xB37546C2AD7EULL,
    0xB5756FC8798FULL,
    0xBB54CF4CA954ULL,
    0xBD54E01C61E5ULL,
    0xC355480AC580ULL,
    0xC555CE42810CULL,
    0xCB7DCAAC3D7CULL,
    0xCD7D4BA4A16FULL,
    0xD3755A8515FDULL,
    0xD57576B68170ULL,
    0xDB5FDE2E896CULL,
    0xDD5FD726698BULL,
    0xE375544AB9A0ULL,
    0xE575D44A45DAULL,
    0xEB54DCF6DDD9ULL,
    0xED5452A48957ULL,
    0xF3554AD8C96CULL,
    0xF5554CE021BCULL,
    0xFB7ECC6B9D2EULL,
    0xFD7ECD6A6DD8ULL,
    0x03155B91EDA1ULL,
    0x0535D0B9ED24ULL,
    0x0B3DDB2D1949ULL,
    0x0D1D5F2DFD0FULL,
    0x1375422C65FDULL,
    0x1555462DC972ULL,
    0x1B5FCF278DB1ULL,
    0x1D7FEE264973ULL,
    0x2315425A7DC3ULL,
    0x2535CE7299A5ULL,
    0x2B34CA7E053EULL,
    0x2D14412C5195ULL,
    0x33755A682DC9ULL,
    0x35555C6AB177ULL,
    0x3B1D4D016997ULL,
    0x3D1CC629E9B3ULL,
    0x4314480121ABULL,
    0x4514C26B6D01ULL,
    0x4B1CD8CD39EFULL,
    0x4D1C5DADF9CCULL,
    0x535450A82942ULL,
    0x55547CADA9EDULL,
    0x5B5CC6221D6AULL,
    0x5D5CEF2BD988ULL,
    0x631450523DDCULL,
    0x6514D476B1F6ULL,
    0x6B15CA8949E0ULL,
    0x6D1460ABE9CFULL,
    0x735448FC2522ULL,
    0x75544EFCB1B6ULL,
    0x7B5CDE6FC9CBULL,
    0x7D5CDF7F2D38ULL,
    0x835C5C359DBFULL,
    0x855C71250910ULL,
    0x8B54DD7A3DD1ULL,
    0x8D54DA32A104ULL,
    0x931D45890DDBULL,
    0x951CCBB1CDAAULL,
    0x9B14CF0DD900ULL,
    0x9D144727D9A4ULL,
    0xA35D4E680945ULL,
    0xA55C4061E9A5ULL,
    0xAB54CD7A01DDULL,
    0xAD54C8E3ADEBULL,
    0xB31C5C4BF9D5ULL,
    0xB51CFD6BB912ULL,
    0xBB1CDE4D8121ULL,
    0xBD1C79250960ULL,
    0xC31550138D8CULL,
    0xC51456299DA7ULL,
    0xCB1CD38DF1ECULL,
    0xCD1CD2AD556FULL,
    0xD354DBAFA9CEULL,
    0xD554D7BD7D02ULL,
    0xDB5C5E706D50ULL,
    0xDD5C7F39E1B2ULL,
    0xE3144E40BDD0ULL,
    0xE5144E6B3D7DULL,
    0xEB14CFCFE982ULL,
    0xED14C9AC4149ULL,
    0xF355CAEAADBFULL,
    0xF554C5E86D75ULL,
    0xFB5C4C6DC5D7ULL,
    0xFD5C4D7D6560ULL,
    0x035CD4CAE137ULL,
    0x055C5EE02151ULL,
    0x0B74484805EDULL,
    0x0D74C0412D42ULL,
    0x137CD6471D74ULL,
    0x157CF375D5ADULL,
    0x1B544A08D14BULL,
    0x1D54632131ADULL,
    0x237CC64D692AULL,
    0x257C4E4E4D83ULL,
    0x2B5D565A4DA0ULL,
    0x2D5CF14185D3ULL,
    0x335CCE1FD1F8ULL,
    0x355CC9245142ULL,
    0x3B74528CB5B3ULL,
    0x3D74533D15A5ULL,
    0x437C42DD7542ULL,
    0x457C46E8B9B9ULL,
    0x4B56DF22FD8AULL,
    0x4D56D623750CULL,
    0x535CC541B9A5ULL,
    0x555CC36A81B6ULL,
    0x5B7447C8DDEAULL,
    0x5D7446CA4D7FULL,
    0x635C528DDDAAULL,
    0x655C5DBD7937ULL,
    0x6B76C5369DE4ULL,
    0x6D76CC2F553AULL,
    0x737CC4464953ULL,
    0x757CED62E5F4ULL,
    0x7B5D54C8F5D8ULL,
    0x7D5C52DBC1FEULL,
    0x835D5EDE5198ULL,
    0x857CD8F01D5BULL,
    0x8B74D34A8D7CULL,
    0x8D545F417929ULL,
    0x937CCA421DEDULL,
    0x955C4E63F586ULL,
    0x9B5546683146ULL,
    0x9D74E7239923ULL,
    0xA35C4C5BE996ULL,
    0xA57CC4721DE5ULL,
    0xAB7DC15269BBULL,
    0xAD5C4A48D9BDULL,
    0xB37DDA0DF57BULL,
    0xB55C5426111DULL,
    0xBB545CAC7DF5ULL,
    0xBD74DD36DD68ULL,
    0xC37CC6C9B196ULL,
    0xC55CEAE8515DULL,
    0xCB574342D5DFULL,
    0xCD764A2A511DULL,
    0xD35DCD550180ULL,
    0xD57C4A5EB59FULL,
    0xDB7440C81DADULL,
    0xDD54CDC95943ULL,
    0xE37DDC8CF1A8ULL,
    0xE55CD2AD5D0CULL,
    0xEB565B1E5D32ULL,
    0xED765A2FF58CULL,
    0xF35CCE5A05A9ULL,
    0xF57C6A72696FULL,
    0xFB7D5DC835B1ULL,
    0xFD5CFFDB5547ULL,
    0x031CDC4BD5CBULL,
    0x051C52699125ULL,
    0x0B145E475DCDULL,
    0x0D14DE6D1925ULL,
    0x135DD87E651BULL,
    0x155CDD75F98EULL,
    0x1B5454641DF4ULL,
    0x1D5477AD8D88ULL,
    0x231DC4C1913CULL,
    0x251C6CE1D5F7ULL,
    0x2B1C4E5DF94BULL,
    0x2D1CE17DD5EEULL,
    0x335CCA2BB9A1ULL,
    0x355CCD716122ULL,
    0x3B544C2355C3ULL,
    0x3D544D30BD3FULL,
    0x435556F5D5F6ULL,
    0x45547AF53139ULL,
    0x4B5CD36EEDD1ULL,
    0x4D5CDA2E6516ULL,
    0x5314CD41B1ECULL,
    0x5514C7617908ULL,
    0x5B1C4BC5D58AULL,
    0x5D1C4AE75D27ULL,
    0x63544EB1EDB3ULL,
    0x655449E00907ULL,
    0x6B5CC9322D77ULL,
    0x6D5CC82291DCULL,
    0x7315C642B1C5ULL,
    0x7514C76299CBULL,
    0x7B145ED4F586ULL,
    0x7D1450F65900ULL,
    0x831C465D5143ULL,
    0x851CC87919A7ULL,
    0x8B14D94375E6ULL,
    0x8D145569F9CAULL,
    0x935CDE686D82ULL,
    0x955C5263E58BULL,
    0x9B54482189B9ULL,
    0x9D54CBAA152BULL,
    0xA31C5ED575D7ULL,
    0xA51CDAF5BDBDULL,
    0xAB1CCD4BF95EULL,
    0xAD1C6E69D9F9ULL,
    0xB35CCE2635A4ULL,
    0xB55C4027B1A9ULL,
    0xBB555A3C253BULL,
    0xBD54DB26C547ULL,
    0xC354C8E0B59CULL,
    0xC554E4E1292BULL,
    0xCB5C5F2B3540ULL,
    0xCD5C7E2AF5A6ULL,
    0xD314C55535F4ULL,
    0xD51442757D1DULL,
    0xDB1C50C135F5ULL,
    0xDD1CD5E37110ULL,
    0xE354D8A5E1BDULL,
    0xE554D6AE1D42ULL,
    0xEB5D4F26F5BCULL,
    0xED5C4E372D73ULL,
    0xF314CC52DD32ULL,
    0xF5146C73B9D1ULL,
    0xFB144BC035C7ULL,
    0xFD14C9E0194FULL,
};

static uint8_t audi_v1_extract_counter(uint64_t hop, uint8_t btn_byte);

static uint8_t audi_v1_get_btn_code(uint8_t original_btn_id);

static uint64_t audi_v1_roundtrip(uint64_t actual_hop, uint8_t ctr_from, uint8_t ctr_to);

static void subghz_protocol_encoder_audi_v1_get_upload(SubGhzProtocolEncoderAudiV1* instance);

static uint8_t audi_v1_extract_counter(uint64_t hop, uint8_t btn_byte) {
    uint8_t hop_bits = (hop >> 42) & 0x3F;
    uint8_t btn_lo = btn_byte & 0x03;
    return hop_bits | (btn_lo << 6);
}

static uint8_t audi_v1_get_btn_code(uint8_t original_btn_id) {
    uint8_t custom_btn = subghz_custom_btn_get();

    switch(custom_btn) {
    case SUBGHZ_CUSTOM_BTN_OK:
        return original_btn_id;
    case SUBGHZ_CUSTOM_BTN_UP:
        return AUDI_V1_BTN_UNLOCK;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return AUDI_V1_BTN_LOCK;
    default:
        return original_btn_id;
    }
}

static uint64_t audi_v1_roundtrip(uint64_t actual_hop, uint8_t ctr_from, uint8_t ctr_to) {
    uint64_t mask = AUDI_V1_HOP_PREDICT[ctr_from] ^ AUDI_V1_HOP_PREDICT[ctr_to];
    return (actual_hop ^ mask) & 0xFFFFFFFFFFFFULL;
}

static void subghz_protocol_encoder_audi_v1_get_upload(SubGhzProtocolEncoderAudiV1* instance) {
    furi_assert(instance);
    size_t index = 0;

    uint8_t original_btn_id = (instance->btn_byte >> 4) & 0x0F;
    uint8_t target_btn_id = original_btn_id;
    if(!subghz_block_generic_global_button_override_get(&target_btn_id)) {
        target_btn_id = audi_v1_get_btn_code(original_btn_id);
    }

    uint8_t current_ctr = audi_v1_extract_counter(instance->hop, instance->btn_byte);

    uint32_t cnt32 = current_ctr;
    if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {
        if(!subghz_block_generic_global_counter_override_get(&cnt32)) {
            int32_t new_cnt = (int32_t)cnt32 + furi_hal_subghz_get_rolling_counter_mult();
            cnt32 = (uint32_t)(new_cnt & 0xFF);
        }
    } else {
        cnt32 = (cnt32 - 1) & 0xFF;
    }
    uint8_t next_ctr = cnt32 & 0xFF;

    uint64_t new_hop = audi_v1_roundtrip(instance->hop, current_ctr, next_ctr);

    uint8_t btn_lo_preserved = instance->btn_byte & 0x0C;
    uint8_t btn_lo_counter = (next_ctr >> 6) & 0x03;
    uint8_t new_btn_byte = (target_btn_id << 4) | ((btn_lo_preserved | btn_lo_counter) & 0x0F);

    FURI_LOG_I(TAG, "Roundtrip: ctr %d->%d, btn 0x%02X", current_ctr, next_ctr, new_btn_byte);

    instance->hop = new_hop;
    instance->btn_byte = new_btn_byte;
    instance->generic.cnt = next_ctr;
    instance->generic.btn = target_btn_id;
    instance->generic.serial = instance->serial;

    instance->generic.data = ((uint64_t)instance->serial << 32) | ((uint64_t)new_btn_byte << 24) |
                             ((new_hop >> 24) & 0xFFFFFF);
    instance->generic.data_count_bit = AUDI_V1_DATA_BITS;

    instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)AUDI_V1_TE_SYNC);

    instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)AUDI_V1_TE_HEADER);

    for(int p = 0; p < 15; p++) {
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_audi_v1_const.te_short);
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_audi_v1_const.te_short);
    }

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_audi_v1_const.te_long);

    uint8_t manchester_state = ManchesterStateMid1;

    for(int i = 87; i >= 0; i--) {
        bool b;
        if(i >= 24) {

            b = (instance->generic.data >> (i - 24)) & 1;
        } else {

            b = (new_hop >> i) & 1;
        }

        switch(manchester_state) {
        case ManchesterStateMid1:
            if(b == 0) {
                instance->encoder.upload[index++] =
                    level_duration_make(true, (uint32_t)subghz_protocol_audi_v1_const.te_long);
                manchester_state = ManchesterStateMid0;
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(true, (uint32_t)subghz_protocol_audi_v1_const.te_short);
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_audi_v1_const.te_short);
                manchester_state = ManchesterStateMid1;
            }
            break;
        case ManchesterStateMid0:
            if(b == 0) {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_audi_v1_const.te_short);
                instance->encoder.upload[index++] =
                    level_duration_make(true, (uint32_t)subghz_protocol_audi_v1_const.te_short);
                manchester_state = ManchesterStateMid0;
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_audi_v1_const.te_long);
                manchester_state = ManchesterStateMid1;
            }
            break;
        default:
            manchester_state = ManchesterStateMid1;
            break;
        }
    }

    bool end_level = (manchester_state == ManchesterStateMid1);
    instance->encoder.upload[index++] = level_duration_make(end_level, (uint32_t)6000);

    instance->encoder.size_upload = index;
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

void* subghz_protocol_encoder_audi_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderAudiV1* instance = malloc(sizeof(SubGhzProtocolEncoderAudiV1));

    instance->base.protocol = &subghz_protocol_audi_v1;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_audi_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderAudiV1* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(subghz_block_generic_deserialize(&instance->generic, flipper_format) !=
           SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        if(instance->generic.data_count_bit != AUDI_V1_DATA_BITS) {
            FURI_LOG_E(
                TAG, "Invalid bit count: %lu", (unsigned long)instance->generic.data_count_bit);
            res = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }

        uint8_t hop_data[6] = {0};
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(flipper_format_read_hex(flipper_format, "Hop", hop_data, 6)) {
            instance->hop = 0;
            for(size_t i = 0; i < 6; i++) {
                instance->hop = (instance->hop << 8) | hop_data[i];
            }
        } else {

            FURI_LOG_W(TAG, "Hop field missing, reconstructing from Key");
            instance->hop = (instance->generic.data & 0xFFFFFFULL) << 24;
        }

        instance->btn_byte = (instance->generic.data >> 24) & 0xFF;
        instance->serial = (uint32_t)(instance->generic.data >> 32);

        uint8_t original_btn_id = (instance->btn_byte >> 4) & 0x0F;
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(original_btn_id);
        }
        subghz_custom_btn_set_max(2);
        subghz_block_generic_global.btn_length_bit = 4;

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_encoder_audi_v1_get_upload(instance);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Key");
            break;
        }

        uint8_t new_hop_data[6];
        for(size_t i = 0; i < 6; i++) {
            new_hop_data[5 - i] = (instance->hop >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Hop", new_hop_data, 6)) {
            FURI_LOG_W(TAG, "Unable to update Hop (may not exist yet)");
        }

        instance->encoder.is_running = true;
        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_audi_v1_vt = {
    .alloc = subghz_protocol_encoder_audi_v1_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_audi_v1_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_audi_v1 = {
    .name = SUBGHZ_PROTOCOL_AUDI_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_audi_v1_vt,
};

static const SubGhzEncodePluginEntry enc_audi_v1_entry = {.name = SUBGHZ_PROTOCOL_AUDI_V1_NAME, .encoder = &enc_audi_v1_vt};
static const FlipperAppPluginDescriptor enc_audi_v1_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_audi_v1_entry};
const FlipperAppPluginDescriptor* enc_audi_v1_ep(void) { return &enc_audi_v1_descriptor; }
