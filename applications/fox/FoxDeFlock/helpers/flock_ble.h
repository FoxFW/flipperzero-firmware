

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "flock_db.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLOCK_BLE_COMPANY_ID 0x09C8

typedef enum {
    FlockBleModelUnknown = 0,
    FlockBleModelGeneric,
    FlockBleModelFalcon,
    FlockBleModelRaven,
} FlockBleModel;

bool flock_ble_extract_serial(
    const uint8_t* mfg,
    size_t len,
    const char* name,
    char* out_serial,
    size_t serial_cap);

FlockBleModel flock_ble_model_ex(const char* serial, const char* name, bool raven_gatt);

FlockConfidence flock_ble_confidence(uint16_t company, const char* name, bool raven_gatt);

const char* flock_ble_model_str(FlockBleModel model);

#ifdef __cplusplus
}
#endif
