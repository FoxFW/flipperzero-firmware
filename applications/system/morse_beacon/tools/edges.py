import numpy as np
from scipy import signal as sg
RATE, OFFSET, DEC = 240000, 25000, 5
raw = np.fromfile('test.iq', dtype=np.uint8).astype(np.float32) - 127.5
iq = raw[0::2] + 1j*raw[1::2]
iq = iq*np.exp(-2j*np.pi*OFFSET*np.arange(len(iq))/RATE)
base = sg.lfilter(sg.firwin(129, 8000/(RATE/2)), 1, iq)[::DEC]
FS = RATE//DEC

env = sg.lfilter(np.ones(240)/240, 1, np.abs(base))
lo, hi = np.percentile(env,10), np.percentile(env,99)
car = env > (lo+hi)/2
i0, i1 = np.argmax(car), len(car)-np.argmax(car[::-1])
print(f'carrier   : {i0/FS:.3f}s -> {i1/FS:.3f}s   ({(i1-i0)/FS:.3f} s)')

d = np.angle(base[1:]*np.conj(base[:-1]))*FS/(2*np.pi)
d = d - sg.lfilter(np.ones(2400)/2400,1,d)
tone = sg.sosfilt(sg.butter(4,[500/(FS/2),1200/(FS/2)],btype='band',output='sos'), d)
tenv = sg.lfilter(np.ones(120)/120,1,np.abs(tone))
key = tenv > (np.percentile(tenv,5)+np.percentile(tenv,95))/2
j0, j1 = np.argmax(key), len(key)-np.argmax(key[::-1])
print(f'tone      : {j0/FS:.3f}s -> {j1/FS:.3f}s   ({(j1-j0)/FS:.3f} s)')
print(f'carrier before first dit : {(j0-i0)/FS*1000:6.0f} ms   (preamble set to 300 ms)')
print(f'carrier after last dit   : {(i1-j1)/FS*1000:6.0f} ms   (tail set to 200 ms)')
