/* enc_hitag2.fal : SD encoder for hitag2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_hitag2 enc_hitag2__subghz_protocol_hitag2
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/hitag2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/protocols/common.h>

static const SubGhzProtocolEncoder enc_hitag2_vt = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_hitag2 = {
    .name = SUBGHZ_PROTOCOL_HITAG2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = NULL,
    .encoder = &enc_hitag2_vt,
};

static const SubGhzEncodePluginEntry enc_hitag2_entry = {.name = SUBGHZ_PROTOCOL_HITAG2_NAME, .encoder = &enc_hitag2_vt};
static const FlipperAppPluginDescriptor enc_hitag2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_hitag2_entry};
const FlipperAppPluginDescriptor* enc_hitag2_ep(void) { return &enc_hitag2_descriptor; }
