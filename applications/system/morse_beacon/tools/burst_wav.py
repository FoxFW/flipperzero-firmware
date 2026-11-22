#!/usr/bin/env python3
"""Render a saved burst (.npy complex baseband at 48 kHz) to audio."""
import sys
import numpy as np
from scipy import signal as sg
from scipy.io import wavfile

FS = 48000
seg = np.load(sys.argv[1])
d = np.angle(seg[1:]*np.conj(seg[:-1])) * FS/(2*np.pi)
a = d - sg.lfilter(np.ones(2400)/2400, 1, d)
a = sg.sosfilt(sg.butter(4, [300/(FS/2), 3000/(FS/2)], btype='band', output='sos'), a)
a = a / (np.abs(a).max() + 1e-9) * 0.85
wavfile.write(sys.argv[2], FS, (a*32767).astype(np.int16))
print(f'{sys.argv[2]}  {len(a)/FS:.2f} s')
