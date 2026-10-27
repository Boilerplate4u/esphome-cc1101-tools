# Reference Guide

This guide explains every value in the README: what it is for, when and how to change it, and how to check the result. Start with Basics, look up a problem under Quick help, or jump to a value from the README.

Web UI values are changed at `http://<device name>.local/`, take effect at once and are kept after a restart. Compile-time values are changed in `cc1101-sniffer.yaml` and need a new install from ESPHome Device Builder. In the tables, the default value is shown in parentheses.

## Basics

The CC1101 is the radio receiver chip. It is connected to an ESP32 board, which runs the ESPHome firmware with these tools. ESPHome Device Builder is the ESPHome app in Home Assistant that builds the firmware and installs it on the board.

Most remote controls, door sensors and weather sensors on 433 MHz send data by switching a radio carrier, the radio wave itself, on and off. This is called OOK (on-off keying). The CC1101 turns that into a series of on and off times, called pulses. A burst of pulses with silence before and after is a capture. A decoder recognises the pulse pattern of a device type and turns it into values.

| Word | Meaning |
| --- | --- |
| Pulse | One on or off period, measured in µs (microseconds, millionths of a second) |
| Capture | One burst of pulses, usually one message. The web UI calls it a frame, as in Decoded frames |
| Copy | Most devices send the same message several times in a row |
| Symbol | The unit the ESP32 receive buffer counts in; one symbol holds two pulses |
| Checksum | A number in a message, calculated from the rest of it, that shows whether the message arrived undamaged |
| Gain | How much an amplifier strengthens the signal |
| dBm (decibel-milliwatts) | Absolute signal level; -60 dBm is stronger than -80 dBm |
| dB (decibel) | Difference between two levels, for example signal above noise |
| Noise floor | The background level when nobody sends |

### Where to find the log

Everything the tools find is written to the log. It is shown in the web UI and under Logs for the device in ESPHome Device Builder. The boot log is the part written right after the device starts; it shows whether the CC1101 was found.

An example line from an Oregon sensor, after the prefix ESPHome adds:

```
14:05:12.345 - oregon THGR122N/THGR228N type=1D20 channel=2 unit_id=7B temp=21.5 hum=45 battery=ok short_us=488 long_us=975 pair_errors=0 signal_grade=4 (snr_db=20, rssi_dbm=-63) rep=2 span_ms=241 max_gap_ms=241
```

