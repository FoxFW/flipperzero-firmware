/* decode_renault_v1.fal : SD get_string for renault_v1 (auto-split v5). FoxFW2.0 2026-10-21. */
#include "subghz_decode_plugin_desc.h"
#include <string.h>
#include <lib/subghz/protocols/renault_v1.h>
#include <lib/subghz/protocols/renault_v1_crypto.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>

#define RENAULT_V1_CUSTOM_BTN_MAX  2 // D-pad custom buttons: this fob has only Lock(0x1) + Unlock(0x2)
#define RENAULT_V1_COUNTER_BITS    16 // rolling counter (push_clock) width, carved from the plaintext IV

struct SubGhzProtocolDecoderRenaultV1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;
    /* Inverted 16-bit Hitag-2 sync word, captured at RENAULT_V1_SYNC_BIT_COUNT and then used
     * as a frame gate and as hash input. It used to squat on SubGhzBlockGeneric.disc, whose
     * own comment describes a KeeLoq 12-bit discriminator -- one shared field carrying two
     * unrelated meanings, with only one of them documented. Decoder-local, never serialized. */
    uint16_t sync;
    uint16_t te_hi; // running 1TE estimate for HIGH pulses, reseeded each frame
    uint16_t te_lo; // running 1TE estimate for LOW pulses, reseeded each frame
};

static const uint8_t renault_v1_btns[] = {0x0, 0x1, 0x2, 0x4, 0x8};

static const uint8_t renault_v1_btn_map[RENAULT_V1_CUSTOM_BTN_MAX][5] = {
    {0x1, 0x1, 0x1, 0x1, 0x1}, // custom_id=1 (UP)   -> Lock
    {0x2, 0x2, 0x2, 0x2, 0x2}, // custom_id=2 (DOWN) -> Unlock
};

static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance);
static const char* renault_v1_get_name_button(uint8_t btn);

static void subghz_protocol_renault_v1_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (uint32_t)(instance->data >> 32);
    instance->btn = (uint8_t)((instance->data >> 28) & 0x0F);
    /* Offer the D-pad button swap ONLY once recovered: on an un-recovered signal the encoder can only
     * replay the captured payload (it cannot re-encrypt a different button), so a swap would send the
     * original button while the on-screen label shows the chosen one. set_max(0) hides the D-pad then.
     * status==1 only: a status of 2 (BF not found) is NOT a recovery, so it must not unlock the swap. */
    subghz_custom_btn_set_max((instance->status == 1) ? RENAULT_V1_CUSTOM_BTN_MAX : 0);
    /* The rolling counter is NOT in the raw ciphertext -- it is a plaintext field of the DECRYPTED IV
     * that recovery leaves in generic.seed, so it exists only once status is set. Host-checked over 44
     * consecutive presses of two fobs, this 16-bit field steps by exactly 1 per press (fob1 2A7A..2AA5,
     * fob3 1208..1233) while the fixed part holds. Deriving it here, from the one stored Seed, keeps
     * every display/serialize/deserialize path consistent and means the counter is never written to file.
     * Layout of the decrypted IV: iv0 low nibble = button, iv0 high nibble | iv1 | iv2 low nibble =
     * counter, iv2 high nibble | iv3 = fixed. status==1 only -- status 2 (BF not found) has no IV. */
    if(instance->status == 1) {
        const uint32_t iv = instance->seed;
        instance->cnt =
            ((iv >> 28) & 0x0F) | (((iv >> 16) & 0xFF) << 4) | (((iv >> 8) & 0x0F) << 12);
    } else {
        instance->cnt = 0;
    }
}

static const char* renault_v1_get_name_button(uint8_t btn) {
    switch(btn) {
    case 0x0:
        return "Sync";
    case 0x1:
        return "Lock";
    case 0x2:
        return "Unlock";
    case 0x4:
        return "Trunk";
    case 0x8:
        return "Panic";
    default:
        return "??";
    }
}

static void renault_v1_decode(const SubGhzDecodeInput* input, FuriString* output) {
    if(!input || !input->decoder) return;
    SubGhzProtocolDecoderRenaultV1* instance = input->decoder;


    subghz_protocol_renault_v1_check_remote_controller(&instance->generic);

    /* Publish the 16-bit rolling counter (max 0xFFFF) only once recovered -- before that cnt is 0 and
     * would just expose a meaningless editable value; the width must match or the editor lets it
     * overflow. check_remote_controller (called just above) derived cnt from the Seed. status==1 only:
     * a status of 2 (BF not found) is not a recovery and has no counter. */
    if(instance->generic.status == 1) {
        subghz_block_generic_global_publish_cnt(instance->generic.cnt, RENAULT_V1_COUNTER_BITS);
    }

    subghz_block_generic_global_publish_btn(instance->generic.btn, 4);
    subghz_custom_btn_fill_labels(
        instance->generic.btn, renault_v1_btns, (const uint8_t*)renault_v1_btn_map,
        COUNT_OF(renault_v1_btns),
        (instance->generic.status == 1) ? RENAULT_V1_CUSTOM_BTN_MAX : 0, renault_v1_get_name_button);
    //

    furi_string_cat_printf(
        output,
        "%s\r\n"
        "K1:%016llX\r\n"
        "K2:%06llX Sn:%08lX\r\n"
        "Btn:%02X [%s] %db",
        instance->generic.protocol_name,
        instance->generic.data,
        instance->generic.data_2,
        instance->generic.serial,
        instance->generic.btn,
        renault_v1_get_name_button(instance->generic.btn),
        instance->generic.data_count_bit);

    /* Status line, at parity with PSA: recovered (1) shows the plaintext IV + the counter derived
     * from it; an exhausted brute (2) shows "BF not found"; an un-recovered signal (0) shows
     * "Encrypted (BF needed)". The 2 is a SESSION fact re-applied from the history flag, never
     * serialized (see the serialize clamp), so a saved capture never claims BF-not-found forever. */
    if(instance->generic.status == 1) {
        /* IV is the full 32-bit secret (8 hex); Cnt is the real 16-bit rolling counter carved out of
         * it (max 0xFFFF, 4 hex), NOT the brute round. Both zero-padded hex, no 0x -- per the SubGhz
         * display convention. */
        furi_string_cat_printf(
            output, "\r\nIV:%08lX Cnt:%04lX", instance->generic.seed, instance->generic.cnt);
    } else if(instance->generic.status == 2) {
        furi_string_cat_str(output, "\r\nBF not found");
    } else {
        furi_string_cat_str(output, "\r\nEncrypted (BF needed)");
    }
}

static const SubGhzDecodeEntry renault_v1_items[] = {{.name = "Hitag-2", .get_string = renault_v1_decode}};
static const SubGhzDecodeRegistry renault_v1_registry = {.items = renault_v1_items, .size = 1};
static const FlipperAppPluginDescriptor decode_renault_v1_descriptor = {.appid = SUBGHZ_DECODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_DECODE_PLUGIN_API_VERSION, .entry_point = &renault_v1_registry};
const FlipperAppPluginDescriptor* decode_renault_v1_ep(void) { return &decode_renault_v1_descriptor; }
