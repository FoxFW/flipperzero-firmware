/**
 * @file nfc_checker_tag.h
 * @brief Protocol independent snapshot of an NFC tag.
 *
 * A snapshot is taken either from a live read or from a .nfc file loaded with
 * NfcDevice. Everything the comparison engine needs is flattened into this
 * structure so that nfc_checker_compare.c does not depend on firmware protocol
 * internals.
 */
#pragma once

#include <furi.h>
#include <nfc/nfc_device.h>
#include <nfc/protocols/nfc_protocol.h>

#define NFC_CHECKER_UID_MAX_SIZE (10)
#define NFC_CHECKER_NAME_MAX_SIZE (32)

#define NFC_CHECKER_CLASSIC_MAX_SECTORS (40)
#define NFC_CHECKER_CLASSIC_MAX_BLOCKS (256)
#define NFC_CHECKER_CLASSIC_BLOCK_SIZE (16)
#define NFC_CHECKER_CLASSIC_KEY_SIZE (6)

#define NFC_CHECKER_ULTRALIGHT_MAX_PAGES (232)
#define NFC_CHECKER_ULTRALIGHT_PAGE_SIZE (4)

/** Which memory layout a snapshot carries (if any). */
typedef enum {
    NfcCheckerMemoryNone = 0,
    NfcCheckerMemoryClassic,
    NfcCheckerMemoryUltralight,
} NfcCheckerMemoryKind;

typedef struct {
    NfcCheckerMemoryKind kind;

    /* MIFARE Classic */
    uint8_t classic_type; /**< MfClassicType of the dump. */
    uint8_t sector_count;
    uint16_t block_count;
    uint64_t key_a_mask;
    uint64_t key_b_mask;
    uint8_t key_a[NFC_CHECKER_CLASSIC_MAX_SECTORS][NFC_CHECKER_CLASSIC_KEY_SIZE];
    uint8_t key_b[NFC_CHECKER_CLASSIC_MAX_SECTORS][NFC_CHECKER_CLASSIC_KEY_SIZE];
    uint8_t blocks[NFC_CHECKER_CLASSIC_MAX_BLOCKS][NFC_CHECKER_CLASSIC_BLOCK_SIZE];
    uint32_t block_read_mask[NFC_CHECKER_CLASSIC_MAX_BLOCKS / 32];

    /* MIFARE Ultralight / NTAG */
    uint16_t pages_total;
    uint16_t pages_read;
    uint8_t pages[NFC_CHECKER_ULTRALIGHT_MAX_PAGES][NFC_CHECKER_ULTRALIGHT_PAGE_SIZE];
} NfcCheckerMemory;

/** One tag, either the baseline reference or the clone under test. */
typedef struct {
    NfcProtocol protocol;
    char protocol_name[24];
    char name[NFC_CHECKER_NAME_MAX_SIZE];
    bool from_file;

    bool has_uid;
    uint8_t uid[NFC_CHECKER_UID_MAX_SIZE];
    uint8_t uid_len;

    /** ATQA/SAK are only meaningful for ISO14443-3A derived tags. */
    bool has_atqa_sak;
    uint8_t atqa[2];
    uint8_t sak;

    NfcCheckerMemory memory;
} NfcCheckerTag;

/** Allocate a zeroed snapshot on the heap (the structure is a few kilobytes). */
NfcCheckerTag* nfc_checker_tag_alloc(void);

void nfc_checker_tag_free(NfcCheckerTag* tag);

/** Reset a snapshot to its "unknown tag" state. */
void nfc_checker_tag_reset(NfcCheckerTag* tag);

/**
 * @brief Fill a snapshot from an NfcDevice.
 *
 * Works for live reads and for files loaded with nfc_device_load().
 * @return true if at least a UID or memory image could be extracted.
 */
bool nfc_checker_tag_from_device(NfcCheckerTag* tag, const NfcDevice* device);

/** Compare two snapshots by UID length and content. */
bool nfc_checker_tag_uid_equal(const NfcCheckerTag* left, const NfcCheckerTag* right);

/** Render the UID as uppercase hex without separators ("04A2B1C2"). */
void nfc_checker_tag_uid_to_string(const NfcCheckerTag* tag, char* out, size_t out_size);

/** Get a printable protocol name, never NULL. */
const char* nfc_checker_tag_protocol_name(const NfcCheckerTag* tag);

/** True if the given classic block was actually read into the snapshot. */
bool nfc_checker_tag_block_read(const NfcCheckerTag* tag, uint16_t block);

/** True if the given classic key (A or B) of a sector is known. */
bool nfc_checker_tag_key_known(const NfcCheckerTag* tag, uint8_t sector, bool key_b);

/** Get a pointer to a known classic key, or NULL. */
const uint8_t* nfc_checker_tag_key(const NfcCheckerTag* tag, uint8_t sector, bool key_b);

/** True if the snapshot carries MIFARE Classic sector data. */
bool nfc_checker_tag_has_classic(const NfcCheckerTag* tag);

/** True if the snapshot carries MIFARE Ultralight page data. */
bool nfc_checker_tag_has_ultralight(const NfcCheckerTag* tag);
