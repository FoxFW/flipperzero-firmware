#include "hitagmicro.h"
#include <furi.h>
#include <furi_hal_rfid.h>
#include <lib/bit_lib/bit_lib.h>
#include <stdio.h>

#define TAG "HitagMicro"

#define HITAGMICRO_FLAGS        0x04
#define HITAGMICRO_CMD_READ_UID 0x02
#define HITAGMICRO_CMD_SYSINFO  0x17
#define HITAGMICRO_CMD_READ     0x12
#define HITAGMICRO_CMD_LOGIN    0x28
#define HITAGMICRO_CMD_WRITE    0x14

#define HITAGMICRO_PAGE_BLOCK0 0x00
#define HITAGMICRO_PAGE_BLOCK1 0x01
#define HITAGMICRO_PAGE_CONFIG 0xFF

#define HITAGMICRO_GAP_US           64
#define HITAGMICRO_BIT0_ON_US       96
#define HITAGMICRO_BIT1_ON_US       160
#define HITAGMICRO_SOF_VIOLATION_US 288
#define HITAGMICRO_CHARGE_US        3000

#define HITAGMICRO_WAIT_UID_US   21000
#define HITAGMICRO_WAIT_SYS_US   13000
#define HITAGMICRO_WAIT_READ_US  17000
#define HITAGMICRO_WAIT_LOGIN_US 9000
#define HITAGMICRO_WAIT_WRITE_US 30000
#define HITAGMICRO_WRITE_REPEATS 2
#define HITAGMICRO_POWERDOWN_US  20000
#define HITAGMICRO_LATCH_CYCLES  6
#define HITAGMICRO_LATCH_HOLD_US 50000
#define HITAGMICRO_COLD_RESET_US 100000

static const struct {
    uint8_t password[LFRFID_HITAGMICRO_BLOCK_SIZE];
    const char* name;
} hitagmicro_variants[HitagMicroVariantCount] = {
    [HitagMicroVariant8265] = {{0x00, 0x00, 0x00, 0x00}, "8265"},
    [HitagMicroVariant8210] = {{0x9A, 0xC4, 0x99, 0x9C}, "8210"},
    [HitagMicroVariantH55] = {{0x49, 0x6B, 0x0E, 0x59}, "H5.5"},
};

const uint8_t* hitagmicro_variant_password(HitagMicroVariant variant) {
    if(variant >= HitagMicroVariantCount) return NULL;
    return hitagmicro_variants[variant].password;
}

const char* hitagmicro_variant_name(HitagMicroVariant variant) {
    if(variant >= HitagMicroVariantCount) return "Unknown";
    return hitagmicro_variants[variant].name;
}

static void hitagmicro_put_bit(uint8_t* buf, size_t* bitpos, bool bit) {
    bit_lib_set_bit(buf, *bitpos, bit);
    (*bitpos)++;
}

static void hitagmicro_put_lsb(uint8_t* buf, size_t* bitpos, uint8_t value, uint8_t nbits) {
    for(uint8_t i = 0; i < nbits; i++) {
        hitagmicro_put_bit(buf, bitpos, (value >> i) & 1);
    }
}

static void
    hitagmicro_put_msb_bytes(uint8_t* buf, size_t* bitpos, const uint8_t* src, size_t nbits) {
    for(size_t i = 0; i < nbits; i++) {
        hitagmicro_put_bit(buf, bitpos, bit_lib_get_bit(src, i));
    }
}

static uint16_t hitagmicro_crc16(const uint8_t* d, size_t bitlength) {
    if(bitlength == 0) return 0;

    uint16_t remainder = 0;

    uint8_t offset = (8 - (bitlength % 8)) % 8;
    uint8_t prebits = 0;

    for(size_t i = 0; i < (bitlength + 7) / 8; i++) {
        uint8_t c = prebits | (uint8_t)(d[i] >> offset);
        prebits = (uint8_t)(d[i] << (8 - offset));

        remainder ^= (uint16_t)(c << 8);
        for(uint8_t j = 8; j; --j) {
            if(remainder & 0x8000) {
                remainder = (uint16_t)((remainder << 1) ^ 0x1021);
            } else {
                remainder <<= 1;
            }
        }
    }

    return bit_lib_reverse_16_fast(remainder);
}

