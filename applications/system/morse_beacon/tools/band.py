import numpy as np
from scipy import signal as sg
RATE = 240000
raw = np.fromfile('test.iq', dtype=np.uint8).astype(np.float32) - 127.5
iq = raw[0::2] + 1j*raw[1::2]
f, t, S = sg.spectrogram(iq, RATE, nperseg=2048, noverlap=0, return_onesided=False)
f = np.fft.fftshift(f); S = np.fft.fftshift(S, axes=0)

for name, ctr, bw in [('433.920 (target)', 25000, 5000), ('433.9356 (ambient)', 40600, 5000)]:
    sel = (f > ctr-bw) & (f < ctr+bw)
    e = 10*np.log10(S[sel].sum(axis=0) + 1e-12)
    lo = np.percentile(e, 5)
    print(f'\n--- {name}: min {e.min():.1f} max {e.max():.1f} dB ---')
    step = max(1, len(t)//45)
    for i in range(0, len(t), step):
        print(f'  {t[i]:5.2f}s {e[i]:6.1f} ' + '#'*max(0,int((e[i]-lo)*1.5)))
