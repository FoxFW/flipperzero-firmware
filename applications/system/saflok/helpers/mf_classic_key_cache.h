#pragma once

#include <nfc/protocols/mf_classic/mf_classic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MfClassicKeyCache MfClassicKeyCache;

MfClassicKeyCache* mf_classic_key_cache_alloc(void);

void mf_classic_key_cache_free(MfClassicKeyCache* keyCache);

bool mf_classic_key_cache_load(MfClassicKeyCache* keyCache, const uint8_t* uid, size_t uid_len);

void mf_classic_key_cache_load_from_data(MfClassicKeyCache* keyCache, const MfClassicData* data);

bool mf_classic_key_cache_get_next_key(
    MfClassicKeyCache* keyCache,
    uint8_t* sector_num,
    MfClassicKey* key,
    MfClassicKeyType* key_type);

bool mf_classic_key_cache_save(MfClassicKeyCache* keyCache, const MfClassicData* data);

void mf_classic_key_cache_reset(MfClassicKeyCache* keyCache);

#ifdef __cplusplus
}
#endif