static void hitagmicro_put_crc(uint8_t* buf, size_t* bitpos, uint16_t crc) {
    for(uint8_t i = 0; i < 16; i++) {
        hitagmicro_put_bit(buf, bitpos, (crc >> i) & 1);
    }
}

static size_t hitagmicro_build_login(uint8_t* tx, const uint8_t* password) {
    size_t bitpos = 0;
    hitagmicro_put_lsb(tx, &bitpos, HITAGMICRO_FLAGS, 5);
    hitagmicro_put_lsb(tx, &bitpos, HITAGMICRO_CMD_LOGIN, 6);
    hitagmicro_put_msb_bytes(tx, &bitpos, password, 32);
    hitagmicro_put_crc(tx, &bitpos, hitagmicro_crc16(tx, bitpos));
    return bitpos;
}

static size_t hitagmicro_build_write(uint8_t* tx, uint8_t page, const uint8_t* data) {
    size_t bitpos = 0;
    hitagmicro_put_lsb(tx, &bitpos, HITAGMICRO_FLAGS, 5);
    hitagmicro_put_lsb(tx, &bitpos, HITAGMICRO_CMD_WRITE, 6);
    hitagmicro_put_lsb(tx, &bitpos, page, 8);
    hitagmicro_put_msb_bytes(tx, &bitpos, data, 32);
    hitagmicro_put_crc(tx, &bitpos, hitagmicro_crc16(tx, bitpos));
    return bitpos;
}

static size_t hitagmicro_build_cmd(uint8_t* tx, uint8_t cmd) {
    size_t bitpos = 0;
    hitagmicro_put_lsb(tx, &bitpos, HITAGMICRO_FLAGS, 5);
    hitagmicro_put_lsb(tx, &bitpos, cmd, 6);
    hitagmicro_put_crc(tx, &bitpos, hitagmicro_crc16(tx, bitpos));
    return bitpos;
}

static size_t hitagmicro_build_read(uint8_t* tx, uint8_t page, uint8_t count) {
    size_t bitpos = 0;
    hitagmicro_put_lsb(tx, &bitpos, HITAGMICRO_FLAGS, 5);
    hitagmicro_put_lsb(tx, &bitpos, HITAGMICRO_CMD_READ, 6);
    hitagmicro_put_lsb(tx, &bitpos, page, 8);
    hitagmicro_put_lsb(tx, &bitpos, count, 8);
    hitagmicro_put_crc(tx, &bitpos, hitagmicro_crc16(tx, bitpos));
    return bitpos;
}

static void hitagmicro_gap(void) {
    furi_hal_rfid_tim_read_pause();
    furi_delay_us(HITAGMICRO_GAP_US);
    furi_hal_rfid_tim_read_continue();
}

static void hitagmicro_send_bit(bool bit) {
    hitagmicro_gap();
    furi_delay_us(bit ? HITAGMICRO_BIT1_ON_US : HITAGMICRO_BIT0_ON_US);
}

static void hitagmicro_send_sof(void) {

    hitagmicro_send_bit(false);
    hitagmicro_gap();
    furi_delay_us(HITAGMICRO_SOF_VIOLATION_US);
}

static void hitagmicro_send_frame(const uint8_t* tx, size_t nbits) {
    hitagmicro_send_sof();
    for(size_t i = 0; i < nbits; i++) {
        hitagmicro_send_bit((tx[i / 8] >> (7 - (i % 8))) & 1);
    }

    hitagmicro_gap();
}

