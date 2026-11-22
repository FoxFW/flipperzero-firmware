#!/usr/bin/env python3
"""Watch a frequency over rtl_tcp and decode every MCW burst as it happens.

usage: live_listen.py HOST PORT TARGET_HZ [SECONDS]
Tunes 25 kHz low so the signal misses the dongle's DC spike.
"""
import socket, struct, sys, time
import numpy as np
from scipy import signal as sg

RATE, OFFSET, DEC = 240000, 25000, 5
FS = RATE // DEC
MIN_BURST_S, HANG_S = 0.4, 0.25

TABLE = {".-":"A","-...":"B","-.-.":"C","-..":"D",".":"E","..-.":"F","--.":"G","....":"H",
 "..":"I",".---":"J","-.-":"K",".-..":"L","--":"M","-.":"N","---":"O",".--.":"P","--.-":"Q",
 ".-.":"R","...":"S","-":"T","..-":"U","...-":"V",".--":"W","-..-":"X","-.--":"Y","--..":"Z",
 "-----":"0",".----":"1","..---":"2","...--":"3","....-":"4",".....":"5","-....":"6",
 "--...":"7","---..":"8","----.":"9","-...-":"=","-..-.":"/","..--..":"?",".-.-.-":".",
 "--..--":",","-.-.--":"!",".----.":"'","-.--.":"(","-.--.-":")",".-...":"&","---...":":",
 "-.-.-.":";",".-.-.":"+","-....-":"-","..--.-":"_",".-..-.":'"',"...-..-":"$",".--.-.":"@"}

