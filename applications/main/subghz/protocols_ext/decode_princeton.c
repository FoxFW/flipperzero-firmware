/* decode_princeton.fal : SD-resident decode/format for the Princeton OOK remote.
 * The always-on capture path (feed/reset/serialize) stays in internal flash in
 * lib/subghz/protocols/princeton.c; this plugin provides only get_string, loaded
 * on demand when a captured Princeton item is opened. It receives the shared
 * captured frame (SubGhzBlockGeneric) plus the protocol scalars te and guard_time
 * via SubGhzDecodeInput.extra, and formats exactly as the former internal
 * get_string did. FoxFW2.0 2026-10-21 split detect/decode PoC. */

#include "subghz_decode_plugin_desc.h"

#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>

static void decode_princeton_check_remote_controller(SubGhzBlockGeneric* instance) {
    if((instance->data & 0xFF) == 0x30 || (instance->data & 0xFF) == 0xC0) {
        instance->serial = instance->data >> 8;
        instance->btn = instance->data & 0xFF;
    } else if((instance->data & 0xFF) == 0x03 || (instance->data & 0xFF) == 0x0C) {
        instance->serial = instance->data >> 8;
        instance->btn = (instance->data & 0xFF) | 0xF0;
    } else {
        instance->serial = instance->data >> 4;
        instance->btn = instance->data & 0xF;
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(4);
}

static void decode_princeton_get_string(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->generic) {
        return;
    }
    SubGhzBlockGeneric* generic = input->generic;
    uint32_t te = (input->extra && input->extra_count > 0) ? (uint32_t)input->extra[0] : 0;
    uint32_t guard_time = (input->extra && input->extra_count > 1) ? (uint32_t)input->extra[1] : 0;

    decode_princeton_check_remote_controller(generic);
    uint32_t data_rev =
        subghz_protocol_blocks_reverse_key(generic->data, generic->data_count_bit);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = generic->btn;

    if(generic->btn == 0x30 || generic->btn == 0xC0 || generic->btn == 0xF3 ||
       generic->btn == 0xFC) {
        subghz_block_generic_global.btn_length_bit = 8;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:0x%08lX\r\n"
            "Yek:0x%08lX\r\n"
            "Sn:0x%05lX Btn:%02X (8b)\r\n"
            "Te:%luus  GT:Te*%lu\r\n",
            generic->protocol_name,
            generic->data_count_bit,
            (uint32_t)(generic->data & 0xFFFFFF),
            data_rev,
            generic->serial,
            (generic->btn == 0xF3 || generic->btn == 0xFC) ? generic->btn & 0xF : generic->btn,
            te,
            guard_time);
    } else {
        subghz_block_generic_global.btn_length_bit = 4;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:0x%08lX\r\n"
            "Yek:0x%08lX\r\n"
            "Sn:0x%05lX Btn:%01X (4b)\r\n"
            "Te:%luus  GT:Te*%lu\r\n",
            generic->protocol_name,
            generic->data_count_bit,
            (uint32_t)(generic->data & 0xFFFFFF),
            data_rev,
            generic->serial,
            generic->btn,
            te,
            guard_time);
    }
}

static const SubGhzDecodeEntry decode_princeton_items[] = {
    {.name = "Princeton", .get_string = decode_princeton_get_string},
};

static const SubGhzDecodeRegistry decode_princeton_registry = {
    .items = decode_princeton_items,
    .size = 1,
};

static const FlipperAppPluginDescriptor decode_princeton_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_princeton_registry,
};

const FlipperAppPluginDescriptor* decode_princeton_ep(void) {
    return &decode_princeton_descriptor;
}
