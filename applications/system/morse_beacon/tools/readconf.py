import serial, time, struct
s = serial.Serial('/dev/ttyACM0', timeout=3)
s.reset_input_buffer(); s.write(b'\r'); time.sleep(0.4); s.reset_input_buffer()
s.write(b'storage read /ext/apps_data/morse_beacon/beacon.conf\r')
time.sleep(1.5)
buf = s.read(4096)
s.close()
i = buf.find(b'Size: ')
j = buf.find(b'\n', i)
size = int(buf[i+6:j].strip())
# payload starts after the newline; strip trailing prompt
data = buf[j+1:]
if data.endswith(b'>: '): data = data[:-3]
data = data.rstrip(b'\r\n')
print('reported size', size, 'got', len(data))
hdr = data[:8]
print('header magic=0x%02x version=%d checksum=0x%02x flags=0x%02x' % tuple(hdr[:4]))
p = data[8:]
print('payload', len(p), 'bytes (struct is 104)')
print('id_text  :', p[:65].split(b'\0')[0].decode('ascii','replace'))
freq, = struct.unpack_from('<I', p, 68)
print('frequency: %d Hz  (%.4f MHz)' % (freq, freq/1e6))
mode, dev, rxbw = p[72], p[73], p[74]
tone, = struct.unpack_from('<H', p, 76)
print('mode=%d deviation=%d rx_bw=%d tone=%d wpm=%d' % (mode, dev, rxbw, tone, p[78]))
