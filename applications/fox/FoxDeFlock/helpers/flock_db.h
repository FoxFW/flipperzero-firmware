

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FlockConfidenceNone = 0,
    FlockConfidencePossible,
    FlockConfidenceLikely,
    FlockConfidenceProbeFp,

    FlockConfidenceConfirmed,
} FlockConfidence;

typedef enum {
    FlockClassAlpr = 0,
    FlockClassAcoustic,
} FlockDevClass;

const char* flock_class_str(FlockDevClass cls);

const char* flock_class_long_str(FlockDevClass cls);

bool soundthinking_oui_match(const uint8_t* mac);

FlockDevClass flock_class_from_mac(const uint8_t* mac);

typedef enum {
    FlockIeFpNone = 0,
    FlockIeFpBuiltin,
    FlockIeFpUser,
} FlockIeFp;

FlockIeFp flock_ie_fp_match(uint32_t fp);

size_t flock_oui_count(void);

const uint8_t* flock_oui_get(size_t index);

bool flock_oui_match(const uint8_t* mac);

typedef struct {
    const uint8_t (*ouis)[3];
    size_t oui_count;
    const char* const* ssid_confirmed;
    size_t ssid_confirmed_count;
    const char* const* ssid_likely;
    size_t ssid_likely_count;
    const uint32_t* ie_fps;
    size_t ie_fp_count;
} FlockDbExtras;

void flock_db_set_extras(const FlockDbExtras* extras);

FlockConfidence flock_ssid_confidence(const char* ssid);

const char* flock_confidence_str(FlockConfidence confidence);

typedef enum {
    FlockMethodUnknown = 0,
    FlockMethodSsid,
    FlockMethodIeFp,
    FlockMethodOui,
    FlockMethodBle,
} FlockMethod;

FlockMethod flock_method_of(const uint8_t* mac, const char* ssid, char ftype, uint32_t ie_fp);

const char* flock_method_str(FlockMethod method);

#ifdef __cplusplus
}
#endif
