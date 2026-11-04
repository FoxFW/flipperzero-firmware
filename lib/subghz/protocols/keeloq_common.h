#pragma once

#include "base.h"

#include <furi.h>

#define KEELOQ_NLF 0x3A5C742E

#define KEELOQ_LEARNING_UNKNOWN             0u
#define KEELOQ_LEARNING_SIMPLE              1u
#define KEELOQ_LEARNING_NORMAL              2u
#define KEELOQ_LEARNING_SECURE              3u
#define KEELOQ_LEARNING_MAGIC_XOR_TYPE_1    4u
#define KEELOQ_LEARNING_FAAC                5u
#define KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_1 6u
#define KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_2 7u
#define KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_3 8u

#define KEELOQ_LEARNING_SIMPLE_KINGGATES    10u
#define KEELOQ_LEARNING_NORMAL_JAROLIFT     11u
#define KEELOQ_LEARNING_AERF                14u
#define KEELOQ_LEARNING_JCM1G               15u

uint32_t subghz_protocol_keeloq_common_encrypt(const uint32_t data, const uint64_t key);

uint32_t subghz_protocol_keeloq_common_decrypt(const uint32_t data, const uint64_t key);

uint64_t subghz_protocol_keeloq_common_normal_learning(uint32_t data, const uint64_t key);

uint64_t
    subghz_protocol_keeloq_common_secure_learning(uint32_t data, uint32_t seed, const uint64_t key);

uint64_t subghz_protocol_keeloq_common_magic_xor_type1_learning(uint32_t data, uint64_t xor);

uint64_t subghz_protocol_keeloq_common_faac_learning(const uint32_t seed, const uint64_t key);

uint64_t subghz_protocol_keeloq_common_magic_serial_type1_learning(uint32_t data, uint64_t man);

uint64_t subghz_protocol_keeloq_common_magic_serial_type2_learning(uint32_t data, uint64_t man);

uint64_t subghz_protocol_keeloq_common_magic_serial_type3_learning(uint32_t data, uint64_t man);

/* AERF (type 14) device-key derivation + nl_extend-based decrypt (JCM family). */
uint64_t subghz_protocol_keeloq_common_learning_aerf(uint32_t fix, const uint64_t key);
uint32_t subghz_protocol_keeloq_common_decrypt_aerf(uint32_t hop, uint64_t man, uint32_t outer_limit);
