// NFC APDU Spy - drawing in grayscale
// Everything is painted into lev[][] (0 = white/orange ... GRAY_P = black).
// render_subframe() turns lev[][] into one 1-bit picture for the LCD.
// Showing GRAY_P of them quickly one after another makes the gray tones
// (temporal dithering, same technique as in "Flipper from Hell").
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "spy_art.h"

#define GRAY_PMAX 8 // deepest gray mode (s8)
static int g_p = 4; // current gray depth s3..s8 = number of subframes (s4 = 5 tones)
#define GRAY_P g_p

static uint8_t lev[64][128];

static const uint8_t bayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static uint8_t lut[16][GRAY_PMAX + 1]; // lut[matrix][tone] = in which subframes the pixel is black

static void gfx_build_lut(void) {
    const int P = GRAY_P;
    for(int b = 0; b < 16; b++)
        for(int level = 0; level <= P; level++) {
            uint8_t mask = 0;
            for(int sf = 0; sf < P; sf++) {
                int t = (sf + b * P / 16) % P;
                if((t + 1) * level / P != t * level / P) mask |= (uint8_t)(1 << sf);
            }
            lut[b][level] = mask;
        }
}

static inline bool art_bit(const uint8_t* m, int x, int y) {
    return (m[y * 16 + (x >> 3)] >> (x & 7)) & 1;
}

// background picture. light = 0..10 (0 = lamp off, 10 = lamp fully on)
// Dolphin: clean tones only (no dither). Rest: ordered dither for soft gradients.
static void gfx_scene(int light) {
    for(int y = 0; y < ART_H; y++)
        for(int x = 0; x < ART_W; x++) {
            int t = art_tone[y * ART_W + x];
            if(art_bit(art_cone, x, y)) {
                int dark = t > 204 ? t : 204;
                t = dark + (t - dark) * light / 10;
            }
            int l;
            if(art_bit(art_dolphin, x, y))
                l = (t * GRAY_P + 127) / 255;
            else
                l = (t * GRAY_P * 16 + 255 * bayer4[y & 3][x & 3] + 127) / (255 * 16);
            if(l > GRAY_P) l = GRAY_P;
            lev[y][x] = (uint8_t)l;
        }
}

static void gfx_fill(int x0, int y0, int x1, int y1, uint8_t l) {
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > 127) x1 = 127;
    if(y1 > 63) y1 = 63;
    for(int y = y0; y <= y1; y++)
        for(int x = x0; x <= x1; x++) lev[y][x] = l;
}

static inline void gfx_px(int x, int y, uint8_t l, int ymax) {
    if(x >= 0 && x < 128 && y >= 0 && y <= ymax) lev[y][x] = l;
}

static void gfx_char(const SpyFont* f, int x, int y, char c, uint8_t l, int ymax) {
    if(c < 32 || c > 126) c = '?';
    const uint16_t* r = &f->rows[(c - 32) * f->h];
    for(int j = 0; j < f->h; j++)
        for(int i = 0; i < f->w; i++)
            if(r[j] & (1u << i)) gfx_px(x + i, y + j, l, ymax);
}

static void gfx_text(const SpyFont* f, int x, int y, const char* s, uint8_t l, int ymax) {
    for(; *s; s++, x += f->w) gfx_char(f, x, y, *s, l, ymax);
}

// width of the inked part of a text
static int gfx_text_width(const SpyFont* f, const char* s) {
    int n = (int)strlen(s);
    if(!n) return 0;
    char last = s[n - 1];
    if(last < 32 || last > 126) last = '?';
    return (n - 1) * f->w + f->ink[last - 32];
}

// blit a bottom-bar button sprite (white -> orange, black -> black, magenta -> skip)
static void gfx_button(const SpyBtn* b, int x, int y) {
    int bpr = (b->w + 7) / 8;
    for(int j = 0; j < b->h; j++)
        for(int i = 0; i < b->w; i++) {
            int k = j * bpr + (i >> 3), bit = i & 7;
            if((b->white[k] >> bit) & 1)
                gfx_px(x + i, y + j, 0, 63);
            else if((b->black[k] >> bit) & 1)
                gfx_px(x + i, y + j, GRAY_P, 63);
        }
}

// white text with a 1px black border, centered on column cx (reads over any BG)
// left-aligned white text with a 1px black border (bold look over any BG)
static void gfx_text_outlined(const SpyFont* f, int x, int y, const char* s, int ymax) {
    for(int dy = -1; dy <= 1; dy++)
        for(int dx = -1; dx <= 1; dx++)
            if(dx || dy) gfx_text(f, x + dx, y + dy, s, GRAY_P, ymax);
    gfx_text(f, x, y, s, 0, ymax);
}

// faux-bold text: each letter drawn twice (1px offset) to thicken it, and the
// cell advanced by one extra pixel so bold letters keep a clear gap between them
static void gfx_text_bold(const SpyFont* f, int x, int y, const char* s, uint8_t l, int ymax) {
    for(; *s; s++, x += f->w + 1) {
        gfx_char(f, x, y, *s, l, ymax);
        gfx_char(f, x + 1, y, *s, l, ymax);
    }
}
// width of bold text (advance f->w+1 per char, plus the 1px bold overstrike)
static int gfx_text_bold_width(const SpyFont* f, const char* s) {
    int n = (int)strlen(s);
    if(!n) return 0;
    char last = s[n - 1];
    if(last < 32 || last > 126) last = '?';
    return (n - 1) * (f->w + 1) + f->ink[last - 32] + 1;
}

