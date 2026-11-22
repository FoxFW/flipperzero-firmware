

#include "flock_ble.h"
#include <string.h>

static char ascii_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static bool is_print(uint8_t b) {
    return b >= 0x20 && b < 0x7f;
}

static bool ci_prefix(const char* s, const char* needle_upper) {
    if(!s) return false;
    for(size_t k = 0; needle_upper[k]; k++) {
        if(ascii_upper(s[k]) != needle_upper[k]) return false;
    }
    return true;
}

static bool ci_contains(const char* s, const char* needle_upper) {
    if(!s || !needle_upper[0]) return false;
    for(const char* h = s; *h; h++) {
        if(ci_prefix(h, needle_upper)) return true;
    }
    return false;
}

FlockConfidence flock_ble_confidence(uint16_t company, const char* name, bool raven_gatt) {

    if(company == FLOCK_BLE_COMPANY_ID) return FlockConfidenceConfirmed;
    if(raven_gatt) return FlockConfidenceConfirmed;
    if(ci_prefix(name, "PENGUIN") || ci_contains(name, "FS EXT")) return FlockConfidenceConfirmed;

    return FlockConfidencePossible;
}

bool flock_ble_extract_serial(
    const uint8_t* mfg,
    size_t len,
    const char* name,
    char* out_serial,
    size_t serial_cap) {
    if(!out_serial || serial_cap == 0) return false;
    out_serial[0] = '\0';

    if(mfg && len > 2) {
        size_t best_start = 0, best_len = 0;
        size_t run_start = 0, run_len = 0;
        for(size_t i = 2; i <= len; i++) {
            bool ok = (i < len) && is_print(mfg[i]) &&
                      ((mfg[i] >= '0' && mfg[i] <= '9') ||
                       (ascii_upper((char)mfg[i]) >= 'A' && ascii_upper((char)mfg[i]) <= 'Z'));
            if(ok) {
                if(run_len == 0) run_start = i;
                run_len++;
            } else {
                if(run_len > best_len) {
                    best_len = run_len;
                    best_start = run_start;
                }
                run_len = 0;
            }
        }
        if(best_len >= 6) {
            size_t n = best_len;
            if(n > serial_cap - 1) n = serial_cap - 1;
            memcpy(out_serial, &mfg[best_start], n);
            out_serial[n] = '\0';
            return true;
        }
    }

    if(name && name[0]) {
        const char* p = name;
        if(ci_prefix(p, "PENGUIN-")) p += 8;

        size_t i = 0;
        bool has_digit = false;
        for(; p[i]; i++) {
            char c = p[i];
            if(c >= '0' && c <= '9')
                has_digit = true;
            else if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
                break;
        }
        if(p[i] == '\0' && i >= 6 && has_digit) {
            size_t n = i;
            if(n > serial_cap - 1) n = serial_cap - 1;
            memcpy(out_serial, p, n);
            out_serial[n] = '\0';
            return true;
        }
    }

    return false;
}

FlockBleModel flock_ble_model_ex(const char* serial, const char* name, bool raven_gatt) {

    if(raven_gatt) {
        return FlockBleModelRaven;
    }

    (void)serial;

    if(name && (ci_prefix(name, "PENGUIN") || ci_prefix(name, "FS EXT"))) {
        return FlockBleModelGeneric;
    }
    if(serial && serial[0]) {
        return FlockBleModelGeneric;
    }
    return FlockBleModelUnknown;
}

const char* flock_ble_model_str(FlockBleModel model) {
    switch(model) {

    case FlockBleModelFalcon:
        return "Flock Falcon? (ALPR)";
    case FlockBleModelRaven:
        return "Flock Raven (audio)";
    case FlockBleModelGeneric:
        return "Flock device (ext. battery)";
    case FlockBleModelUnknown:
    default:
        return "-";
    }
}
