#pragma once

#include "base.h"
#include "renault_v1_crypto.h" // RenaultV1BfContext, used by the bf hooks declared below

#define SUBGHZ_PROTOCOL_RENAULT_V1_NAME "Hitag-2"

typedef struct SubGhzProtocolDecoderRenaultV1 SubGhzProtocolDecoderRenaultV1;
typedef struct SubGhzProtocolEncoderRenaultV1 SubGhzProtocolEncoderRenaultV1;

extern const SubGhzProtocolDecoder subghz_protocol_renault_v1_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_renault_v1_encoder;
extern const SubGhzProtocol subghz_protocol_renault_v1;

void* subghz_protocol_encoder_renault_v1_alloc(SubGhzEnvironment* environment);

SubGhzProtocolStatus
    subghz_protocol_encoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Allocate SubGhzProtocolDecoderRenaultV1.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolDecoderRenaultV1* pointer to a SubGhzProtocolDecoderRenaultV1 instance
 */
void* subghz_protocol_decoder_renault_v1_alloc(SubGhzEnvironment* environment);

/**
 * Reset decoder SubGhzProtocolDecoderRenaultV1.
 * @param context Pointer to a SubGhzProtocolDecoderRenaultV1 instance
 */
void subghz_protocol_decoder_renault_v1_reset(void* context);

/**
 * Parse a raw sequence of levels and durations received from the air.
 * @param context Pointer to a SubGhzProtocolDecoderRenaultV1 instance
 * @param level Signal level true-high false-low
 * @param duration Duration of this level in, us
 */
void subghz_protocol_decoder_renault_v1_feed(void* context, bool level, uint32_t duration);

/**
 * Getting the hash sum of the last randomly received parcel.
 * @param context Pointer to a SubGhzProtocolDecoderRenaultV1 instance
 * @return hash Hash sum
 */
uint8_t subghz_protocol_decoder_renault_v1_get_hash_data(void* context);

/**
 * Serialize data SubGhzProtocolDecoderRenaultV1.
 * @param context Pointer to a SubGhzProtocolDecoderRenaultV1 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param preset The modulation on which the signal was received, SubGhzRadioPreset
 * @return true On success
 */

SubGhzProtocolStatus subghz_protocol_decoder_renault_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserialize data SubGhzProtocolDecoderRenaultV1.
 * @param context Pointer to a SubGhzProtocolDecoderRenaultV1 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return true On success
 */

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Getting a textual representation of the received data.
 * @param context Pointer to a SubGhzProtocolDecoderRenaultV1 instance
 * @param output Resulting text
 */
void subghz_protocol_decoder_renault_v1_get_string(void* context, FuriString* output);

/* Bruteforce hooks -- implemented in renault_v1.c because they touch the file-local decoder struct
 * (the protocol-agnostic cipher lives in renault_v1_crypto.c). They mirror the PSA bf_init/apply/needs
 * shape so the shared bruteforce scene can drive either protocol. */

/* True when this signal has no recovered counter yet, i.e. the button should offer recovery. Takes the
 * decoder base as void* and checks the protocol identity itself, so a generic caller can hand it any
 * decoder safely. */
bool subghz_protocol_renault_v1_needs_bruteforce(void* decoder_context);

/* Set up the context from the currently-loaded Hitag-2 decoder. */
void subghz_protocol_renault_v1_bf_init(RenaultV1BfContext* ctx, void* decoder_context);

/* Store the recovered IV in the decoder's generic.seed and mark the signal recovered, so serialize()
 * persists it and get_string() can derive the button/counter from it. */
void subghz_protocol_renault_v1_bf_apply(RenaultV1BfContext* ctx, void* decoder_context);

/* Mark this decoder's signal as "brute exhausted, not found" (generic.status = 2). A SESSION fact,
 * not a property of the signal: needs_bruteforce stops offering the button and get_string shows "BF
 * not found", but serialize writes Recovered=(status==1) so a 2 is never stored -- a saved capture
 * must not claim BF-not-found forever. Re-applied on the receiver flow from a per-history flag, since
 * the return re-deserialize wipes the in-RAM status. Self-guards on the protocol identity. */
void subghz_protocol_renault_v1_bf_set_not_found(void* decoder_context);

/* Pack the 4-byte decrypted IV (MSB-first) into a 32-bit word (the generic.seed form). */
uint32_t renault_v1_iv_to_u32(const uint8_t iv[4]);
