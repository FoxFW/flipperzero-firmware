# Off-air verification tools

Used to prove the transmitter end to end against any `rtl_tcp` receiver -
an RTL-SDR on the local machine, or one shared over the network.

```
python3 rtl_rec.py <host> 1234 433895000 240000 13 test.iq   # record IQ
python3 analyze.py        # FM demod -> tone, deviation, dit timing, Morse decode
python3 wav.py            # render the demodulated audio to a WAV
python3 spec.py           # spectrogram, to find a signal in a busy band
python3 band.py           # per-channel energy over time
python3 readconf.py       # parse the settings file off the device

python3 live_listen.py <host> 1234 433920000 600   # watch and decode live
python3 live_listen.py replay test.iq 433920000    # replay a capture offline
python3 burst_wav.py burst_01.npy out.wav          # render a detected burst
```

Tune 25 kHz below the target so the signal misses the dongle's DC spike;
`analyze.py` mixes it back to baseband.
