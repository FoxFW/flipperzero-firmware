/* decode_g1.fal : SD-resident decode/format for a batch of generic-only Sub-GHz
 * protocols whose get_string reads only the shared SubGhzBlockGeneric capture
 * block. Loaded on demand by helpers/subghz_decode_plugin_loader.c and dispatched
 * by protocol name; the internal decoders point vtable .get_string at the shared
 * subghz_protocol_decoder_common_get_string wrapper (lib common.c), which passes
 * &generic with no extras. FoxFW2.0 2026-10-21 split detect/decode rollout. */

#include "subghz_decode_plugin_desc.h"

#include <lib/subghz/blocks/generic.h>
#include <furi.h>

static void dg1_mitsubishi_v0(const SubGhzDecodeInput* input, FuriString* output) {
    const SubGhzBlockGeneric* generic = input->generic;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Sn:%08lX Cnt:%04lX\r\n"
        "Btn:%02X\r\n",
        generic->protocol_name,
        generic->data_count_bit,
        generic->serial,
        generic->cnt,
        generic->btn);
}

static void dg1_nissan_v0(const SubGhzDecodeInput* input, FuriString* output) {
    const SubGhzBlockGeneric* generic = input->generic;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "(structural lock)\r\n",
        generic->protocol_name,
        generic->data_count_bit,
        (unsigned long)(generic->data >> 32),
        (unsigned long)(generic->data & 0xFFFFFFFFU));
}

static void dg1_opel_v0(const SubGhzDecodeInput* input, FuriString* output) {
    const SubGhzBlockGeneric* generic = input->generic;
    uint16_t bits = generic->data_count_bit;
    if(bits <= 64) {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%016llX\r\n"
            "Hop:%016llX",
            generic->protocol_name,
            bits,
            generic->data,
            generic->data);
    } else {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%016llX\r\n"
            "Key2:%08lX\r\n"
            "Hop:%016llX%08lX",
            generic->protocol_name,
            bits,
            generic->data,
            (uint32_t)(generic->seed),
            generic->data,
            (uint32_t)(generic->seed));
    }
}

static void dg1_porsche_touareg(const SubGhzDecodeInput* input, FuriString* output) {
    const SubGhzBlockGeneric* generic = input->generic;
    uint8_t frame_type = (uint8_t)((generic->data >> 56) & 0x07);
    const char* frame_type_name = "??";
    if(frame_type == 0x02) {
        frame_type_name = "First";
    } else if(frame_type == 0x01) {
        frame_type_name = "Cont";
    } else if(frame_type == 0x04) {
        frame_type_name = "Final";
    }
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Sn:%06lX Btn:%X\r\n"
        "Cnt:%04lX FT:%s\r\n"
        "Raw:%08lX%08lX\r\n",
        generic->protocol_name,
        generic->data_count_bit,
        (unsigned long)(generic->serial & 0xFFFFFF),
        (unsigned int)generic->btn,
        (unsigned long)generic->cnt,
        frame_type_name,
        (unsigned long)(generic->data >> 32),
        (unsigned long)(generic->data & 0xFFFFFFFF));
}

static const SubGhzDecodeEntry decode_g1_items[] = {
    {.name = "Mitsubishi V0", .get_string = dg1_mitsubishi_v0},
    {.name = "Nissan V0", .get_string = dg1_nissan_v0},
    {.name = "Opel V0", .get_string = dg1_opel_v0},
    {.name = "Porsche Touareg", .get_string = dg1_porsche_touareg},
};

static const SubGhzDecodeRegistry decode_g1_registry = {
    .items = decode_g1_items,
    .size = COUNT_OF(decode_g1_items),
};

static const FlipperAppPluginDescriptor decode_g1_descriptor = {
    .appid = SUBGHZ_DECODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION,
    .entry_point = &decode_g1_registry,
};

const FlipperAppPluginDescriptor* decode_g1_ep(void) {
    return &decode_g1_descriptor;
}
