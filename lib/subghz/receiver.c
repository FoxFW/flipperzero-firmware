#include "receiver.h"

#include "registry.h"

#include <m-array.h>

#include <core/kernel.h>

typedef struct {
    SubGhzProtocolEncoderBase* base;
    size_t registry_index;
} SubGhzReceiverSlot;

ARRAY_DEF(SubGhzReceiverSlotArray, SubGhzReceiverSlot, M_POD_OPLIST);
#define M_OPL_SubGhzReceiverSlotArray_t() ARRAY_OPLIST(SubGhzReceiverSlotArray, M_POD_OPLIST)

struct SubGhzReceiver {
    SubGhzReceiverSlotArray_t slots;
    SubGhzProtocolFlag filter;
    SubGhzProtocolFlag modulation_filter;

    SubGhzReceiverCallback callback;
    void* context;

    SubGhzReceiverProtocolEnabledCallback protocol_enabled_callback;
    void* protocol_enabled_context;
};

SubGhzReceiver* subghz_receiver_alloc_init(SubGhzEnvironment* environment) {
    return subghz_receiver_alloc_init_ex(environment, 0, 0);
}

SubGhzReceiver* subghz_receiver_alloc_init_ex(
    SubGhzEnvironment* environment,
    SubGhzProtocolFlag modulation,
    SubGhzProtocolFlag exclude_flags) {
    SubGhzReceiver* instance = malloc(sizeof(SubGhzReceiver));
    SubGhzReceiverSlotArray_init(instance->slots);
    const SubGhzProtocolRegistry* protocol_registry_items =
        subghz_environment_get_protocol_registry(environment);

    for(size_t i = 0; i < subghz_protocol_registry_count(protocol_registry_items); ++i) {
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(protocol_registry_items, i);

        const SubGhzProtocolFlag protocol_modulation =
            protocol->flag & (SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM);
        if(exclude_flags && (protocol->flag & exclude_flags) != 0) continue;
        if(modulation && protocol_modulation && (protocol_modulation & modulation) == 0) continue;

        if(protocol->decoder && protocol->decoder->alloc) {
            SubGhzReceiverSlot* slot = SubGhzReceiverSlotArray_push_new(instance->slots);
            slot->base = protocol->decoder->alloc(environment);
            slot->registry_index = i;
        }
    }

    instance->callback = NULL;
    instance->context = NULL;
    instance->protocol_enabled_callback = NULL;
    instance->protocol_enabled_context = NULL;
    instance->modulation_filter = modulation;
    return instance;
}

void subghz_receiver_free(SubGhzReceiver* instance) {
    furi_check(instance);

    instance->callback = NULL;
    instance->context = NULL;

    furi_kernel_lock();
    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            slot->base->protocol->decoder->free(slot->base);
            slot->base = NULL;
        }
    SubGhzReceiverSlotArray_clear(instance->slots);

    free(instance);
    furi_kernel_unlock();
}

void subghz_receiver_decode(SubGhzReceiver* instance, bool level, uint32_t duration) {
    furi_check(instance);
    furi_check(instance->slots);

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            const SubGhzProtocolFlag protocol_flag = slot->base->protocol->flag;
            const SubGhzProtocolFlag protocol_modulation =
                protocol_flag & (SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM);
            if((protocol_flag & instance->filter) != 0 &&
               (instance->modulation_filter == 0 || protocol_modulation == 0 ||
                (protocol_modulation & instance->modulation_filter) != 0) &&
               (!instance->protocol_enabled_callback ||
                instance->protocol_enabled_callback(
                    instance->protocol_enabled_context,
                    slot->registry_index,
                    slot->base->protocol->name))) {
                slot->base->protocol->decoder->feed(slot->base, level, duration);
            }
        }
}

void subghz_receiver_reset(SubGhzReceiver* instance) {
    furi_check(instance);
    furi_check(instance->slots);

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            slot->base->protocol->decoder->reset(slot->base);
        }
}