def decode(seg):
    d = np.angle(seg[1:]*np.conj(seg[:-1])) * FS/(2*np.pi)
    d = d - np.median(d)
    f, P = sg.welch(d, FS, nperseg=min(4096, max(2, len(d)//2)))
    band = (f > 300) & (f < 3000)
    if not band.any(): return None
    tone = float(f[band][np.argmax(P[band])])
    if not (500 < tone < 1400): return None
    dev = float(np.percentile(np.abs(d), 99))

    sos = sg.butter(4, [max(tone-300,150)/(FS/2), (tone+300)/(FS/2)], btype='band', output='sos')
    tenv = sg.lfilter(np.ones(120)/120, 1, np.abs(sg.sosfilt(sos, d)))
    key = tenv > (np.percentile(tenv,5) + np.percentile(tenv,95))/2
    runs, cur, n = [], key[0], 0
    for v in key:
        if v == cur: n += 1
        else: runs.append((cur,n)); cur, n = v, 1
    runs.append((cur,n))
    runs = [(s, c*1000.0/FS) for s,c in runs if c*1000.0/FS > 8]
    marks = sorted(x for s,x in runs if s)
    if not marks: return None
    dit = float(np.median([x for x in marks if x < np.median(marks)*1.8]))
    out, sym = [], ""
    for st, ms in runs:
        u = ms/dit
        if st: sym += "." if u < 2 else "-"
        elif u > 5: out.append(TABLE.get(sym,"?" if sym else "")); out.append(" "); sym=""
        elif u > 2: out.append(TABLE.get(sym,"?" if sym else "")); sym=""
    return tone, dev, dit, ("".join(out)+TABLE.get(sym,"")).strip()

def main():
  HOST = sys.argv[1]
  PORT = int(sys.argv[2]) if HOST != 'replay' else 0
  TARGET = int(sys.argv[3])
  RUN_S = float(sys.argv[4]) if len(sys.argv) > 4 else 3600.0
  CENTER = TARGET - OFFSET
  if HOST == 'replay':
      # PORT position carries the .iq path; lets the exact detection loop be
      # validated offline against a known-good recording.
      class FileSource:
          def __init__(self, path): self.f = open(path, 'rb')
          def recv(self, n): return self.f.read(n)
          def sendall(self, *a): pass
          def settimeout(self, *a): pass
          def close(self): self.f.close()
      s = FileSource(sys.argv[2])
      print(f'replaying {sys.argv[2]}', flush=True)
      RUN_S = 1e9
  else:
      s = None
      for attempt in range(5):
          try:
              s = socket.create_connection((HOST, PORT), timeout=10); break
          except OSError as e:
              print(f'connect {attempt+1} failed: {e}', flush=True); time.sleep(2)
      if s is None: raise SystemExit('unreachable')
      s.recv(12)
      for cmd, val in ((0x02, RATE), (0x03, 0), (0x08, 0), (0x01, CENTER)):
          s.sendall(struct.pack('>BI', cmd, val)); time.sleep(0.15)

  print(f'listening {TARGET/1e6:.4f} MHz via {HOST}:{PORT} for {RUN_S:.0f}s', flush=True)
  taps = sg.firwin(129, 8000/(RATE/2))
  zi = np.zeros(len(taps)-1)
  w, phase = 2*np.pi*OFFSET/RATE, 0.0
  env_taps = np.ones(120)/120
  env_zi = np.zeros(119)
  floor, have_floor = 0.0, False
  in_burst, burst, quiet, t_burst = False, [], 0, 0.0
  t0 = time.time(); n_total = 0; nfound = 0
  s.settimeout(20)
  buf = b''
  while time.time() - t0 < RUN_S:
      try: chunk = s.recv(262144)
      except socket.timeout: break
      if HOST == 'replay' and not chunk and not buf: break
      if not chunk: break
      buf += chunk
      if len(buf) < 2: continue
      use = len(buf) - (len(buf) % 2)
      d = np.frombuffer(buf[:use], dtype=np.uint8).astype(np.float32) - 127.5
      buf = buf[use:]
      iq = d[0::2] + 1j*d[1::2]
      L = len(iq)
      iq = iq * np.exp(-1j*(w*np.arange(L) + phase))
      phase = (phase + w*L) % (2*np.pi)
      base, zi = sg.lfilter(taps, 1, iq, zi=zi)
      base = base[::DEC]
      n_total += len(base)

      env, env_zi = sg.lfilter(env_taps, 1, np.abs(base), zi=env_zi)
      # Freeze the floor while a burst is in progress: updating it mid-burst
      # let a long transmission drag the threshold up under itself until the
      # signal fell below it and the burst was lost.
      if not in_burst:
          blk = float(np.percentile(env, 20))
          floor = blk if not have_floor else 0.95*floor + 0.05*blk
          have_floor = True
      thr = max(floor*3.0, 1.0)

      on = env > thr
      for i, v in enumerate(on):
          if v:
              if not in_burst:
                  in_burst, burst, quiet = True, [], 0
                  t_burst = (n_total - len(base) + i)/FS
              burst.append(base[i]); quiet = 0
          elif in_burst:
              quiet += 1; burst.append(base[i])
              if quiet > HANG_S*FS:
                  seg = np.array(burst[:-int(HANG_S*FS)])
                  in_burst = False
                  if len(seg)/FS >= MIN_BURST_S:
                      nfound += 1
                      np.save(f'burst_{nfound:02d}.npy', seg)
                      r = decode(seg)
                      if r:
                          tone, dev, dit, text = r
                          print(f'[{t_burst:7.2f}s] {len(seg)/FS:5.2f}s tone {tone:4.0f} Hz '
                                f'dev {dev/1000:.2f} kHz dit {dit:5.1f} ms ({1200/dit:4.1f} wpm) '
                                f'-> "{text}"', flush=True)
                      else:
                          print(f'[{t_burst:7.2f}s] {len(seg)/FS:5.2f}s kept as '
                                f'burst_{nfound:02d}.npy (not recognised as MCW)', flush=True)
  s.close()
  print(f'done, {nfound} MCW burst(s) decoded', flush=True)


if __name__ == '__main__':
    main()
