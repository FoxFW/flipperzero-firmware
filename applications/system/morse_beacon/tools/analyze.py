import numpy as np
from scipy import signal as sg

RATE, OFFSET = 240000, 25000        # tuned 25 kHz low; signal sits at +25 kHz
raw = np.fromfile('test.iq', dtype=np.uint8).astype(np.float32) - 127.5
iq = raw[0::2] + 1j*raw[1::2]
t = np.arange(len(iq)) / RATE
iq = iq * np.exp(-2j*np.pi*OFFSET*t)                 # shift signal to DC

DEC = 5                                              # -> 48 kHz
b = sg.firwin(129, 8000/(RATE/2))
base = sg.lfilter(b, 1, iq)[::DEC]
FS = RATE // DEC

mag = np.abs(base)
env = sg.lfilter(np.ones(240)/240, 1, mag)           # 5 ms smoothing
floor, peak = np.percentile(env, 10), np.percentile(env, 99)
print(f'carrier envelope: floor={floor:.1f} peak={peak:.1f}  ratio={20*np.log10(peak/max(floor,1e-9)):.1f} dB')

carrier = env > (floor + peak) / 2
if not carrier.any():
    raise SystemExit('no carrier found')
i0, i1 = np.argmax(carrier), len(carrier) - np.argmax(carrier[::-1])
print(f'carrier present {i0/FS:.2f}s -> {i1/FS:.2f}s  ({(i1-i0)/FS:.2f} s long)')

# FM demodulate
seg = base[i0:i1]
demod = np.angle(seg[1:] * np.conj(seg[:-1])) * FS / (2*np.pi)   # instantaneous freq, Hz

# audio spectrum during the burst
f, P = sg.welch(demod - demod.mean(), FS, nperseg=4096)
top = f[np.argmax(P[(f > 200) & (f < 5000)]) + np.searchsorted(f, 200)]
print(f'dominant audio tone: {top:.0f} Hz')
print(f'peak deviation: {np.percentile(np.abs(demod - np.median(demod)), 99)/1000:.2f} kHz')

# envelope of the tone -> keying
sos = sg.butter(4, [max(top-300,100)/(FS/2), (top+300)/(FS/2)], btype='band', output='sos')
tone = sg.sosfilt(sos, demod - demod.mean())
tenv = sg.lfilter(np.ones(120)/120, 1, np.abs(tone))
thr = (np.percentile(tenv, 5) + np.percentile(tenv, 95)) / 2
key = tenv > thr

runs, cur, n = [], key[0], 0
for v in key:
    if v == cur: n += 1
    else: runs.append((cur, n)); cur, n = v, 1
runs.append((cur, n))
runs = [(s, c*1000.0/FS) for s, c in runs if c*1000.0/FS > 8]   # drop <8 ms glitches

marks = sorted(d for s, d in runs if s)
dit = np.median([d for d in marks if d < np.median(marks)*1.8]) if marks else 0
print(f'estimated dit: {dit:.1f} ms  -> {1200/dit:.1f} wpm   (sent 18 wpm, dit 66.7 ms)')

TABLE = {".-":"A","-...":"B","-.-.":"C","-..":"D",".":"E","..-.":"F","--.":"G","....":"H",
 "..":"I",".---":"J","-.-":"K",".-..":"L","--":"M","-.":"N","---":"O",".--.":"P","--.-":"Q",
 ".-.":"R","...":"S","-":"T","..-":"U","...-":"V",".--":"W","-..-":"X","-.--":"Y","--..":"Z",
 "-----":"0",".----":"1","..---":"2","...--":"3","....-":"4",".....":"5","-....":"6",
 "--...":"7","---..":"8","----.":"9"}

out, sym = [], ""
for state, dur in runs:
    u = dur / dit
    if state:
        sym += "." if u < 2 else "-"
    else:
        if u > 5:
            out.append(TABLE.get(sym, "?" if sym else "")); out.append(" "); sym = ""
        elif u > 2:
            out.append(TABLE.get(sym, "?" if sym else "")); sym = ""
print("decoded:", repr("".join(out) + TABLE.get(sym, "")))