// Reset every decoder EXCEPT the one named `keep_name`. Used when a generic
// decoder fires on a signal that a more-specific superset decoder is still
// accumulating: the normal post-fire subghz_receiver_reset() would wipe the
// superset's in-progress frame before it can complete, so the generic core
// always wins and the specific protocol never surfaces (e.g. the 88-bit
// "Hitag-2" core completing first and clobbering the 102-bit "Fiat V1" frame
// of the same burst). Keeping only the named superset alive lets it finish
// while every other decoder is cleared exactly as a full reset would clear it,
// so no additional cross-fire is unmasked. keep_name NULL falls back to a full
// reset.
void subghz_receiver_reset_except_by_name(SubGhzReceiver* instance, const char* keep_name) {
    furi_check(instance);
    furi_check(instance->slots);

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            if(keep_name && slot->base->protocol->name &&
               strcmp(slot->base->protocol->name, keep_name) == 0) {
                continue;
            }
            slot->base->protocol->decoder->reset(slot->base);
        }
}

static void subghz_receiver_rx_callback(SubGhzProtocolDecoderBase* decoder_base, void* context) {
    SubGhzReceiver* instance = context;
    if(instance->callback) {
        instance->callback(instance, decoder_base, instance->context);
    }
}

void subghz_receiver_set_rx_callback(
    SubGhzReceiver* instance,
    SubGhzReceiverCallback callback,
    void* context) {
    furi_check(instance);

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            subghz_protocol_decoder_base_set_decoder_callback(
                (SubGhzProtocolDecoderBase*)slot->base, subghz_receiver_rx_callback, instance);
        }

    instance->callback = callback;
    instance->context = context;
}

void subghz_receiver_set_filter(SubGhzReceiver* instance, SubGhzProtocolFlag filter) {
    furi_check(instance);
    instance->filter = filter;
}

void subghz_receiver_set_modulation_filter(
    SubGhzReceiver* instance,
    SubGhzProtocolFlag modulation_filter) {
    furi_check(instance);
    instance->modulation_filter = modulation_filter;
}

void subghz_receiver_set_protocol_enabled_callback(
    SubGhzReceiver* instance,
    SubGhzReceiverProtocolEnabledCallback callback,
    void* context) {
    furi_check(instance);
    instance->protocol_enabled_callback = callback;
    instance->protocol_enabled_context = context;
}

bool subghz_receiver_detach_decoder(SubGhzReceiver* instance, SubGhzProtocolDecoderBase* decoder) {
    furi_check(instance);
    furi_check(decoder);

    size_t size = SubGhzReceiverSlotArray_size(instance->slots);
    for(size_t i = 0; i < size; i++) {
        SubGhzReceiverSlot* slot = SubGhzReceiverSlotArray_get(instance->slots, i);
        if((SubGhzProtocolDecoderBase*)slot->base == decoder) {
            SubGhzReceiverSlotArray_remove_v(instance->slots, i, i + 1);
            subghz_protocol_decoder_base_set_decoder_callback(decoder, NULL, NULL);
            return true;
        }
    }
    return false;
}

bool subghz_receiver_adopt_decoder(SubGhzReceiver* instance, SubGhzProtocolDecoderBase* decoder) {
    furi_check(instance);
    furi_check(decoder);

    size_t size = SubGhzReceiverSlotArray_size(instance->slots);
    for(size_t i = 0; i < size; i++) {
        SubGhzReceiverSlot* slot = SubGhzReceiverSlotArray_get(instance->slots, i);
        if(slot->base->protocol == decoder->protocol) {
            slot->base->protocol->decoder->free(slot->base);
            slot->base = (SubGhzProtocolEncoderBase*)decoder;
            subghz_protocol_decoder_base_set_decoder_callback(
                decoder, subghz_receiver_rx_callback, instance);
            return true;
        }
    }
    return false;
}

SubGhzProtocolDecoderBase* subghz_receiver_search_decoder_base_by_name(
    SubGhzReceiver* instance,
    const char* decoder_name) {
    furi_check(instance);

    SubGhzProtocolDecoderBase* result = NULL;

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            if(strcmp(slot->base->protocol->name, decoder_name) == 0) {
                result = (SubGhzProtocolDecoderBase*)slot->base;
                break;
            }
        }
    return result;
}
