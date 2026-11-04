#pragma once

#include <lib/subghz/protocols/base.h>

#define TESLA_CHARGE_PROTOCOL_NAME "Tesla Charge"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzProtocolDecoderTeslaCharge SubGhzProtocolDecoderTeslaCharge;
typedef struct SubGhzProtocolEncoderTeslaCharge SubGhzProtocolEncoderTeslaCharge;

extern const SubGhzProtocolDecoder subghz_protocol_tesla_charge_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_tesla_charge_encoder;
extern const SubGhzProtocol subghz_protocol_tesla_charge;

/**
 * Allocate SubGhzProtocolEncoderTeslaCharge.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolEncoderTeslaCharge* pointer to a SubGhzProtocolEncoderTeslaCharge instance
 */
void* subghz_protocol_encoder_tesla_charge_alloc(SubGhzEnvironment* environment);

/**
 * Free SubGhzProtocolEncoderTeslaCharge.
 * @param context Pointer to a SubGhzProtocolEncoderTeslaCharge instance
 */
void subghz_protocol_encoder_tesla_charge_free(void* context);

/**
 * Deserialize and generating an upload to send.
 * @param context Pointer to a SubGhzProtocolEncoderTeslaCharge instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
SubGhzProtocolStatus
    subghz_protocol_encoder_tesla_charge_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Forced transmission stop.
 * @param context Pointer to a SubGhzProtocolEncoderTeslaCharge instance
 */
void subghz_protocol_encoder_tesla_charge_stop(void* context);

/**
 * Getting the level and duration of the upload to be loaded into DMA.
 * @param context Pointer to a SubGhzProtocolEncoderTeslaCharge instance
 * @return LevelDuration
 */
LevelDuration subghz_protocol_encoder_tesla_charge_yield(void* context);

/**
 * Allocate SubGhzProtocolDecoderTeslaCharge.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolDecoderTeslaCharge* pointer to a SubGhzProtocolDecoderTeslaCharge instance
 */
void* subghz_protocol_decoder_tesla_charge_alloc(SubGhzEnvironment* environment);

/**
 * Free SubGhzProtocolDecoderTeslaCharge.
 * @param context Pointer to a SubGhzProtocolDecoderTeslaCharge instance
 */
void subghz_protocol_decoder_tesla_charge_free(void* context);

/**
 * Reset decoder SubGhzProtocolDecoderTeslaCharge.
 * @param context Pointer to a SubGhzProtocolDecoderTeslaCharge instance
 */
void subghz_protocol_decoder_tesla_charge_reset(void* context);

/**
 * Parse a raw sequence of levels and durations received from the air.
 * @param context Pointer to a SubGhzProtocolDecoderTeslaCharge instance
 * @param level Signal level true-high false-low
 * @param duration Duration of this level in, us
 */
void subghz_protocol_decoder_tesla_charge_feed(void* context, bool level, uint32_t duration);

/**
 * Getting the hash sum of the last randomly received parcel.
 * @param context Pointer to a SubGhzProtocolDecoderTeslaCharge instance
 * @return hash Hash sum
 */
uint8_t subghz_protocol_decoder_tesla_charge_get_hash_data(void* context);

/**
 * Serialize data SubGhzProtocolDecoderTeslaCharge.
 * @param context Pointer to a SubGhzProtocolDecoderTeslaCharge instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param preset The modulation on which the signal was received, SubGhzRadioPreset
 * @return status
 */
SubGhzProtocolStatus subghz_protocol_decoder_tesla_charge_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserialize data SubGhzProtocolDecoderTeslaCharge.
 * @param context Pointer to a SubGhzProtocolDecoderTeslaCharge instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
SubGhzProtocolStatus
    subghz_protocol_decoder_tesla_charge_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Getting a textual representation of the received data.
 * @param context Pointer to a SubGhzProtocolDecoderTeslaCharge instance
 * @param output Resulting text
 */
void subghz_protocol_decoder_tesla_charge_get_string(void* context, FuriString* output);

#ifdef __cplusplus
}
#endif
