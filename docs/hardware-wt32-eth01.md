# Hardware Guide: WT32-ETH01

The WT32-ETH01 is an ESP32 board with wired Ethernet. It is a good choice for these tools: with Ethernet there is no Wi-Fi radio to keep awake, and a network cable makes it easy to place the receiver where reception is best.

Configuration: [`cc1101-sniffer-wt32-eth01.yaml`](cc1101-sniffer-wt32-eth01.yaml)

## What you need

- A WT32-ETH01 board
- A CC1101 module made for 433 MHz, with an antenna
- A 5 V power supply for the WT32-ETH01
- A USB-to-serial adapter with 3.3 V logic for the first install, since the board has no USB port
- Eight jumper wires

## Wiring

| CC1101 | WT32-ETH01 | Note |
| --- | --- | --- |
| VCC | 3V3 | Never 5 V, the CC1101 is a 3.3 V chip |
| GND | GND |  |
| SCK | IO14 |  |
| MOSI (SI) | IO15 |  |
| MISO (SO/GDO1) | IO35 | Input only, which is fine for MISO |
| CSN | IO4 |  |
| GDO2 (data) | IO36 | Input only; the received pulses arrive here |
| GDO0 | IO32 (labelled CFG) | Not used yet, reserved for a transmitter |

The pins are set under `substitutions` at the top of the configuration. The pins that the Ethernet chip uses are left free.

## Install

1. Wire the CC1101 as in the table above.
2. In ESPHome Device Builder, create a new device and replace its YAML with the configuration above. Change `name` and `timezone` if you like.
3. For the first install, connect the USB-to-serial adapter to the TX, RX, GND and power pins of the WT32-ETH01, and hold IO0 to GND while the board powers up to start it in flash mode.
4. Install, remove the IO0 connection and restart the board with the Ethernet cable connected.
5. Open `http://cc1101-sniffer.local/`, or the name you chose, and check the boot log: it should show the CC1101 with a chip ID and no error.

Later installs go over the network.

## Tips and tricks

- **Antenna.** Use the antenna that came with the module, or a straight wire of about 17 cm, a quarter of the wavelength at 433.92 MHz. Keep it straight and away from the board, the Ethernet cable and metal.
- **Placement.** Place the receiver high and central rather than in a cabinet. Use `signal_grade` and `rssi_dbm` in the log to compare places.
- **Module band.** CC1101 modules are sold for 315, 433 and 868 MHz, and the parts around the chip are matched to one band. A module for the wrong band hears much worse.
- **Short wires.** Keep the SPI wires short; long loose wires can make the CC1101 fail to start.
- **Nothing received.** If Frames stays at 0, check the boot log and the wiring first. See [Quick help](reference-guide.md#quick-help) in the Reference Guide.
- **More than one receiver.** Give each one its own `name`, so they do not share the same address on the network.

All settings are explained in the [Reference Guide](reference-guide.md).
