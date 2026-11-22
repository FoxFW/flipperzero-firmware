

#include "flock_db.h"
#include <string.h>

static const uint8_t flock_ouis[][3] = {
    {0x70, 0xc9, 0x4e}, {0x3c, 0x91, 0x80}, {0xd8, 0xf3, 0xbc}, {0x80, 0x30, 0x49},
    {0xb8, 0x35, 0x32}, {0x14, 0x5a, 0xfc}, {0x74, 0x4c, 0xa1}, {0x08, 0x3a, 0x88},
    {0x9c, 0x2f, 0x9d}, {0xc0, 0x35, 0x32}, {0x94, 0x08, 0x53}, {0xe4, 0xaa, 0xea},
    {0xf4, 0x6a, 0xdd}, {0x24, 0xb2, 0xb9}, {0x00, 0xf4, 0x8d}, {0xd0, 0x39, 0x57},
    {0xe8, 0xd0, 0xfc}, {0xe0, 0x4f, 0x43}, {0xb8, 0x1e, 0xa4}, {0x70, 0x08, 0x94},
    {0x58, 0x8e, 0x81}, {0xec, 0x1b, 0xbd}, {0x3c, 0x71, 0xbf}, {0x58, 0x00, 0xe3},
    {0x90, 0x35, 0xea}, {0x5c, 0x93, 0xa2}, {0x64, 0x6e, 0x69}, {0x48, 0x27, 0xea},
    {0xa4, 0xcf, 0x12}, {0x82, 0x6b, 0xf2}, {0xb4, 0x1e, 0x52},
};

#define FLOCK_OUI_COUNT (sizeof(flock_ouis) / sizeof(flock_ouis[0]))

static const uint8_t soundthinking_ouis[][3] = {
    {0xd4, 0x11, 0xd6},
};

#define SOUNDTHINKING_OUI_COUNT (sizeof(soundthinking_ouis) / sizeof(soundthinking_ouis[0]))

bool soundthinking_oui_match(const uint8_t* mac) {
    if(!mac) return false;
    for(size_t i = 0; i < SOUNDTHINKING_OUI_COUNT; i++) {
        if(mac[0] == soundthinking_ouis[i][0] && mac[1] == soundthinking_ouis[i][1] &&
           mac[2] == soundthinking_ouis[i][2]) {
            return true;
        }
    }

    return false;
}

FlockDevClass flock_class_from_mac(const uint8_t* mac) {
    return soundthinking_oui_match(mac) ? FlockClassAcoustic : FlockClassAlpr;
}

const char* flock_class_str(FlockDevClass cls) {
    return (cls == FlockClassAcoustic) ? "Acoustic" : "ALPR";
}

const char* flock_class_long_str(FlockDevClass cls) {

    return (cls == FlockClassAcoustic) ? "SoundThinking sensor" : "Flock / ALPR camera";
}

static const FlockDbExtras* g_extras = NULL;

void flock_db_set_extras(const FlockDbExtras* extras) {
    g_extras = extras;
}

size_t flock_oui_count(void) {
    return FLOCK_OUI_COUNT;
}

const uint8_t* flock_oui_get(size_t index) {
    if(index >= FLOCK_OUI_COUNT) return NULL;
    return flock_ouis[index];
}

bool flock_oui_match(const uint8_t* mac) {
    if(!mac) return false;
    for(size_t i = 0; i < FLOCK_OUI_COUNT; i++) {
        if(mac[0] == flock_ouis[i][0] && mac[1] == flock_ouis[i][1] &&
           mac[2] == flock_ouis[i][2]) {
            return true;
        }
    }

    if(g_extras) {
        for(size_t i = 0; i < g_extras->oui_count; i++) {
            if(mac[0] == g_extras->ouis[i][0] && mac[1] == g_extras->ouis[i][1] &&
               mac[2] == g_extras->ouis[i][2]) {
                return true;
            }
        }
    }
    return false;
}

