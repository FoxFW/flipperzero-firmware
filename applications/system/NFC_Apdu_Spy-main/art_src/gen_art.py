# Generates ../spy_art.h for NFC APDU Spy (run: python3 art_src/gen_art.py; needs pillow, numpy, scipy)
# Sources (128x64, gray):  spy_base.png  = empty scene with stop field
#                          spy_box.png   = scene with the empty monitor window
#                          spy_saved.png = monitor window with the check symbol
import os, tempfile
import numpy as np
from PIL import Image, ImageDraw, ImageFont, BdfFontFile
from scipy import ndimage as nd
S = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(S, '..', 'spy_art.h')
W, H, P, SH = 128, 64, 4, 52
def gray(name): return np.array(Image.open(os.path.join(S, name)).convert('L')).astype(int)
g1, g2, g3 = gray('spy_base.png'), gray('spy_box.png'), gray('spy_saved.png')
T = (255 - g1)[:SH]                               # darkness 0..255
L0 = np.clip(np.round(T / 255 * P), 0, P)
pm = Image.new('L', (W, SH), 0)
ImageDraw.Draw(pm).polygon([(12,52),(12,47),(14,40),(16,33),(18,28),(19,22),(23,19),(24,16),(23,13),(22,11),(26,9),(30,6),(32,4),(40,4),(46,5),(49,8),(49,13),(52,16),(54,20),(53,25),(56,29),(55,34),(53,41),(50,46),(48,48),(43,50),(43,52)], fill=255)
dol = np.array(pm) > 0
dol = dol | (nd.binary_dilation(dol, iterations=2) & (L0 == 0))
cm = Image.new('L', (W, SH), 0); ImageDraw.Draw(cm).polygon([(13,0),(26,0),(64,52),(7,52)], fill=255)
cone = (np.array(cm) > 0) & ~dol
# shift the whole gray scene left so the dolphin lines up with the 1-bit screen,
# and fill the exposed right columns with the BW background's shape (so GRAY == BW there)
SHIFT = 8
T = np.roll(T, -SHIFT, axis=1)
dol = np.roll(dol, -SHIFT, axis=1); dol[:, -SHIFT:] = False
cone = np.roll(cone, -SHIFT, axis=1); cone[:, -SHIFT:] = False
_bw = np.array(Image.open(os.path.join(S, 'bw_base.png')).convert('L'))[1:1 + SH]
FILL = 16 # right columns taken from the BW shape (covers the shifted edge + stray pixels)
T[:, W - FILL:] = np.where(_bw[:, W - FILL:] < 128, 255, 0)
dol[:, W - FILL:] = False
cone[:, W - FILL:] = False
# monitor window
BX0, BY0, BX1, BY1 = 58, 2, 125, 49
BW, BH = BX1 - BX0 + 1, BY1 - BY0 + 1
box = (255 - g2)[BY0:BY1+1, BX0:BX1+1]
chk = (g3[BY0:BY1+1, BX0:BX1+1] > 170)
chk[0, :] = chk[-1, :] = False; chk[:, 0] = chk[:, -1] = False
chk[:, :2] = False; chk[:, -2:] = False; chk[:3, :] = False; chk[-3:, :] = False
# stop field
SX, SY = 95, 52
stop = g1[SY:64, SX:128] < 128
def bits(m):
    w = (m.shape[1] + 7) // 8; out = []
    for y in range(m.shape[0]):
        for xb in range(w):
            v = 0
            for i in range(8):
                x = xb * 8 + i
                if x < m.shape[1] and m[y, x]: v |= 1 << i
            out.append(v)
    return out
def carr(name, typ, vals, per=24):
    s = 'static const %s %s[%d] = {\n' % (typ, name, len(vals))
    for i in range(0, len(vals), per): s += '    ' + ', '.join(str(v) for v in vals[i:i+per]) + ',\n'
    return s + '};\n\n'
def load_bdf(name):
    tmp = os.path.join(tempfile.mkdtemp(), name)
    BdfFontFile.BdfFontFile(open(os.path.join(S, name + '.bdf'), 'rb')).save(tmp)
    return ImageFont.load(tmp + '.pil')
def font(name, fn, cw, chh):
    F = load_bdf(fn); rows = []; ink = []
    for c in range(32, 127):
        im = Image.new('L', (cw + 4, chh + 4), 0); ImageDraw.Draw(im).text((0, 0), chr(c), font=F, fill=255)
        m = (np.array(im) > 0)[:chh, :cw]
        cols = np.where(m.any(0))[0]; ink.append(int(cols.max()) + 1 if len(cols) else 0)
        for y in range(chh):
            rows.append(sum(1 << x for x in range(cw) if m[y, x]))
    return carr('font_%s_rows' % name, 'uint16_t', rows, 16) + carr('font_%s_ink' % name, 'uint8_t', ink, 32) + \
        'static const SpyFont font_%s = {%d, %d, font_%s_rows, font_%s_ink};\n\n' % (name, cw, chh, name, name)