// filled right-pointing triangle (selection cursor), left edge at x, apex to the right
static void gfx_tri(int x, int y, int h, uint8_t l) {
    int mid = (h - 1) / 2;
    for(int r = 0; r < h; r++) {
        int len = (r <= mid ? r : h - 1 - r) + 1;
        for(int c = 0; c < len; c++) gfx_px(x + c, y + r, l, 63);
    }
}

// text clipped to the horizontal range [xmin, xmax] (for a scrolling marquee)
static void
    gfx_text_clip(const SpyFont* f, int x, int y, const char* s, uint8_t l, int ymax, int xmin, int xmax) {
    for(; *s; s++, x += f->w) {
        char c = *s;
        if(c < 32 || c > 126) c = '?';
        const uint16_t* r = &f->rows[(c - 32) * f->h];
        for(int j = 0; j < f->h; j++)
            for(int i = 0; i < f->w; i++)
                if((r[j] & (1u << i))) {
                    int xx = x + i;
                    if(xx >= xmin && xx <= xmax) gfx_px(xx, y + j, l, ymax);
                }
    }
}

// ---- 1-bit mode (default look) ----
// whole screen from the 1-bit background picture (bar and stop field included)
static void gfx_bw_base(void) {
    for(int y = 0; y < 64; y++)
        for(int x = 0; x < 128; x++)
            lev[y][x] = (bw_base[y * 16 + (x >> 3)] >> (x & 7)) & 1 ? GRAY_P : 0;
}

// picture element on top (headline pictures, frame, check): white and black pixels,
// everything else stays as it is
static void gfx_overlay(const SpyOverlay* o) {
    const int bpr = (o->w + 7) / 8;
    for(int j = 0; j < o->h; j++)
        for(int i = 0; i < o->w; i++) {
            int k = j * bpr + (i >> 3), bit = i & 7;
            if((o->white[k] >> bit) & 1)
                lev[o->y + j][o->x + i] = 0;
            else if((o->black[k] >> bit) & 1)
                lev[o->y + j][o->x + i] = GRAY_P;
        }
}

// status text (1-bit mode): status left, Stop button sprite on the right
static void gfx_status_text(const char* status) {
    gfx_fill(0, 52, 127, 63, 0);
    for(int x = 0; x < 128; x++) lev[51][x] = GRAY_P;
    gfx_text(&font_status, 2, STATUS_TOP, status, GRAY_P, 63);
    gfx_button(&btn_stop, 128 - btn_stop.w, 64 - btn_stop.h);
}

// bottom bar: status text on the left (down to the last row), stop field on the right
static void gfx_status_bar(const char* status) {
    gfx_fill(0, 52, 127, 63, 0);
    gfx_text(&font_status, 2, STATUS_TOP, status, GRAY_P, 63);
    const int bpr = (STOP_W + 7) / 8;
    for(int j = 0; j < STOP_H; j++)
        for(int i = 0; i < STOP_W; i++)
            lev[STOP_Y + j][STOP_X + i] = (art_stop[j * bpr + (i >> 3)] >> (i & 7)) & 1 ? GRAY_P : 0;
}

// monitor window (from the picture: see-through dark glass with a white frame)
#define BOX_LINES 5
#define BOX_CHARS 16
static void gfx_box_background(void) {
    for(int j = 0; j < BOX_H; j++)
        for(int i = 0; i < BOX_W; i++) {
            int x = BOX_X0 + i, y = BOX_Y0 + j;
            int t = art_box[j * BOX_W + i];
            int l = (t * GRAY_P * 16 + 255 * bayer4[y & 3][x & 3] + 127) / (255 * 16);
            lev[y][x] = (uint8_t)(l > GRAY_P ? GRAY_P : l);
        }
}

// monitor window with the live log. ink = text tone (0 = white)
// bw = 1-bit mode: black window with the white frame picture, else gray glass
static void gfx_log_box(
    char lines[][BOX_CHARS + 1],
    int n,
    int typed,
    bool cursor,
    uint8_t ink,
    bool bw) {
    if(bw) {
        gfx_fill(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, GRAY_P);
        gfx_overlay(&ov_frame);
    } else {
        gfx_box_background();
    }
    int y = 5;
    for(int k = 0; k < n; k++, y += 8) {
        char tmp[BOX_CHARS + 1];
        strncpy(tmp, lines[k], BOX_CHARS);
        tmp[BOX_CHARS] = 0;
        if(k == n - 1 && typed >= 0 && typed < (int)strlen(tmp)) tmp[typed] = 0;
        gfx_text(&font_log, 61, y, tmp, ink, BOX_Y1 - 1);
        if(k == n - 1 && cursor) {
            int w = (int)strlen(tmp) * font_log.w;
            if(61 + w + 3 < BOX_X1) gfx_fill(61 + w, y, 61 + w + 2, y + 5, ink);
        }
    }
}

#define PX_IDX(x, y) (((y) >> 3) * 128 + (x))
#define PX_BIT(y)    ((uint8_t)(1 << ((y) & 7)))

// one subframe (sf = 0..GRAY_P-1) in the LCD page format (1024 bytes)
// gray = false: plain black/white (middle gray and darker = black)
static void gfx_render_subframe(int sf, uint8_t* out, bool gray) {
    memset(out, 0, 1024);
    for(int y = 0; y < 64; y++)
        for(int x = 0; x < 128; x++) {
            int l = lev[y][x];
            if(!l) continue;
            bool on = gray ? (lut[bayer4[y & 3][x & 3]][l] >> sf) & 1 : l * 2 >= GRAY_P;
            if(on) out[PX_IDX(x, y)] |= PX_BIT(y);
        }
}
