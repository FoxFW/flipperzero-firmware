#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM4x05_GET_BITRATE(x)        ((((x) & 0x3F) * 2) + 2)

#define EM4x05_SET_BITRATE(x)        (((x) - 2) / 2)
#define EM4x05_MODULATION_NRZ        (0x00000000)
#define EM4x05_MODULATION_MANCHESTER (0x00000040)
#define EM4x05_MODULATION_BIPHASE    (0x00000080)
#define EM4x05_MODULATION_MILLER     (0x000000C0)
#define EM4x05_MODULATION_PSK1       (0x00000100)
#define EM4x05_MODULATION_PSK2       (0x00000140)
#define EM4x05_MODULATION_PSK3       (0x00000180)
#define EM4x05_MODULATION_FSK1       (0x00000200)
#define EM4x05_MODULATION_FSK2       (0x00000240)
#define EM4x05_PSK_RF_2              (0)
#define EM4x05_PSK_RF_4              (0x00000400)
#define EM4x05_PSK_RF_8              (0x00000800)
#define EM4x05_MAXBLOCK_SHIFT        (14)
#define EM4x05_FIRST_USER_BLOCK      (5)
#define EM4x05_SET_NUM_BLOCKS(x) \
    (((x) + 4) << 14)
#define EM4x05_GET_NUM_BLOCKS(x)  ((((x) >> 14) & 0xF) - 4)
#define EM4x05_READ_LOGIN_REQ     (1 << 18)
#define EM4x05_READ_HK_LOGIN_REQ  (1 << 19)
#define EM4x05_WRITE_LOGIN_REQ    (1 << 20)
#define EM4x05_WRITE_HK_LOGIN_REQ (1 << 21)
#define EM4x05_READ_AFTER_WRITE   (1 << 22)
#define EM4x05_DISABLE_ALLOWED    (1 << 23)
#define EM4x05_READER_TALK_FIRST  (1 << 24)
#define EM4x05_INVERT             (1 << 25)
#define EM4x05_PIGEON             (1 << 26)

#define EM4x05_WORD_COUNT (16)

#define EM4x05_OPCODE_LOGIN   (0b001)
#define EM4x05_OPCODE_WRITE   (0b010)
#define EM4x05_OPCODE_READ    (0b100)
#define EM4x05_OPCODE_PROTECT (0b110)
#define EM4x05_OPCODE_DISABLE (0b101)

typedef struct {
    uint32_t word[EM4x05_WORD_COUNT];
    uint16_t mask;
} LFRFIDEM4305;

void em4305_write(LFRFIDEM4305* data);

#ifdef __cplusplus
}
#endif
