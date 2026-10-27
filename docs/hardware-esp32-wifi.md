# Hardware Guide: ESP32 with Wi-Fi

A standard ESP32 development board, such as an ESP32 DevKit or NodeMCU-32, connects over Wi-Fi. It is easy to get and has a USB port, so the first install needs no extra adapter.

Configuration: [`cc1101-sniffer-esp32-wifi.yaml`](cc1101-sniffer-esp32-wifi.yaml)

## What you need

- An ESP32 development board with the classic ESP32 chip (board `esp32dev`)
- A CC1101 module made for 433 MHz, with an antenna
- A USB cable and a USB power supply
- Eight jumper wires

## Wiring

| CC1101 | ESP32 | Note |
| --- | --- | --- |
| VCC | 3V3 | Never 5 V, the CC1101 is a 3.3 V chip |
| GND | GND |  |
| SCK | GPIO14 |  |
| MOSI (SI) | GPIO15 |  |
| MISO (SO/GDO1) | GPIO35 | Input only, which is fine for MISO |
| CSN | GPIO4 |  |
| GDO2 (data) | GPIO36 | Often labelled VP or SVP; input only; the received pulses arrive here |
| GDO0 | GPIO32 | Not used yet, reserved for a transmitter |

The pins are set under `substitutions` at the top of the configuration.

## Install

1. Wire the CC1101 as in the table above.
2. Add `wifi_ssid` and `wifi_password` to your ESPHome secrets.
3. In ESPHome Device Builder, create a new device and replace its YAML with the configuration above. Change `name` and `timezone` if you like.
4. For the first install, connect the board with USB. Later installs go over the network.
5. Open `http://cc1101-sniffer-wifi.local/`, or the name you chose, and check the boot log: it should show the CC1101 with a chip ID and no error.

## Tips and tricks

- **Keep Wi-Fi awake.** The configuration sets `power_save_mode: none`. With Wi-Fi power saving, the interrupt on GPIO36 can trigger falsely, so keep this setting.
- **Antenna.** Use the antenna that came with the module, or a straight wire of about 17 cm, a quarter of the wavelength at 433.92 MHz. Keep it straight and away from the ESP32 and its Wi-Fi antenna.
- **Placement.** Place the receiver high and central rather than in a cabinet. Use `signal_grade` and `rssi_dbm` in the log to compare places.
- **Module band.** CC1101 modules are sold for 315, 433 and 868 MHz, and the parts around the chip are matched to one band. A module for the wrong band hears much worse.
- **Short wires.** Keep the SPI wires short; long loose wires can make the CC1101 fail to start.
- **Other ESP32 chips.** The configuration is made for the classic ESP32. Chips such as the ESP32-C3 or ESP32-S3 have other pins and less receive memory, so pins and `rx_symbols` need changing.
- **Nothing received.** If Frames stays at 0, check the boot log and the wiring first. See [Quick help](reference-guide.md#quick-help) in the Reference Guide.

All settings are explained in the [Reference Guide](reference-guide.md).