It shows the time the transmission began, the decoder (`oregon`), the decoded values, the signal and how many copies arrived. A capture no decoder recognises gives a line like `14:05:20.101 - unknown pulses=130 duration_ms=76 truncated=no` followed by the signal. The fields are explained under [Log fields](#log-fields) and [Signal](#radio-signal-levels).

### Tuning in five steps

1. Install with the default values and let the device run for a few minutes.
2. Trigger your device and find its line in the log. No line at all: see Quick help.
3. Check `signal_grade`. 3 or higher is fine; lower means the receiver needs a better place or other settings.
4. Change one value at a time, press Reset statistics and compare Decoded frames, Unknown frames and `signal_grade` after a few minutes.
5. Copy the working values to your own ESPHome configuration.

To start over, set Radio profile to Standard and set the other values back to the defaults shown in parentheses. Radio profile only covers five radio values, not the Decoders, Oregon v2.1 decoder and Raw log values.

## Quick help

The values named here are explained in the sections further down.

| Symptom | Try first | Then |
| --- | --- | --- |
| Frames stays at 0 | Check the boot log for the CC1101 and its chip ID ([compile-time values](#values-that-can-only-be-set-at-compile-time)) | Check the wiring and the pins against the header of your board configuration ([WT32-ETH01](cc1101-sniffer-wt32-eth01.yaml) or [ESP32 with Wi-Fi](cc1101-sniffer-esp32-wifi.yaml)) |
| No line at all when the device sends | Set Raw log to All and check that captures arrive ([Raw log](#raw-log)) | Radio profile Sensitive, check `frequency` ([CC1101 Radio and receiver](#cc1101-radio-and-receiver-settings), [compile-time values](#values-that-can-only-be-set-at-compile-time)) |
| The device shows up as `unknown` | RX tolerance higher ([CC1101 Radio and receiver](#cc1101-radio-and-receiver-settings)) | Check that a decoder exists for it ([Decoders](#decoders)); study it with Raw log Unknown only |
| Many false or broken captures | Radio profile Noise hunting ([CC1101 Radio and receiver](#cc1101-radio-and-receiver-settings)) | Max DVGA gain lower, OOK decision boundary higher |
| `signal_grade` 1 or 2 | Move the receiver or the antenna | Radio profile Sensitive |
| One transmission gives several lines with `rep=1` | Max repeat wait higher ([Decoders](#decoders)) |  |
| `truncated=yes` | The device sends longer captures than the buffer holds | `rx_symbols` ([compile-time values](#values-that-can-only-be-set-at-compile-time)) |
| Many `oregon rejected` lines | Better signal first | Oregon timing ([Oregon v2.1 decoder](#oregon-v21-decoder)) |
| Times show `up <seconds>` | The clock is not synchronised yet | Check the network and `timezone` |

## CC1101 Radio and receiver settings

These values decide what the CC1101 hears. They are in the Radio and receiver group of the web UI. The receiver has two amplifier stages, the LNA (low-noise amplifier) and the DVGA (digital variable gain amplifier), and an AGC (automatic gain control) that sets their gain for each signal. More gain hears weaker devices but also more noise, so tuning is a balance. The most useful values come first; the last four rarely need changing.

| Value | Range, steps or options (default) | What it does |
| --- | --- | --- |
| Radio profile | Standard, Sensitive, Noise hunting, Custom (Standard) | Sets Max DVGA gain, OOK decision boundary, RX filter bandwidth, RX filter and RX tolerance in one step |
| Max DVGA gain | Default, -1, -2, -3 (-3) | Limits the gain of the second amplifier stage; -3 removes the three highest gain steps, Default allows full gain |
| OOK decision boundary | 4, 8, 12, 16 dB (8 dB) | How much stronger than the background a signal must be to count as on |
| RX (receive) filter bandwidth | 58 to 812 kHz (kilohertz) in 16 steps (203 kHz) | How wide a frequency band the receiver listens to |
| RX filter | 50 to 180 µs, steps of 10 (100 µs) | Pulses shorter than this are treated as noise spikes and merged into the pulse around them |
| RX tolerance | 15 to 50 %, steps of 1 (35 %) | How much a device's pulse timing may differ from the standard and still be decoded |
| RX attenuation | 0, 6, 12, 18 dB (0 dB) | Weakens the incoming signal for all devices |
| Max LNA gain | Default down to -17.1 dB in 8 steps (Default) | Limits the gain of the first amplifier stage |
| AGC target amplitude | 24 to 42 dB in 8 steps (42 dB) | The signal level the gain control aims for |
| AGC hysteresis | None, Low, Medium, High (Medium) | How large a change in level the gain control waits for before it adjusts |

### When to change them

#### Radio profile

Start with Standard. Try Sensitive when devices are missed and Noise hunting when the log fills with false or broken captures. Changing one of the five values yourself switches the profile to Custom. Check: press Reset statistics, wait a few minutes and compare Decoded frames and Unknown frames with the previous profile.

#### Max DVGA gain

Move it towards -3 when Noise frames per minute is high, and towards Default when a distant device is missed, at the cost of more noise. Check: Noise frames per minute drops while `signal_grade` for your devices stays the same.

#### OOK decision boundary

A lower value hears weaker devices but turns more noise into pulses; a higher value gives cleaner captures from strong devices. Raise it when captures are broken by noise, lower it for weak devices. Check: compare Unknown frames and Noise frames per minute before and after.

#### RX filter bandwidth

Narrower lets in less noise, but a device whose frequency is slightly off is no longer heard, and cheap 433 MHz devices often are. Narrow it one step at a time while your device still decodes, and go back one step when it disappears. Check: the device keeps decoding and `snr_db` rises.

#### RX filter

A higher value removes more noise but must stay below the shortest real pulse your device sends, or its captures break. Raise it when the raw log shows many very short pulses. Check: the device still decodes and Unknown frames drops.

#### RX tolerance

Applies to the ESPHome decoders; the Oregon v2.1 decoder has its own timing. Higher accepts devices with poor timing but gives more false decodes. Raise it when a known device shows up as `unknown`. Check: the device decodes and no unexpected decoder lines appear.

#### RX attenuation

For a device so close that it overloads the receiver. Use it only when captures from a nearby device break even though `signal_grade` is 5, since it lowers sensitivity for all devices. Check: the nearby device decodes again and distant devices are still heard.

#### Max LNA gain

Also protects against overload from strong nearby devices and reduces range like RX attenuation does. Keep Default unless strong signals break. Check: as for RX attenuation.

#### AGC target amplitude

A lower target gives less gain overall, which can help in a place with strong interference; 42 dB gives the most sensitivity. Check: Noise frames per minute and `signal_grade`.

#### AGC hysteresis

Higher keeps the gain steadier during a message; lower follows changing signals faster but also follows noise. Rarely needs changing. Check: compare broken captures for a device whose level varies.

## Decoders

Every capture is offered to the decoders in turn, Oregon v2.1 first and rc\_switch last. The first one that recognises the pattern writes a line with the decoded values; if none does, the capture is logged as `unknown`. A transmission that repeats the same message gives one line with a repeat count. These values are in the Decoders group of the web UI.

| Value | Range, steps or options (default) | What it does |
| --- | --- | --- |
| Max repeat wait | 100 to 2000 ms (milliseconds), steps of 50 (500 ms) | How long to wait for another copy before the line is written, so repeated copies become one line |
| Decoder switches | On or off, one per decoder (On) | Shows or hides each decoder's lines, so the log only shows what you are looking for |

### When to change them

#### Max repeat wait

A higher value merges copies with long pauses but delays the line; a lower value logs faster but can split one transmission into several lines. Raise it when one transmission shows up as several lines with `rep=1`. Check: the device gives one line with the expected `rep`.

#### Decoder switches

A hidden decoder still recognises its captures, so they are not counted or dumped as unknown. Drayton and rc\_switch are the exception: without a checksum they can mistake other signals for their own, so they run only when shown and need two identical copies. Check: the hidden decoder's lines are gone and Unknown frames does not rise.

### Which decoder fits your device

If you do not know which decoder fits, leave all of them on and trigger the device: the line starts with the name of the decoder that recognised it. The decoders and what ESPHome's documentation says they decode:

| Decoder | Decodes | Note |
| --- | --- | --- |
| Oregon v2.1 | Oregon Scientific v2.1 sensors | Built into these tools |
| Nexa | Nexa RF codes |  |
| KeeLoq | KeeLoq (HCS301) RF codes |  |
| Dooya | Dooya RF codes |  |
| Drayton Digistat | Drayton Digistat RF codes | No checksum |
| Byron SX | Byron SX doorbell RF codes |  |
| Brennenstuhl | Brennenstuhl RF codes |  |
| Nexus | Nexus temperature and humidity sensor RF codes | ESPHome dev only |
| A-OK | A-OK RF codes | ESPHome dev only |
| Cardin S438 | Cardin S438 RF codes | ESPHome dev only, not yet in the documentation |
| rc\_switch | RCSwitch RF codes, used by many simple fixed-code remotes | No checksum |

## Oregon v2.1 decoder

Oregon v2.1 sensors use a coding that ESPHome does not decode, so these tools have their own decoder with adjustable timing. These values are in the Oregon v2.1 decoder group of the web UI. A good message has short pulses near 488 µs and long pulses near 976 µs; `short_us` and `long_us` in each line show what your sensor sends. The four time values must satisfy Min pulse < Long pulse from < Max pulse < End gap, otherwise a change is ignored and a warning is logged.

| Value | Range, steps or options (default) | What it does |
| --- | --- | --- |
| Verify bit pairs | On or off (On) | Rejects damaged messages by checking each bit against its inverted copy; Oregon v2.1 sends every bit twice, the second time inverted |
| Min pulse | 100 to 400 µs, steps of 10 (200 µs) | The shortest pulse that counts as part of a message; shorter ones are noise |
| Long pulse from | 500 to 900 µs, steps of 10 (700 µs) | Where short pulses end and long pulses begin |
| Max pulse | 1000 to 2000 µs, steps of 10 (1200 µs) | The longest pulse that counts as part of a message; longer ones are noise |
| End gap | 1500 to 5000 µs, steps of 100 (2500 µs) | A pause this long ends a message |
| Min preamble | 8 to 64 pulses, steps of 1 (20 pulses) | How many long pulses a message must start with; this start sequence is called the preamble |

### When to change them

#### Verify bit pairs

Turn it off only to see what a very weak sensor sends. The checksum still applies, but wrong values become more likely. Check: Rejected Oregon frames and the `pair_errors` field.

#### Min pulse

Lower it only if `short_us` comes close to 200 µs. Check: the sensor decodes and `pair_errors` stays 0.

#### Long pulse from

Move it only for a sensor whose `short_us` or `long_us` is far from 488 and 976 µs, so the boundary stays between them. Check: the sensor decodes and `pair_errors` stays 0.

#### Max pulse

Raise it only if `long_us` comes close to 1200 µs. Check: the sensor decodes.

#### End gap

Lower ends messages sooner when noise follows them; higher lets a message with a short break continue. Check: Rejected Oregon frames with reason `too_short` should not rise.

#### Min preamble

Lower catches sensors with a short preamble but starts more false messages, because noise can look like a preamble. Lower it when a sensor that is seen in the raw log is never decoded. Check: the sensor decodes and Rejected Oregon frames does not rise.

## Raw log

The raw log shows a capture exactly as received: every pulse width in µs, positive for on and negative for off. Use it to study a device that no decoder knows, or to see why a known device fails. Raw lines use the tag `rf_raw`. These values are in the Raw log group of the web UI.

| Value | Range, steps or options (default) | What it does |
| --- | --- | --- |
| Raw log | Off, Unknown only, All (Off) | Writes the received pulses to the log; Unknown only writes captures no decoder accepted, All writes every capture |
| Min pulses per frame | 0 to 896, steps of 1 (60) | Captures with fewer pulses count as noise: they are neither dumped nor counted as unknown |

### When to change them

#### Raw log

Unknown only is the right choice for a new device. All produces a lot of output and can slow the device, so use it only for short tests. For that reason it is always Off after a restart. Check: `rf_raw` lines appear when the device sends.

#### Min pulses per frame

Lower it to see devices with very short messages; raise it when the log fills with short noise captures. Check: Noise frames per minute and Unknown frames change as expected.

### Build your own decoder using the raw log

If none of the built-in decoders recognises your device, the raw log is the starting point for a decoder of your own, the way the Oregon v2.1 decoder in these tools was made.

1. Set Raw log to Unknown only and let it run for a few minutes while the device sends.
2. Note what the device sent each time: the button you pressed, or the values on its display, with the time.
3. Repeat with different buttons or values, so each change in the data can be matched to a change in the pulses.
4. Save the whole log and give it, together with your notes and the log format in the README, to your favourite AI assistant or LLM (large language model) for analysis.
5. Test the decoder it suggests against new captures before you rely on it.
6. To use the decoder in your own ESPHome configuration, add it as an [external component](https://esphome.io/components/external_components/).

A raw log of a remote for a gate, garage door or alarm can be enough to copy that remote. Do not share such logs publicly.

## Statistics

The statistics show how reception works over time and are updated every 60 s. They are in the Statistics group of the web UI. They cannot be changed; use them to compare settings. Press Reset statistics, change one value and compare after a few minutes.

| Value | Type | Description |
| --- | --- | --- |
| Frames | Counter | All captures since boot or the last reset, noise included. Shows that the receiver hears anything at all. |
| Decoded frames | Counter | Captures a decoder accepted. It should rise each time your devices send. |
| Unknown frames | Counter | Captures of at least Min pulses per frame that no decoder accepted. A steady rise means an undecoded device or broken captures; set Raw log to Unknown only to see them. |
| Rejected Oregon frames | Counter | Oregon messages that were found but failed their checks. A rise points to a weak or disturbed Oregon sensor; the `oregon rejected` lines show why. |
| Noise frames per minute | Rate | Captures shorter than Min pulses per frame, per minute. A high value means a noisy place or too much gain. |
| Noise floor | dBm | The background level while nobody sends, measured once a second when the receiver is quiet. A more negative value is better. It is the reference for `snr_db` and `signal_grade`. |
| Decoded per protocol | Text | Accepted captures per decoder, to see which devices are heard and how often. |
| Reset statistics | Button | Zeroes all counters, so a new setting can be compared from a clean start. |
| Restart device | Button | Restarts the ESP32 from the web UI, for example when it behaves oddly. Settings are kept; Raw log returns to Off. |

## Log fields

Each log line starts with the time the transmission began, then the decoder name and the decoded values. The fields below follow the values. You read them; they cannot be changed.

| Field | Unit or values | Description |
| --- | --- | --- |
| `rep` | Count | How many identical copies were merged into the line. If a device that repeats its messages always shows `rep=1`, Max repeat wait may be too short. |
| `span_ms` | ms | Time from the start of the first copy to the start of the last. |
| `max_gap_ms` | ms | The longest time between the starts of two copies. Max repeat wait must be longer than the pause between copies, which is somewhat shorter than this. |
| `short_us`, `long_us` | µs | Oregon v2.1 only: the average length of short and long pulses. Values far from 488 and 976 µs show a sensor with skewed timing. |
| `pair_errors` | Count | Oregon v2.1 only: bit pairs that did not match. Above 0 means a damaged message. |
| `reason` | `bad_checksum`, `pair_errors`, `bad_sync`, `bad_value`, `too_short`, `no_checksum` | Why an Oregon message was rejected: damaged data, damaged bits, wrong start, invalid values, cut short, or an unknown sensor type without a checksum the decoder can find. |
| `nibbles` | Hexadecimal | The content of a rejected message in hexadecimal, digits 0 to 9 and A to F; each digit, a nibble, is 4 bits. Identical rejected lines in a row point to a sensor the decoder does not know. |
| `pulses` | Count | Number of pulses in an `unknown` or raw capture. |
| `duration_ms` | ms | Length of the capture. |
| `gap_ms` | ms | Time since the previous capture started; repeated values show how often a device sends. |
| `truncated` | yes, no | yes means the capture filled the receive buffer and the end of the transmission is missing. |

## Radio signal levels

Three fields describe how well a transmission was received. They are the same signal expressed three ways, from raw measurement to a simple grade. Right after start only `rssi_dbm` is shown, until the first noise floor is measured.

| Field | Unit or values | Description |
| --- | --- | --- |
| `signal_grade` | 1 to 5 | A simple grade of how well the signal was received, from 5 (very clear) to 1 (barely). Aim for 3 or higher for devices you want to rely on. It is calculated from `snr_db` below; `rssi_dbm` is the measured signal strength behind it. |
| `rssi_dbm` | dBm | RSSI (received signal strength indicator): how strong the signal is, the strongest of up to four readings during the transmission. Closer to 0 is stronger; -60 is strong, -90 very weak. Use it to compare places or antennas for the same device. |
| `snr_db` | dB | SNR (signal-to-noise ratio): how far the signal is above the noise floor. This decides whether a device is received reliably, because the same `rssi_dbm` can be fine in a quiet place and useless in a noisy one. |

## Values that can only be set at compile time

These values are built into the firmware, so they cannot be changed while the device runs. They are set in the ESPHome configuration file, and the column **Set in** shows which block each value belongs to. Values under `substitutions` are named values defined once at the top of the file and used by the rest of it. After a change, install the firmware again and check the boot log. Most of them only need changing when your hardware or setup differs from the examples.

| Value | Set in | Range or options (default) | What it does |
| --- | --- | --- | --- |
| `name`, `friendly_name` | `substitutions` | Text (cc1101-sniffer, CC1101 Sniffer) | The name of the device in ESPHome Device Builder, on the network (`<name>.local`) and in the web UI |
| `board` | `substitutions` | ESP32 board name (wt32-eth01) | Tells ESPHome which ESP32 board you use, for example `wt32-eth01` or `esp32dev` |
| `frequency` | `substitutions` | CC1101 bands, for example 433.92 or 868.3 MHz (433.92 MHz) | The radio frequency the receiver listens on |
| `spi_clk_pin`, `spi_mosi_pin`, `spi_miso_pin`, `cc1101_cs_pin` | `substitutions` | GPIO (general purpose input/output) pins (see the wiring in [WT32-ETH01](cc1101-sniffer-wt32-eth01.yaml) or [ESP32 with Wi-Fi](cc1101-sniffer-esp32-wifi.yaml)) | The pins for the SPI (serial peripheral interface) bus that the ESP32 uses to configure the CC1101 |
| `rx_pin` | `substitutions` | GPIO pin (GPIO36 in [WT32-ETH01](cc1101-sniffer-wt32-eth01.yaml) and [ESP32 with Wi-Fi](cc1101-sniffer-esp32-wifi.yaml)) | The pin connected to the CC1101 data output (GDO2, general digital output 2), where the received pulses arrive |
| `activity_pin` | `rf_sniffer` | Same pin as `rx_pin` | Lets the firmware see when a capture is being received, so RSSI is read only then |
| `rx_symbols` | `substitutions` | 64 to 512, even numbers (448) | The size of the receive buffer; 448 symbols hold about 896 pulses |
| `idle` | `remote_receiver` | Time (5 ms) | How long the signal must be silent before a capture ends |
| `filter_symbols` | `remote_receiver` | Count (20) | Captures with fewer symbols than this are dropped as noise before any decoder sees them |
| `symbol_rate` | `cc1101` | Rate (5000) | A CC1101 data rate setting |
| `level` | `logger` | NONE to VERY\_VERBOSE (DEBUG) | How much the device writes to the log |
| `timezone` | `substitutions` | IANA (Internet Assigned Numbers Authority) time zone name (Europe/London) | The time zone for the times in the log, for example `Europe/London` or `America/New_York` |
| `update_interval` | `rf_sniffer` | Time (60 s) | How often the statistics are updated |

### When to change them

#### `name`, `friendly_name`

Change them when you run more than one receiver. The first install under a new name must go to the old address or over USB (universal serial bus). Check: the device answers at the new address.

#### `board`

It must match your board, or pins and network may not work. Check: the device boots and connects.

#### `frequency`

Set it to that of your devices and use a CC1101 module made for that band; a module tuned for another band hears much worse. Check: Frames rises when your devices send.

#### SPI pins

They must match your wiring. Check: the boot log shows the CC1101 with a chip ID and no error.

#### `rx_pin`

It must match your wiring to GDO2. Check: Frames rises when a device sends.

#### `activity_pin`

Keep it on the same pin as `rx_pin`. Without it only the noise floor is measured and the lines show no signal values. Check: lines show `signal_grade`.

#### `rx_symbols`

The ESP32 has 512 symbols in total for receiving and sending, so a larger value leaves too little for a future transmitter. Raise it only if your devices send longer captures and you will not transmit. Check: `truncated=yes` disappears.

#### `idle`

Longer joins copies that follow closely into one capture; shorter splits messages that contain long pauses. Rarely needs changing. Check: one capture per copy in the raw log.

#### `filter_symbols`

Raise it in a very noisy place; lower it for devices with very short messages. Check: Noise frames per minute.

#### `symbol_rate`

The tested configurations use 5000; leave it unless you know your receiver needs another value.

#### `logger` `level`

It must stay at DEBUG or higher to see decoded, unknown and raw lines; INFO shows only warnings and status messages.

#### `timezone`

Set it to your own. Check: the log times match your clock.

#### `update_interval`

Shorter reacts faster; longer gives steadier values for Noise frames per minute and Noise floor. Check: the statistics update at the new pace.

## Sources

ESPHome documentation, checked 2026-10-27: [Remote Receiver](https://esphome.io/components/remote_receiver/) (release, for the decoders and `filter_symbols`), [Remote Receiver](https://next.esphome.io/components/remote_receiver) (next, for the ESPHome dev decoders) and [External Components](https://esphome.io/components/external_components/).