h = '// Generated art for NFC APDU Spy (do not edit by hand, see art_src/gen_art.py)\n#pragma once\n#include <stdint.h>\n\n'
h += '#define ART_W 128\n#define ART_H %d\n\n' % SH
h += '// darkness per pixel 0=white .. 255=black\n' + carr('art_tone', 'uint8_t', T.ravel().tolist(), 32)
h += '// 1 bit per pixel, LSB = left: dolphin (no dither) / lamp light cone (flickers)\n'
h += carr('art_dolphin', 'uint8_t', bits(dol), 32) + carr('art_cone', 'uint8_t', bits(cone), 32)
h += '// monitor window (gray mode): darkness per pixel\n'
h += '#define BOX_X0 %d\n#define BOX_Y0 %d\n#define BOX_X1 %d\n#define BOX_Y1 %d\n#define BOX_W %d\n#define BOX_H %d\n\n' % (BX0, BY0, BX1, BY1, BW, BH)
h += carr('art_box', 'uint8_t', box.ravel().tolist(), 34)
h += '// stop field bottom right (1 bit, set = black)\n#define STOP_X %d\n#define STOP_Y %d\n#define STOP_W %d\n#define STOP_H %d\n' % (SX, SY, stop.shape[1], stop.shape[0])
h += carr('art_stop', 'uint8_t', bits(stop), 20)
h += 'typedef struct {\n    uint8_t w, h; // cell size\n    const uint16_t* rows; // h rows per char, ASCII 32..126\n    const uint8_t* ink; // used width per char\n} SpyFont;\n\n'
h += font('status', '6x13', 6, 13) + font('log', '4x6', 4, 6) + font('name', '5x8', 5, 8)
F = load_bdf('6x13'); im = Image.new('L', (10, 16), 0); ImageDraw.Draw(im).text((0, 0), 'S', font=F, fill=255)
capbot = int(np.where(np.array(im).any(1))[0].max())
h += '#define STATUS_TOP %d\n' % (63 - capbot)
# ---- 1-bit mode (default): background picture + overlays ----
# Sources are 128x65; the top row is dropped so everything lines up with the 64-row screen.
# Overlay colors: magenta = see-through, white, black.
def rgba(name):
    a = np.array(Image.open(os.path.join(S, name)).convert('RGBA')).astype(int)[1:65]
    return a
bw = rgba('bw_base.png')
bwk = bw[..., :3].mean(-1) < 128
bwk[0, :2] = bwk[0, -2:] = True   # rounded corners of the mockup -> black
h += '\n// 1-bit background (whole screen, set = black)\n' + carr('bw_base', 'uint8_t', bits(bwk), 32)
h += 'typedef struct {\n    uint8_t x, y, w, h;\n    const uint8_t* white; // 1 bit per pixel, rows of (w+7)/8 bytes\n    const uint8_t* black;\n} SpyOverlay;\n\n'
for name, fn in [('get_near_target', 'ov_get_near_target.png'), ('frame', 'ov_frame.png'),
                 ('hold_still', 'ov_hold_still.png'), ('check', 'ov_check.png'),
                 ('data_saved', 'ov_data_saved.png'), ('scan_tag', 'ov_scan_tag.png'),
                 ('fail', 'ov_fail.png')]:
    a = rgba(fn)
    mag = (a[..., 0] > 200) & (a[..., 1] < 80) & (a[..., 2] > 80) & (a[..., 2] < 180)
    lum = a[..., :3].mean(-1)
    wh = ~mag & (lum >= 128); bk = ~mag & (lum < 128)
    ys, xs = np.where(wh | bk)
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    wc, bc = wh[y0:y1+1, x0:x1+1], bk[y0:y1+1, x0:x1+1]
    h += carr('ov_%s_w' % name, 'uint8_t', bits(wc), 32) + carr('ov_%s_b' % name, 'uint8_t', bits(bc), 32)
    h += 'static const SpyOverlay ov_%s = {%d, %d, %d, %d, ov_%s_w, ov_%s_b};\n\n' % (
        name, x0, y0, x1 - x0 + 1, y1 - y0 + 1, name, name)

# ---- movable bottom-bar buttons (from buttons.png) ----
h += 'typedef struct {\n    uint8_t w, h;\n    const uint8_t* white;\n    const uint8_t* black;\n} SpyBtn;\n\n'
_b = np.array(Image.open(os.path.join(S, 'buttons.png')).convert('RGBA')).astype(int)
_bmag = (_b[..., 0] > 200) & (_b[..., 1] < 80) & (_b[..., 2] > 80) & (_b[..., 2] < 180)
_blum = _b[..., :3].mean(-1)
for name, (ry0, ry1) in [('back', (2, 12)), ('no', (15, 25)), ('yes', (28, 38)),
                         ('exit', (41, 51)), ('stop', (54, 64))]:
    wh = (~_bmag & (_blum >= 128)); bk = (~_bmag & (_blum < 128))
    band = (wh | bk).copy(); band[:ry0] = False; band[ry1 + 1:] = False
    ys, xs = np.where(band)
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    wc = wh[y0:y1 + 1, x0:x1 + 1]; bc = bk[y0:y1 + 1, x0:x1 + 1]
    h += carr('btn_%s_w' % name, 'uint8_t', bits(wc), 32) + carr('btn_%s_b' % name, 'uint8_t', bits(bc), 32)
    h += 'static const SpyBtn btn_%s = {%d, %d, btn_%s_w, btn_%s_b};\n\n' % (
        name, x1 - x0 + 1, y1 - y0 + 1, name, name)
open(OUT, 'w').write(h)
print('ok', len(h), 'check px', int(chk.sum()), 'stop', stop.shape)
