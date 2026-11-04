/* enc_x10.fal : SD encoder for x10 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_x10 enc_x10__subghz_protocol_x10
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/x10.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

static const SubGhzProtocolEncoder enc_x10_vt = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_x10 = {
    .name = SUBGHZ_PROTOCOL_X10_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315  | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable,
    .decoder = NULL,
    .encoder = &enc_x10_vt,
};

static const SubGhzEncodePluginEntry enc_x10_entry = {.name = SUBGHZ_PROTOCOL_X10_NAME, .encoder = &enc_x10_vt};
static const FlipperAppPluginDescriptor enc_x10_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_x10_entry};
const FlipperAppPluginDescriptor* enc_x10_ep(void) { return &enc_x10_descriptor; }
