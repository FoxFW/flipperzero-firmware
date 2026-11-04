/* enc_siemens_vdo.fal : SD encoder for siemens_vdo (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_siemens_vdo enc_siemens_vdo__subghz_protocol_siemens_vdo
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/siemens_vdo.h>
#include <lib/subghz/protocols/pp_auto_common.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

static const SubGhzProtocolEncoder enc_siemens_vdo_vt = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_siemens_vdo = {
    .name = SIEMENS_VDO_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = NULL,
    .encoder = &enc_siemens_vdo_vt,
};

static const SubGhzEncodePluginEntry enc_siemens_vdo_entry = {.name = SIEMENS_VDO_PROTOCOL_NAME, .encoder = &enc_siemens_vdo_vt};
static const FlipperAppPluginDescriptor enc_siemens_vdo_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_siemens_vdo_entry};
const FlipperAppPluginDescriptor* enc_siemens_vdo_ep(void) { return &enc_siemens_vdo_descriptor; }