static char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool ci_contains(const char* haystack, const char* needle_lower) {
    if(!haystack || !needle_lower) return false;
    size_t nlen = strlen(needle_lower);
    if(nlen == 0) return false;
    for(const char* h = haystack; *h; h++) {
        size_t k = 0;
        while(needle_lower[k] && ascii_lower(h[k]) == needle_lower[k]) {
            k++;
        }
        if(k == nlen) return true;
    }
    return false;
}

static bool is_flock_provisioning_ssid(const char* ssid) {
    const char* pfx = "flock-";
    for(int i = 0; i < 6; i++) {
        if(ssid[i] == '\0' || ascii_lower(ssid[i]) != pfx[i]) return false;
    }
    for(int i = 6; i < 12; i++) {
        char c = ssid[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if(!hex) return false;
    }
    return ssid[12] == '\0';
}

FlockConfidence flock_ssid_confidence(const char* ssid) {
    if(!ssid || ssid[0] == '\0') return FlockConfidenceNone;

    if(is_flock_provisioning_ssid(ssid) || ci_contains(ssid, "test_flck")) {
        return FlockConfidenceConfirmed;
    }

    if(g_extras) {
        for(size_t i = 0; i < g_extras->ssid_confirmed_count; i++) {
            if(ci_contains(ssid, g_extras->ssid_confirmed[i])) return FlockConfidenceConfirmed;
        }
    }

    if(ci_contains(ssid, "flock") || ci_contains(ssid, "flck")) {
        return FlockConfidenceLikely;
    }

    if(g_extras) {
        for(size_t i = 0; i < g_extras->ssid_likely_count; i++) {
            if(ci_contains(ssid, g_extras->ssid_likely[i])) return FlockConfidenceLikely;
        }
    }

    return FlockConfidenceNone;
}

static const uint32_t flock_ie_fps[] = {
    0,
};

#define FLOCK_IE_FP_COUNT (sizeof(flock_ie_fps) / sizeof(flock_ie_fps[0]))

FlockIeFp flock_ie_fp_match(uint32_t fp) {
    if(fp == 0) return FlockIeFpNone;

    for(size_t i = 0; i < FLOCK_IE_FP_COUNT; i++) {
        if(flock_ie_fps[i] == 0) continue;
        if(flock_ie_fps[i] == fp) return FlockIeFpBuiltin;
    }

    if(g_extras) {
        for(size_t i = 0; i < g_extras->ie_fp_count; i++) {
            if(g_extras->ie_fps[i] == fp) return FlockIeFpUser;
        }
    }
    return FlockIeFpNone;
}

const char* flock_confidence_str(FlockConfidence confidence) {
    switch(confidence) {
    case FlockConfidenceConfirmed:
        return "CONFIRMED";
    case FlockConfidenceProbeFp:
        return "Class?";
    case FlockConfidenceLikely:
        return "Likely";
    case FlockConfidencePossible:
        return "Possible";
    case FlockConfidenceNone:
    default:
        return "-";
    }
}

FlockMethod flock_method_of(const uint8_t* mac, const char* ssid, char ftype, uint32_t ie_fp) {

    if(flock_ssid_confidence(ssid) != FlockConfidenceNone) return FlockMethodSsid;
    if(flock_ie_fp_match(ie_fp) != FlockIeFpNone) return FlockMethodIeFp;

    if(flock_oui_match(mac) || soundthinking_oui_match(mac)) return FlockMethodOui;

    if(ftype == 'L') return FlockMethodBle;
    return FlockMethodUnknown;
}

const char* flock_method_str(FlockMethod method) {

    switch(method) {
    case FlockMethodSsid:
        return "SSID";
    case FlockMethodIeFp:
        return "IE fp";
    case FlockMethodOui:
        return "OUI";
    case FlockMethodBle:
        return "BLE mfg ID";
    case FlockMethodUnknown:
    default:

        return "ESP probe rule";
    }
}
