
#define subghz_protocol_recon_manchester enc_recon_manchester__proto

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <string.h>

#define RECON_MAN_UPLOAD_CAPACITY 1024
#define RECON_MAN_REPEAT_DEFAULT  3

typedef struct {
    const char* name;
    uint32_t te_short;
    uint32_t gap_us;
    uint8_t header_bits;
    uint64_t header_val;
    bool invert;
    bool no_trailing_clock;
    bool ma_lead;
    bool diff_man;
    uint16_t pre_shorts;
    bool prime_long;
    bool prime_hi;
    uint16_t prime_mult;
    bool nrz;
    uint32_t te_long;
    bool pwm;
    uint32_t sync_hi_us;
    bool horm_pre;
    bool inv_pwm;
    bool markw_pwm;
    uint32_t sep_us;
    bool honda_fsm;
    bool hv6_fsm;
    bool mokka;
} ReconManSpec;

static const ReconManSpec recon_man_specs[] = {

    {"BMW/Mini V3", 95, 400, 8, 0xCCU, false, false, false, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"BMW V2",      500, 2200, 0, 0, false, true, false, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Code Alarm",  550, 2000, 0, 0, false, true, true, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Microchip 6SC2", 250, 12000, 16, 0x5554U, false, true, true, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Nissan V0",    500, 30000, 0, 0, false, false, false, true, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},

    {"Audi V2", 280, 20000, 0, 0, true, true, false, false, 44, true, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Audi V3", 383, 20000, 0, 0, true, true, false, false, 23, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Chevrolet V0", 120, 20000, 32, 0xAAAAAAABULL, true, true, true, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Jeep V0", 200, 20000, 0, 0, false, true, false, false, 20, true, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"VAG Polo", 300, 20000, 49, 0x1FFFFFFFF3F91ULL, false, true, true, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Mitsubishi V1", 200, 20000, 2, 0x1ULL, false, false, false, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"LDV V0", 250, 20000, 49, 0x1FFFFFFFFFFFEULL, false, true, true, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Mazda V1", 505, 20000, 2, 0x1ULL, false, false, false, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Siemens VDO", 220, 20000, 16, 0x6001ULL, false, true, true, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Honda V9", 63, 20000, 0, 0, false, true, false, false, 110, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Honda V10", 104, 20000, 0, 0, false, true, false, false, 32, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Cardin S508", 100, 20000, 12, 0x0CF4ULL, false, true, false, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Renault V2", 230, 20000, 2, 0x1ULL, false, false, false, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Lexus V0", 350, 20000, 0, 0, false, true, false, false, 20, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Fiat Ducato V0", 200, 20000, 0, 0, false, true, false, false, 120, true, true, 4, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Renault V3", 52, 20000, 0, 0, false, false, false, false, 12, false, false, 0, true, 0, false, 0, false, false, false, 0, false, false, false},
    {"Honda V7", 250, 20000, 0, 0, false, false, false, false, 20, false, false, 0, false, 480, true, 0, false, false, false, 0, false, false, false},
    {"Renault V4", 125, 20000, 13, 0x1FFEULL, true, true, false, false, 0, false, false, 0, false, 0, false, 1000, false, false, false, 0, false, false, false},
    {"Hormann BiSecur", 208, 20000, 0, 0, false, false, false, false, 8, false, false, 0, false, 416, false, 0, true, false, false, 0, false, false, false},
    {"Honda V8", 400, 20000, 0, 0, false, false, false, false, 0, false, false, 0, false, 800, false, 0, false, true, false, 0, false, false, false},
    {"BMW CAS4", 500, 20000, 0, 0, false, true, true, false, 10, true, false, 4, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Compustar", 708, 2000, 0, 0, false, false, false, false, 0, false, false, 0, false, 1076, false, 0, false, false, true, 0, false, false, false},
    {"Nidec RKE", 250, 2000, 32, 0xFFFFFFF0, true, false, false, false, 0, false, false, 0, false, 500, false, 0, false, false, true, 0, false, false, false},
    {"Audiovox PRO", 445, 2000, 0, 0, false, false, false, false, 0, false, false, 0, false, 895, false, 1368, false, false, true, 0, false, false, false},
    {"Opel V0", 4000, 25000, 0, 0, false, false, false, false, 0, false, false, 0, false, 2000, false, 50000, false, false, true, 5400, false, false, false},
    {"AstroStart 2000", 326, 2000, 0, 0, false, false, false, false, 0, false, false, 0, false, 526, false, 0, false, true, false, 0, false, false, false},
    {"Continental RKE", 100, 1500, 20, 0xF0F06, false, false, false, false, 0, false, false, 0, false, 0, false, 100, false, false, false, 0, false, false, false},
    {"Alps FWB1U545", 500, 20000, 0, 0, false, false, false, false, 12, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Hitag2", 240, 6000, 16, 0x0000, false, false, false, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"GM ABO1502T", 300, 2000, 0, 0, false, false, false, false, 0, false, false, 0, false, 500, false, 0, false, true, false, 0, false, false, false},
    {"Opel V1", 120, 20000, 0, 0, false, false, false, false, 12, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, false},
    {"Honda V5", 63, 3000, 0, 0, false, false, false, false, 12, false, false, 0, false, 126, false, 0, false, false, false, 0, true, false, false},
    {"Honda V6", 100, 20000, 0, 0, false, false, false, false, 26, false, false, 0, false, 200, false, 0, false, false, false, 0, false, true, false},
    {"Opel-Mokka", 100, 2000, 0, 0, false, false, false, false, 0, false, false, 0, false, 0, false, 0, false, false, false, 0, false, false, true},
};

static const ReconManSpec* recon_man_find(const char* name) {
    for(size_t i = 0; i < sizeof(recon_man_specs) / sizeof(recon_man_specs[0]); i++) {
        if(strcmp(recon_man_specs[i].name, name) == 0) return &recon_man_specs[i];
    }
    return NULL;
}

static size_t recon_push(LevelDuration* up, size_t i, size_t cap, bool level, uint32_t us) {
    if(i > 0 && level_duration_get_level(up[i - 1]) == level) {
        up[i - 1] = level_duration_make(level, level_duration_get_duration(up[i - 1]) + us);
        return i;
    }
    if(i < cap) up[i++] = level_duration_make(level, us);
    return i;
}

static size_t
    recon_emit_bit(LevelDuration* up, size_t i, size_t cap, bool bit, uint32_t te, bool invert) {
    bool b = invert ? !bit : bit;
    if(b) {
        i = recon_push(up, i, cap, true, te);
        i = recon_push(up, i, cap, false, te);
    } else {
        i = recon_push(up, i, cap, false, te);
        i = recon_push(up, i, cap, true, te);
    }
    return i;
}

static size_t recon_emit_diffbit(LevelDuration* up, size_t i, size_t cap, bool bit, uint32_t te) {
    if(bit) { i = recon_push(up, i, cap, false, te); i = recon_push(up, i, cap, true, te); }
    else { i = recon_push(up, i, cap, true, te); i = recon_push(up, i, cap, false, te); }
    return i;
}

typedef struct SubGhzProtocolEncoderReconMan SubGhzProtocolEncoderReconMan;
struct SubGhzProtocolEncoderReconMan {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    uint8_t frame[32];
    uint16_t frame_bits;
    uint32_t mokka_key_id;
    uint8_t mokka_event;
    uint8_t hv6_variant;
    uint64_t aux_hi;
    SubGhzBlockGeneric generic;
};

static const SubGhzProtocol subghz_protocol_recon_manchester = {
    .name = "ReconMan",
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = NULL};

static inline bool recon_frame_bit(
    const SubGhzProtocolEncoderReconMan* in, uint64_t payload, uint32_t nbits, uint32_t idx) {
    if(in->frame_bits) return (in->frame[idx >> 3] >> (7 - (idx & 7))) & 1;
    return (payload >> (nbits - 1 - idx)) & 1;
}

static const uint8_t recon_honda_lut[4] = {0x01, 0x91, 0x9B, 0xFB};
static bool recon_honda_step(uint8_t* man, uint8_t tt, uint8_t* bit) {
    if(tt == 8) {
        *man = 1;
        return false;
    }
    if(*man >= 4) {
        *man = 1;
        return false;
    }
    uint8_t ns = (recon_honda_lut[*man] >> tt) & 0x3;
    if(*man == ns) return false;
    *man = ns;
    if(ns == 2) {
        *bit = 0;
        return true;
    }
    if(ns == 1) {
        *bit = 1;
        return true;
    }
    return false;
}
static inline uint8_t recon_honda_ev(bool level, bool wlong) {
    if(!wlong) return level ? 0 : 2;
    return level ? 4 : 6;
}

static size_t recon_honda_emit(
    LevelDuration* up, size_t i, size_t cap, uint8_t* man, bool* lv, uint8_t tbit, uint32_t ts,
    uint32_t tl) {
    for(int len = 1; len <= 3; len++) {
        for(int combo = 0; combo < (1 << len); combo++) {
            uint8_t m = *man;
            bool l = *lv;
            int ne = 0;
            bool ok = false, bad = false;
            bool lvs[3];
            uint32_t wds[3];
            for(int st = 0; st < len; st++) {
                l = !l;
                bool wl = (combo >> st) & 1;
                lvs[st] = l;
                wds[st] = wl ? tl : ts;
                uint8_t b = 0;
                if(recon_honda_step(&m, recon_honda_ev(l, wl), &b)) {
                    if(st == len - 1 && b == tbit && ne == 0)
                        ok = true;
                    else
                        bad = true;
                    ne++;
                }
            }
            if(ok && !bad && ne == 1) {
                for(int st = 0; st < len; st++) i = recon_push(up, i, cap, lvs[st], wds[st]);
                *man = m;
                *lv = l;
                return i;
            }
        }
    }
    return i;
}

static uint8_t recon_hv6_step(uint8_t* man, uint8_t ev) {
    if(ev == 8) {
        *man = 1;
        return 0xFF;
    }
    uint8_t ns = (recon_honda_lut[*man] >> ev) & 0x3;
    if(ns == *man) {
        *man = 1;
        return 0xFF;
    }
    *man = ns;
    if(ns == 2) return 0;
    if(ns == 1) return 1;
    return 0xFF;
}
static inline uint8_t recon_hv6_ev(bool level, bool wlong) {
    if(!wlong) return level ? 2 : 0;
    return level ? 6 : 4;
}
static size_t recon_hv6_emit(
    LevelDuration* up, size_t i, size_t cap, uint8_t* man, bool* lv, uint8_t tbit, uint32_t ts,
    uint32_t tl, bool first_data) {
    for(int len = 1; len <= 3; len++) {
        for(int combo = 0; combo < (1 << len); combo++) {
            uint8_t m = *man;
            bool l = *lv;
            int ne = 0;
            bool ok = false, bad = false;
            bool lvs[3];
            uint32_t wds[3];
            for(int st = 0; st < len; st++) {
                l = !l;
                bool wl = (combo >> st) & 1;
                if(first_data && st == 0 && !wl) bad = true;
                lvs[st] = l;
                wds[st] = wl ? tl : ts;
                uint8_t b = recon_hv6_step(&m, recon_hv6_ev(l, wl));
                if(b != 0xFF) {
                    if(st == len - 1 && b == tbit && ne == 0)
                        ok = true;
                    else
                        bad = true;
                    ne++;
                }
            }
            if(ok && !bad && ne == 1) {
                for(int st = 0; st < len; st++) i = recon_push(up, i, cap, lvs[st], wds[st]);
                *man = m;
                *lv = l;
                return i;
            }
        }
    }
    return i;
}

static bool recon_man_build(SubGhzProtocolEncoderReconMan* instance, const ReconManSpec* s) {
    LevelDuration* up = instance->encoder.upload;
    const size_t cap = RECON_MAN_UPLOAD_CAPACITY;
    size_t i = 0;
    uint64_t payload = instance->generic.data;
    uint32_t pbits = instance->generic.data_count_bit;
    if(pbits == 0 || pbits > 64) pbits = 64;
    uint32_t nbits = instance->frame_bits ? instance->frame_bits : pbits;

    if(s->honda_fsm) {
        i = recon_push(up, i, cap, false, s->gap_us);
        uint8_t man = 1;
        bool lv = false;
        for(uint16_t k = 0; k < s->pre_shorts; k++)
            i = recon_honda_emit(up, i, cap, &man, &lv, 1, s->te_short, s->te_long);
        for(uint32_t idx = 0; idx < nbits; idx++)
            i = recon_honda_emit(
                up, i, cap, &man, &lv, recon_frame_bit(instance, payload, nbits, idx) ? 1 : 0,
                s->te_short, s->te_long);
        i = recon_push(up, i, cap, !lv, s->te_short);
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    if(s->hv6_fsm && instance->hv6_variant == 2) {

        uint64_t d2 = instance->generic.data;
        uint64_t d1 = instance->aux_hi;
        uint32_t tc = instance->generic.data_count_bit / 2;
        if(tc > 60) tc = 60;
        bool lv = true;
        for(int k = 0; k < 18; k++) {
            i = recon_push(up, i, cap, lv, 800);
            lv = !lv;
        }
        for(uint32_t t = 0; t < tc; t++) {
            uint64_t w = (t < 32) ? d2 : d1;
            uint32_t idx = (t < 32) ? t : t - 32;
            uint8_t tv = (uint8_t)((w >> (62 - idx * 2)) & 3);
            uint32_t us = tv == 0 ? 800 : (tv == 1 ? 1200 : 1600);
            i = recon_push(up, i, cap, lv, us);
            lv = !lv;
        }
        i = recon_push(up, i, cap, lv, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }
    if(s->hv6_fsm) {
        i = recon_push(up, i, cap, false, s->gap_us);

        bool bit0 = recon_frame_bit(instance, payload, nbits, 0);
        bool first_level = bit0 ? false : true;
        bool lv = true, last = false;
        for(uint16_t k = 0; k < s->pre_shorts; k++) {
            i = recon_push(up, i, cap, lv, s->te_short);
            last = lv;
            lv = !lv;
        }
        if(last == first_level) {
            i = recon_push(up, i, cap, lv, s->te_short);
            last = lv;
            lv = !lv;
        }
        uint8_t man = 1;
        bool el = last;
        for(uint32_t idx = 0; idx < nbits; idx++)
            i = recon_hv6_emit(
                up, i, cap, &man, &el, recon_frame_bit(instance, payload, nbits, idx) ? 1 : 0,
                s->te_short, s->te_long, idx == 0);

        if(man == 2) {
            i = recon_push(up, i, cap, false, s->te_short);
            i = recon_push(up, i, cap, true, s->te_long);
        } else {
            i = recon_push(up, i, cap, true, s->te_short);
        }
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    if(s->mokka) {
        uint8_t fr[36];
        for(int b = 0; b < 36; b++) fr[b] = 0;
        uint32_t kid = instance->mokka_key_id & 0x7FF;
        uint8_t ev = instance->mokka_event & 0x1F;
        uint64_t pl = instance->generic.data;
#define RECON_MOKKA_SB(idx) fr[(idx) >> 3] |= (uint8_t)(0x80U >> ((idx) & 7))
        RECON_MOKKA_SB(88);
        for(int k = 0; k < 11; k++)
            if((kid >> (10 - k)) & 1) {
                RECON_MOKKA_SB(90 + k);
                RECON_MOKKA_SB(187 + k);
            }
        for(int k = 0; k < 5; k++)
            if((ev >> (4 - k)) & 1) {
                RECON_MOKKA_SB(101 + k);
                RECON_MOKKA_SB(198 + k);
            }
        for(int k = 0; k < 64; k++)
            if((pl >> (63 - k)) & 1) {
                RECON_MOKKA_SB(106 + k);
                RECON_MOKKA_SB(203 + k);
            }
        RECON_MOKKA_SB(170);
        RECON_MOKKA_SB(186);
        RECON_MOKKA_SB(267);
#undef RECON_MOKKA_SB

        for(uint32_t idx = 0; idx < 268; idx++) {
            bool bit = (fr[idx >> 3] >> (7 - (idx & 7))) & 1;
            if(bit) {
                i = recon_push(up, i, cap, true, s->te_short);
                i = recon_push(up, i, cap, false, s->te_short);
            } else {
                i = recon_push(up, i, cap, false, s->te_short);
                i = recon_push(up, i, cap, true, s->te_short);
            }
        }
        i = recon_push(up, i, cap, true, s->te_short);
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    if(s->diff_man) {
        i = recon_push(up, i, cap, false, s->gap_us);
        for(int k = 0; k < 16; k++) i = recon_emit_diffbit(up, i, cap, k & 1, s->te_short);
        for(int8_t b = (int8_t)pbits - 1; b >= 0; b--)
            i = recon_emit_diffbit(up, i, cap, (payload >> b) & 1, s->te_short);
        for(int k = 0; k < 8; k++) i = recon_emit_diffbit(up, i, cap, k & 1, s->te_short);
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    if(s->nrz) {
        i = recon_push(up, i, cap, false, s->gap_us);
        for(uint16_t k = 0; k < s->pre_shorts; k++)
            i = recon_push(up, i, cap, (k & 1) ? false : true, s->te_short);
        for(uint32_t idx = 0; idx < nbits; idx++)
            i = recon_push(up, i, cap, recon_frame_bit(instance, payload, nbits, idx), s->te_short);
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    if(s->pwm) {
        i = recon_push(up, i, cap, false, s->gap_us);
        for(uint16_t k = 0; k < s->pre_shorts; k++) {
            i = recon_push(up, i, cap, true, s->te_short);
            i = recon_push(up, i, cap, false, s->te_short);
        }
        i = recon_push(up, i, cap, true, s->te_short);
        i = recon_push(up, i, cap, false, s->te_short * 3);
        for(uint32_t idx = 0; idx < nbits; idx++) {
            uint32_t w = recon_frame_bit(instance, payload, nbits, idx) ? s->te_long : s->te_short;
            i = recon_push(up, i, cap, true, w);
            i = recon_push(up, i, cap, false, w);
        }
        i = recon_push(up, i, cap, true, s->te_short);
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    if(s->horm_pre) {
        i = recon_push(up, i, cap, false, s->gap_us);
        i = recon_push(up, i, cap, true, s->te_short);
        i = recon_push(up, i, cap, false, s->te_short + s->te_short / 2);
        for(uint16_t k = 0; k < s->pre_shorts; k++) {
            i = recon_push(up, i, cap, true, s->te_short);
            i = recon_push(up, i, cap, false, s->te_short);
        }
        i = recon_push(up, i, cap, true, s->te_long * 4);
        i = recon_push(up, i, cap, false, s->te_long);
        i = recon_push(up, i, cap, true, s->te_long);
        i = recon_push(up, i, cap, false, s->te_long);
        i = recon_push(up, i, cap, true, s->te_long);
        for(uint32_t idx = 0; idx < nbits; idx++)
            i = recon_emit_bit(
                up, i, cap, recon_frame_bit(instance, payload, nbits, idx), s->te_short, s->invert);
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    if(s->inv_pwm) {
        i = recon_push(up, i, cap, false, s->gap_us);
        for(uint32_t idx = 0; idx < nbits; idx++) {
            i = recon_push(up, i, cap, true, s->te_short);
            uint32_t w = recon_frame_bit(instance, payload, nbits, idx) ? s->te_long : s->te_short;
            i = recon_push(up, i, cap, false, w);
        }
        i = recon_push(up, i, cap, true, s->te_short);
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    if(s->markw_pwm) {
        i = recon_push(up, i, cap, false, s->gap_us);
        uint32_t sep = s->sep_us ? s->sep_us : s->te_short;
        if(s->sync_hi_us) {
            i = recon_push(up, i, cap, true, s->sync_hi_us);
            i = recon_push(up, i, cap, false, sep);
        }
        uint32_t nh = s->header_bits;
        uint32_t tot = nh + nbits;
        for(uint32_t idx = 0; idx < tot; idx++) {
            int bit;
            if(idx < nh) {
                bit = (s->header_val >> (nh - 1 - idx)) & 1;
            } else {
                bit = recon_frame_bit(instance, payload, nbits, idx - nh);
                if(s->invert) bit = !bit;
            }
            i = recon_push(up, i, cap, true, bit ? s->te_short : s->te_long);
            if(idx + 1 < tot) i = recon_push(up, i, cap, false, sep);
        }
        i = recon_push(up, i, cap, false, s->gap_us);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    i = recon_push(up, i, cap, false, s->gap_us);
    if(s->sync_hi_us) i = recon_push(up, i, cap, true, s->sync_hi_us);
    if(s->pre_shorts) {
        bool lv = true;
        for(uint16_t k = 0; k < s->pre_shorts; k++) {
            i = recon_push(up, i, cap, lv, s->te_short);
            lv = !lv;
        }
        if(s->prime_long) {
            if(up[i - 1].level == s->prime_hi) i = recon_push(up, i, cap, !s->prime_hi, s->te_short);
            i = recon_push(
                up, i, cap, s->prime_hi, s->te_short * (s->prime_mult ? s->prime_mult : 2));
            i = recon_push(up, i, cap, !s->prime_hi, s->te_short);
        }

    }
    if(s->ma_lead) {
        i = recon_push(up, i, cap, true, s->te_short);
        i = recon_push(up, i, cap, false, s->te_short);
    }
    for(int8_t b = (int8_t)s->header_bits - 1; b >= 0; b--)
        i = recon_emit_bit(up, i, cap, (s->header_val >> b) & 1, s->te_short, s->invert);
    for(uint32_t idx = 0; idx < nbits; idx++)
        i = recon_emit_bit(
            up, i, cap, recon_frame_bit(instance, payload, nbits, idx), s->te_short, s->invert);
    if(!s->no_trailing_clock && !s->ma_lead)
        i = recon_push(up, i, cap, true, s->te_short);
    i = recon_push(up, i, cap, false, s->gap_us);

    instance->encoder.size_upload = i;
    return i > 0 && i < cap;
}

static void* subghz_protocol_encoder_recon_man_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderReconMan* instance = malloc(sizeof(SubGhzProtocolEncoderReconMan));
    instance->base.protocol = &subghz_protocol_recon_manchester;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = RECON_MAN_REPEAT_DEFAULT;
    instance->encoder.size_upload = RECON_MAN_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

static void subghz_protocol_encoder_recon_man_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderReconMan* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static SubGhzProtocolStatus
    subghz_protocol_encoder_recon_man_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderReconMan* instance = context;

    FuriString* proto = furi_string_alloc();
    bool have = flipper_format_rewind(flipper_format) &&
                flipper_format_read_string(flipper_format, "Protocol", proto);
    const ReconManSpec* spec = have ? recon_man_find(furi_string_get_cstr(proto)) : NULL;
    furi_string_free(proto);
    if(!spec) return SubGhzProtocolStatusErrorParserOthers;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;

    instance->frame_bits = 0;
    uint16_t fbits = instance->generic.data_count_bit;
    if(fbits > 64 && fbits <= 256) {
        uint16_t fbytes = (fbits + 7) / 8;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(flipper_format, "Data", instance->frame, fbytes)) {
            instance->frame_bits = fbits;
        } else if(fbits == 128) {

            uint8_t hi[8];
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_hex(flipper_format, "Data", hi, 8)) {
                for(uint8_t b = 0; b < 8; b++) instance->frame[b] = hi[b];
                uint64_t lo = instance->generic.data;
                for(uint8_t b = 0; b < 8; b++)
                    instance->frame[8 + b] = (uint8_t)(lo >> (56 - b * 8));
                instance->frame_bits = 128;
            }
        }
    }

    flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
    {
        uint32_t v = 0;
        flipper_format_rewind(flipper_format);
        instance->mokka_key_id = flipper_format_read_uint32(flipper_format, "KeyId", &v, 1) ? v : 0;
        flipper_format_rewind(flipper_format);
        instance->mokka_event = flipper_format_read_uint32(flipper_format, "Event", &v, 1) ? (uint8_t)v : 0;
        flipper_format_rewind(flipper_format);
        instance->hv6_variant = flipper_format_read_uint32(flipper_format, "Variant", &v, 1) ? (uint8_t)v : 0;
        uint8_t hib[8];
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(flipper_format, "DataHi", hib, 8)) {
            instance->aux_hi = 0;
            for(int b = 0; b < 8; b++) instance->aux_hi = (instance->aux_hi << 8) | hib[b];
        } else {
            instance->aux_hi = 0;
        }
    }
    if(!recon_man_build(instance, spec)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static void subghz_protocol_encoder_recon_man_stop(void* context) {
    SubGhzProtocolEncoderReconMan* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static LevelDuration subghz_protocol_encoder_recon_man_yield(void* context) {
    SubGhzProtocolEncoderReconMan* instance = context;
    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = instance->encoder.upload[instance->encoder.front];
    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }
    return ret;
}

static const SubGhzProtocolEncoder enc_recon_manchester_vt = {
    .alloc = subghz_protocol_encoder_recon_man_alloc,
    .free = subghz_protocol_encoder_recon_man_free,
    .deserialize = subghz_protocol_encoder_recon_man_deserialize,
    .stop = subghz_protocol_encoder_recon_man_stop,
    .yield = subghz_protocol_encoder_recon_man_yield,
};

static const SubGhzEncodePluginEntry enc_recon_manchester_entry = {
    .name = "*",
    .encoder = &enc_recon_manchester_vt};
static const FlipperAppPluginDescriptor enc_recon_manchester_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_recon_manchester_entry};
const FlipperAppPluginDescriptor* enc_recon_manchester_ep(void) {
    return &enc_recon_manchester_descriptor;
}
