#include "decode_plugin.h"

#include <furi.h>

static SubGhzDecodeLoaderFn subghz_decode_loader = NULL;

void subghz_decode_plugin_set_loader(SubGhzDecodeLoaderFn loader) {
    subghz_decode_loader = loader;
}

bool subghz_decode_plugin_get_string(
    const char* fal_base,
    const SubGhzDecodeInput* input,
    FuriString* output) {
    if(subghz_decode_loader && subghz_decode_loader(fal_base, input, output)) {
        return true;
    }

    if(input && input->generic) {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:0x%08lX\r\n"
            "Decode on SD unavailable\r\n",
            input->generic->protocol_name ? input->generic->protocol_name : "Unknown",
            input->generic->data_count_bit,
            (uint32_t)(input->generic->data & 0xFFFFFFFF));
    }
    return false;
}
