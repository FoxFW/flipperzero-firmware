import numpy as np
from scipy import signal as sg
from scipy.io import wavfile

RATE, OFFSET, DEC = 240000, 25000, 5
raw = np.fromfile('test.iq', dtype=np.uint8).astype(np.float32) - 127.5
iq = raw[0::2] + 1j*raw[1::2]
iq = iq * np.exp(-2j*np.pi*OFFSET*np.arange(len(iq))/RATE)
base = sg.lfilter(sg.firwin(129, 8000/(RATE/2)), 1, iq)[::DEC]
FS = RATE // DEC

demod = np.angle(base[1:]*np.conj(base[:-1])) * FS/(2*np.pi)
a = demod - sg.lfilter(np.ones(2400)/2400, 1, demod)      # strip the DC/park offset
a = sg.sosfilt(sg.butter(4, [300/(FS/2), 3000/(FS/2)], btype='band', output='sos'), a)
a = a / (np.abs(a).max() + 1e-9) * 0.85
wavfile.write('morse_id_offair.wav', FS, (a*32767).astype(np.int16))
print(f'wrote morse_id_offair.wav  {len(a)/FS:.1f} s @ {FS} Hz')
