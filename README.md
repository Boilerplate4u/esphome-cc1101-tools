# esphome-cc1101-tools

ESPHome CC1101 tools for tuning the transceiver and decoders, dumping raw signals and printing decoded protocol data.

## Overview

Use this ECC1101 tool to troubleshoot sub-GHz like 433 and 800 Mhz devices with ESPHome and print their protocol data, either raw or decoded. The output is the basis for a dedicated ESPHome configuration, for example to create Läggentities in Home Assistant.

CC1101 and decoder settings can be changed at runtime in the device web UI. When a device is received reliably, copy the values into your own configuration. The defaults are meant as a good starting point.

All settings are internal entities: they are shown in the web UI only and are never exposed to Home Assistant.

Runs on the WT32-ETH01 with Ethernet and on ordinary ESP32 boards with WiFi. The main configuration `cc1101-dumper.yaml` is for the WT32-ETH01; other boards are listed under Hardware config examples.

## Status

| Part | Status |
| --- | --- |
| Raw dump | Available |
| CC1101 runtime tuning | Available |
| Decoded output (Oregon v2.1, Nexa, rc\_switch) | Planned |

## Hardware

An ESP32 board and a CC1101 module. The CC1101 covers 300 to 928 MHz, including the 315, 433, 868 and 915 MHz ISM bands. The examples use 433.92 MHz; change `frequency` under `cc1101:` for another band. The wiring for each board is listed under Hardware config examples.

Use a module matched for the band you receive: the chip covers all bands, but the module's antenna circuit is tuned for one, and sensitivity drops on the others. A straight quarter-wave wire is a good antenna: 23.8 cm for 315 MHz, 17.3 cm for 433 MHz, 8.6 cm for 868 MHz and 8.2 cm for 915 MHz.

## Quick start

1. Add `cc1101-dumper.yaml`, or the example for your board under Hardware config examples, in ESPHome Device Builder and install it.
2. To change settings och follow to trace/debug log online, open the device web UI at `http://<device name>.local/`.
3. You don't need to add the device in Home Assistant.&#32;

Requires ESPHome 2026.10.0 or later (tested with 2026.10.0).

## Hardware Settings

### CC1101 radio

| Setting | Default | Effect |
| --- | --- | --- |
| RX filter bandwidth kHz | 203 | Lower gives less noise; too low loses transmitters with frequency drift |
| RX attenuation | 0dB | Fixed attenuation, only useful for transmitters close by |
| OOK decision boundary | 8dB | Higher gives less noise and less range |
| AGC hysteresis | Medium | Higher makes the AGC react less to small level changes |
| Max LNA gain | Default | Lower gives less noise and less range |
| Max DVGA gain | -3 | -3 is the lowest gain; higher increases sensitivity and noise |
| AGC target amplitude | 42dB | Lower amplifies noise less |

### Raw dump

| Setting | Default | Effect |
| --- | --- | --- |
| Raw log | On | Turns the raw frame log on and off |
| Min pulses per frame | 60 | Shorter frames are not logged |
| RX filter | 100 µs | Pulses shorter than this are dropped; keep below the shortest pulse of your devices |
| RX tolerance | 35 % | Timing tolerance for the built-in ESPHome decoders |

### Decoders (planned)

| Setting | Default | Effect |
| --- | --- | --- |
| Max repeat wait | 500 ms | Identical frames within this time are printed once with a repeat counter |
| Accept bit pair errors | Off | Oregon v2.1: also print frames with a valid checksum but bit pair errors, marked as uncertain |
| Min preamble | 24 pulses | Oregon v2.1: lower accepts frames with a damaged preamble, but increases false matches |

## Raw dump

Every received frame with at least *Min pulses per frame* pulses is logged:

```
Raw frame, 228 pulses, t=1234.567 s, gap=812 ms:
  1024, -981, 964, -964, ...
```

- Positive values are marks (carrier on), negative values are spaces, in µs.
- `t` is the uptime, `gap` the time since the previous frame, including frames filtered out.
- The last value is the `idle` timeout (5000 µs), not a real pause.

## \
\
Tuning

Change one setting at a time and compare the number of noise frames with the number of complete frames.

- **Less noise:** lower the bandwidth, raise the OOK decision boundary, lower Max LNA gain and AGC target amplitude.
- **Weak transmitter:** check antenna and module frequency first, then raise Max DVGA gain and lower the OOK decision boundary.

## Limitations

- `idle`, `rmt_symbols` and `receive_symbols` can only be changed at compile time.
- Logging many frames can block the main loop (`remote_receiver took a long time`). Turn off *Raw log* or raise *Min pulses per frame* when it happens.

## License

MIT

## Hardware config examples

### WT32-ETH01 (Ethernet)

`cc1101-dumper.yaml` (main configuration)

| CC1101 | WT32-ETH01 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SCK | IO14 |
| MOSI (SI) | IO15 |
| MISO (SO) | IO35 |
| CSN | IO4 |
| GDO2 (RX data) | IO36 |
| GDO0 (reserved for TX) | CFG (IO32) |

### ESP32 with WiFi

`hardware-config-examples/cc1101-dumper-esp32-wifi.yaml`

| CC1101 | ESP32 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SCK | GPIO14 |
| MOSI (SI) | GPIO15 |
| MISO (SO) | GPIO35 |
| CSN | GPIO4 |
| GDO2 (RX data) | GPIO36 (VP) |
| GDO0 (reserved for TX) | GPIO32 |
