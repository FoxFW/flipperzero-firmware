#!/usr/bin/env python3
"""Record raw IQ from an rtl_tcp server."""
import socket, struct, sys, time

HOST, PORT = sys.argv[1], int(sys.argv[2])
FREQ = int(sys.argv[3])
RATE = int(sys.argv[4])
SECONDS = float(sys.argv[5])
OUT = sys.argv[6]

def cmd(s, c, v):
    s.sendall(struct.pack(">BI", c, v))

s = None
for attempt in range(5):
    try:
        s = socket.create_connection((HOST, PORT), timeout=10)
        break
    except OSError as e:
        print(f"connect attempt {attempt+1} failed: {e}", flush=True)
        time.sleep(2)
if s is None:
    raise SystemExit(f"could not reach {HOST}:{PORT}")
hdr = s.recv(12)
print(f"magic={hdr[:4]!r} tuner={hdr[4:8].hex()} gains={hdr[8:12].hex()}", flush=True)

cmd(s, 0x02, RATE)      # sample rate
time.sleep(0.1)
cmd(s, 0x03, 0)         # tuner gain mode: 0 = automatic
time.sleep(0.1)
cmd(s, 0x08, 0)         # RTL AGC off
time.sleep(0.1)
cmd(s, 0x01, FREQ)      # center frequency
time.sleep(0.3)

need = int(RATE * 2 * SECONDS)
got = 0
s.settimeout(15)
t0 = time.time()
with open(OUT, "wb") as f:
    while got < need:
        b = s.recv(262144)
        if not b:
            break
        f.write(b)
        got += len(b)
s.close()
print(f"wrote {got} bytes ({got/2/RATE:.2f} s) in {time.time()-t0:.1f}s wall", flush=True)
