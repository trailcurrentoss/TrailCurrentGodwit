# Godwit radio

What the RTL-SDR USB dongle receives: FM radio, the [aircraft](#aircraft) and
[ships](#ships) nearby, and [NOAA Weather Radio](#weather-radio). There is one
dongle, so it does one at a time.

Aircraft, Ships and Weather Radio appear only when **Settings > System > Country**
is United States, and it starts out not set: until then their icons are not shown
and their programs do not run. What may be received varies by country, and some
restrict signals like these even though they are unencrypted. Only US law has been
reviewed, and that was not legal advice.

## FM

`godwit-radio` turns the dongle's raw samples into stereo audio and reads the
station's RDS. The HMI's Radio app runs it while the radio is what is heard ([`../hmi/src/radio.cpp`](../hmi/src/radio.cpp)),
with `pw-play` carrying the sound to PipeWire's multimedia role, the same as
music. Everything happens on the head unit; no internet is involved.

### What it needs

Any RTL2832U dongle with an R820T or R820T2 tuner (tested with a Nooelec NESDR
SMArt v5) and an FM antenna. Plug it in at any time: the app says "Tuner not
connected" until it appears.

AM is not available. These tuners stop at about 24 MHz, and AM broadcast is at
1 MHz, so AM would take an upconverter as well. Voice requests for AM say so.

### How it is used

- **First use:** with no presets saved, the band is scanned (about 20 seconds)
  and the six strongest stations become the presets.
- **Right knob or dial:** turn to tune. Press or the arrows to seek.
- **Presets:** tap to listen, hold to save the current station there.
- **Saved:** presets, the last frequency and station names, in
  `/var/lib/godwit/radio.json`.

The band plan and de-emphasis are North America's (87.9 to 107.9 MHz in 200 kHz
steps, 75 µs), as are the call signs and programme types read from RDS.

### How it works

```text
dongle  1.14 MS/s I/Q
  |  mix the station to zero, channel filter, / 5
228 kHz  discriminator -> multiplex
  |-- / 4   57 kHz: left + right, and left - right from the 38 kHz subcarrier
  |         de-emphasis, stereo blend, fade on a weak signal -> stdout
  |-- / 24  9.5 kHz: RDS from the 57 kHz subcarrier -> call sign, name, text
  '-- 19 kHz pilot PLL: the phase both subcarriers are taken from
```

The rates make the pilot exactly 12 samples and an RDS symbol exactly 8. The
tuner sits a quarter of the sample rate below the station, which keeps the
station off the tuner's DC spike and makes the mix trivial.

Signal quality comes from the steadiness of the envelope. FM has a constant
envelope and noise does not, so this needs no calibration and does not depend
on the tuner's gain. An empty channel reads 0 to 3 dB. A weak station reads
about 9 and a strong one 15 to 20; a real station's own wide peaks stop it
reading higher.

## Aircraft

`godwit-adsb` decodes the ADS-B broadcasts aircraft send on 1090 MHz: who they
are, where, how high and how fast. The HMI's Aircraft app runs it only while the
app is open, and the radio is silent meanwhile. It comes back when the app
closes. Nothing is kept once the app closes, so what the app shows was heard
within the last minute.

It is for interest. Plenty of aircraft send no ADS-B, so an empty scope does not
mean an empty sky.

### What it needs

- **An antenna for 1090 MHz.** An FM antenna is the wrong length and may hear
  only what is close. A quarter-wave whip is about 69 mm, and it needs a view of
  the sky: indoors or inside a vehicle, range drops sharply.
- **A GPS fix,** for distance, bearing and the scope. Without one the aircraft
  are still listed, with their altitude and speed.

### How it is used

- **Right knob:** turn to move through the aircraft, press to change the range
  (10, 25, 50 or 100 nautical miles).
- **Left knob:** turn to move through the aircraft. In Park, press to open the full
  list, turn to scroll it, and press again to select that aircraft and go back to
  its data. A call, Reverse, the assistant or an incoming message takes the knob
  over, as they do in Climate, and it is not the volume here.
- **Screen:** tap an aircraft on the scope to choose it. In Park, **Full list**
  shows them all, the nearest first.

### How it works

```text
dongle  2.4 MS/s I/Q, fixed gain
  |  magnitude
  |  preamble: four pulses standing above the quiet between them
  |  read 112 bits, at five fractions of a sample, until the parity agrees
frames -> callsign, category, altitude, speed
  '-- a pair of compact positions, one even, one odd -> latitude, longitude
```

A bit is 2.4 samples, so it rarely lands on whole ones. Reading each likely frame
at several phases and letting its 24-bit parity choose is what makes this
sensitive. One bad bit is repaired, but a repaired frame cannot introduce an
aircraft, and an aircraft needs two messages to be shown, so noise does not put
anything on the screen. A position no aircraft could have reached in the time
is ignored.

ADS-B does not say what an aircraft is or where it is going. The HMI looks those
up by address and callsign in tables on the head unit, packed by
[`tools/pack_aircraft_data.py`](tools/pack_aircraft_data.py) from Virtual Radar
Server's Standing Data. Routes are widely covered but contributed by volunteers
and tied to the callsign, so one can be out of date. Types are known for about
17,000 aircraft. For the rest the app shows the category the aircraft broadcasts
("Large"), then a route if there is one. A flight with several stops shows the
first and last.

The lines it prints are at the top of [`src/adsb_main.cpp`](src/adsb_main.cpp).
`--gain <tenths of a dB>` replaces the tuner's highest gain.

## Ships

`godwit-ais` decodes the AIS broadcasts ships send on 161.975 and 162.025 MHz.
They use both in turn, so it receives both at once. A ship sends its own name,
type and destination, so unlike aircraft nothing needs looking up. The HMI's Ships
app runs it only while the app is open, and the radio is silent meanwhile, as with
Aircraft.

It is for interest and is not for navigation. Small craft often send no AIS.

Tested so far against published messages and synthesised transmissions only: it
has not yet been within range of a real ship.

### What it needs

- **To be near the water.** VHF reaches about 20 to 40 nautical miles from a ship
  to an antenna near the ground, so inland it will just say "Listening".
- **An antenna for 162 MHz.** A quarter-wave whip is about 44 cm. An FM antenna is
  closer to this than to 1090 MHz, and works for ships nearby.
- **A GPS fix,** for distance, bearing and the scope.

### How it is used

As Aircraft, with ranges of 5, 10, 25 and 50 nautical miles.

A ship under way reports every few seconds, one at anchor only every 3 minutes,
and a name comes only every 6. So a moored ship can take a few minutes to appear,
and a little longer to have a name. Ships stay listed for 10 minutes after they
were last heard, dimmed once their position is 5 minutes old.

### How it works

```text
dongle  288 kS/s I/Q, midway between the two channels, automatic gain
  |  for each channel: mix to zero, filter, / 6
48 kHz  discriminator -> 5 samples a bit
  |  slice, following a slow average for a tuner a little off frequency
  |  read at all five phases of a bit, each its own decoder
  |  NRZI, bit stuffing, HDLC flags, 16-bit frame check
frames -> position, speed and course; name, type and destination
```

Any phase whose frame passes the check wins, so there is no clock to recover
from a burst only 26 ms long. A frame must also be the exact length its type calls
for, and a ship needs two messages to be shown. Between them, noise does not put
anything on the screen.

The lines it prints are at the top of [`src/ais_main.cpp`](src/ais_main.cpp).
`--gain <tenths of a dB>` replaces the tuner's automatic gain.

## Weather Radio

`godwit-wxradio` receives the spoken forecasts of NOAA Weather Radio, on the seven
channels from 162.400 to 162.550 MHz. The HMI's Weather Radio app runs it only while
the app is open, and the radio is silent meanwhile, as with Aircraft. Music pauses
while it plays and comes back afterwards.

It is for interest and convenience, and it is not an alerting system. You hear an
alert only if the station speaks it while the app is open. Alert codes are not
decoded and nothing listens in the background, so do not rely on it for warnings.

Godwit is not affiliated with or endorsed by NOAA or the National Weather Service.

Tested so far against synthesised transmissions only: it has not yet heard a real
station.

### What it needs

- **To be near a transmitter.** The National Weather Service gives about 40 miles,
  less among hills and trees and more over salt water.
- **An antenna for 162 MHz.** The same as for Ships: a quarter-wave whip is about
  44 cm, and an FM antenna is close.
- **A GPS fix,** to name the nearest transmitter on each channel. Without one the
  channels still play, with their frequencies and strengths.

### How it is used

- **First use:** it goes to the strongest channel. After that it stays on the one
  chosen, in `/var/lib/godwit/weatherradio.json`.
- **Right knob:** turn to change channel, press for the strongest.
- **Left knob:** volume. Press to mute.
- **Screen:** tap a channel to hear it, or **Strongest**.

### How it works

```text
dongle  288 kS/s I/Q, midway between two channels, automatic gain
  |-- mix the channel to zero, / 6
  |   48 kHz: channel filter, discriminator, de-emphasis, 5 kHz low pass -> stdout
  '-- 1024-point FFT, 10 a second, 8 averaged -> the strength of all seven
```

All seven fit in one tuning, so the strength of every channel is always known and
changing channel only moves a mixer. The tuner sits between two channels, which
keeps its DC spike out of both. The mixer follows the carrier, since a dongle with
no temperature-compensated crystal can be some 5 kHz out at this frequency.

The audio is silent unless the channel has a steady carrier with speech on it lately.
Noise is not steady, and a spur from the board carries nothing. The threshold is set
just above noise, so a faint station is heard with hiss rather than not at all. Speech is de-emphasised
at 750 µs, the usual figure for two-way radio: transmitters are specified as rising
6 dB an octave, with no time constant published.

The broadcast does not say which transmitter it is, so the app can only say which is
nearest on the channel. [`data/nwr-stations.tsv`](data/nwr-stations.tsv) is on the
head unit and nothing about this uses the internet. It is a dated snapshot of
NOAA's list, so it can be out of date. To refresh it, download
<https://www.weather.gov/source/nwr/JS/ccl-data.js> and run
[`tools/pack_nwr_stations.py`](tools/pack_nwr_stations.py).

The commands it takes and the lines it prints are at the top of
[`src/wxradio_main.cpp`](src/wxradio_main.cpp). `--gain <tenths of a dB>` replaces the
tuner's automatic gain.

## Building and testing

The image builds them all with the `godwit-radio` recipe. On a PC, the DSP and its
tests need only a compiler. One test synthesises a complete FM broadcast, with
stereo, pilot and RDS, and checks what the receiver makes of it. The others check
the ADS-B and AIS decoders against published messages, then transmit those in I/Q
at every phase of a bit, in noise. Another transmits Weather Radio on all seven
channels and checks the station table:

```sh
cmake -S radio -B /tmp/radio-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/radio-build
ctest --test-dir /tmp/radio-build --output-on-failure
```

`godwit-radio`, `godwit-adsb`, `godwit-ais` and `godwit-wxradio` are also built where librtlsdr is installed. To
try the radio from a shell (`pw-play` as root on the head unit needs `PIPEWIRE_RUNTIME_DIR=/run/pipewire`):

```sh
godwit-radio --freq 97500000 | pw-play --raw --rate 57000 --channels 2 --format s16 -
```

The commands it takes and the lines it prints are at the top of
[`src/main.cpp`](src/main.cpp). `--gain <tenths of a dB>` replaces the tuner's
automatic gain.

## Licence

The code here is MIT. The programs link librtlsdr, which is GPL-2.0-or-later,
so the `godwit-radio`, `godwit-adsb`, `godwit-ais` and `godwit-wxradio` binaries are distributed on those terms.
They run as their own processes, so the HMI is not affected.