static void hitagmicro_log_frame(const char* label, const uint8_t* tx, size_t nbits) {
#ifndef LOGS_RELEASE_BUILD
    if(furi_log_get_level() < FuriLogLevelDebug) return;
    char hex[3 * 9 + 1] = {0};
    size_t pos = 0;
    for(size_t i = 0; i < (nbits + 7) / 8 && pos + 3 < sizeof(hex); i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", tx[i]);
    }
    FURI_LOG_D(TAG, "tx %s (%u bits): %s", label, (unsigned)nbits, hex);
#else
    UNUSED(label);
    UNUSED(tx);
    UNUSED(nbits);
#endif
}

static void hitagmicro_send(const uint8_t* tx, size_t nbits, uint32_t wait_us) {
    FURI_CRITICAL_ENTER();
    hitagmicro_send_frame(tx, nbits);
    FURI_CRITICAL_EXIT();
    furi_delay_us(wait_us);
}

static void hitagmicro_field_on(void) {
    furi_hal_rfid_tim_read_start(125000, 0.5);
    furi_hal_rfid_pin_pull_release();
}

static void hitagmicro_field_off(void) {
    furi_hal_rfid_tim_read_stop();
    furi_hal_rfid_pins_reset();
    furi_delay_us(HITAGMICRO_POWERDOWN_US);
}

void hitagmicro_write(const LFRFIDHitagMicro* data, const uint8_t* password) {
    furi_check(data);
    furi_check(password);

    furi_delay_us(HITAGMICRO_COLD_RESET_US);

    uint8_t read_uid_tx[16] = {0};
    uint8_t sysinfo_tx[16] = {0};
    uint8_t read_cfg_tx[16] = {0};
    uint8_t login_tx[16] = {0};
    size_t read_uid_bits = hitagmicro_build_cmd(read_uid_tx, HITAGMICRO_CMD_READ_UID);
    size_t sysinfo_bits = hitagmicro_build_cmd(sysinfo_tx, HITAGMICRO_CMD_SYSINFO);
    size_t read_cfg_bits = hitagmicro_build_read(read_cfg_tx, HITAGMICRO_PAGE_CONFIG, 0x00);
    size_t login_bits = hitagmicro_build_login(login_tx, password);

    hitagmicro_log_frame("READ UID", read_uid_tx, read_uid_bits);
    hitagmicro_log_frame("SYSINFO", sysinfo_tx, sysinfo_bits);
    hitagmicro_log_frame("READ config", read_cfg_tx, read_cfg_bits);
    hitagmicro_log_frame("LOGIN", login_tx, login_bits);

    const struct {
        uint8_t page;
        const uint8_t* block;
        const char* label;
    } steps[3] = {
        {HITAGMICRO_PAGE_BLOCK0, data->block0, "WRITE block0"},
        {HITAGMICRO_PAGE_BLOCK1, data->block1, "WRITE block1"},
        {HITAGMICRO_PAGE_CONFIG, data->config, "WRITE config"},
    };

    for(uint8_t i = 0; i < 3; i++) {
        uint8_t write_tx[16] = {0};
        size_t write_bits = hitagmicro_build_write(write_tx, steps[i].page, steps[i].block);
        hitagmicro_log_frame(steps[i].label, write_tx, write_bits);

        hitagmicro_field_on();
        furi_delay_us(HITAGMICRO_CHARGE_US);
        hitagmicro_send(read_uid_tx, read_uid_bits, HITAGMICRO_WAIT_UID_US);
        hitagmicro_send(sysinfo_tx, sysinfo_bits, HITAGMICRO_WAIT_SYS_US);
        hitagmicro_send(read_cfg_tx, read_cfg_bits, HITAGMICRO_WAIT_READ_US);
        hitagmicro_send(login_tx, login_bits, HITAGMICRO_WAIT_LOGIN_US);

        for(uint8_t w = 0; w < HITAGMICRO_WRITE_REPEATS; w++) {
            hitagmicro_send(write_tx, write_bits, HITAGMICRO_WAIT_WRITE_US);
        }

        hitagmicro_field_off();
    }

    for(uint8_t i = 0; i < HITAGMICRO_LATCH_CYCLES; i++) {
        hitagmicro_field_on();

        furi_delay_us(HITAGMICRO_LATCH_HOLD_US);
        hitagmicro_field_off();
    }
}
