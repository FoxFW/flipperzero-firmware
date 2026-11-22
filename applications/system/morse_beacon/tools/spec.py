import numpy as np
from scipy import signal as sg

RATE = 240000
raw = np.fromfile('test.iq', dtype=np.uint8).astype(np.float32) - 127.5
iq = raw[0::2] + 1j*raw[1::2]

f, t, S = sg.spectrogram(iq, RATE, nperseg=4096, noverlap=0, return_onesided=False)
f = np.fft.fftshift(f); S = np.fft.fftshift(S, axes=0)
db = 10*np.log10(S + 1e-12)
base = np.median(db)

print(f"record {len(iq)/RATE:.1f}s, noise floor {base:.1f} dB")
print("\nPer-100ms peak bin (freq offset from 433.895 MHz):")
step = max(1, int(0.1 / (t[1]-t[0])))
for i in range(0, len(t), step):
    col = db[:, i]
    k = np.argmax(col)
    if col[k] - base > 12:
        print(f"  t={t[i]:5.2f}s  peak {f[k]/1000:+7.1f} kHz  {col[k]-base:5.1f} dB")

# energy in a 12 kHz window centred on +25 kHz (433.920) over time
sel = (f > 25000-6000) & (f < 25000+6000)
e = 10*np.log10(S[sel].sum(axis=0) + 1e-12)
print("\n433.920 +/-6 kHz band energy:")
for i in range(0, len(t), step):
    bar = "#" * max(0, int(e[i] - np.percentile(e, 10)))
    print(f"  t={t[i]:5.2f}s {e[i]:6.1f} {bar}")
