# Rotary selector mapping

Verified from the PCB-design session `01a0840e-b8a8-7301-8cc7-f94e807b47d9`,
linked by the user as `codex://threads/01a0840e-b8a8-7301-8cc7-f94e807b47d9`.
The session's `LED_Nixie_Controller_Rev20_Finalization/electronics/Controller_Rev20.brd`
and `Controller_Rev20.sch` agree:

| Control | PCB connection | Filtered net | ESP32 U7 pad | GPIO |
| --- | --- | --- | --- | --- |
| Frequency | JSEL1 common, through R1ADC | SEL1_ADC / C1ADC | J2_5 / IO34 | 34 |
| Range | JSEL2 common, through R2ADC | SEL2_ADC / C2ADC | J2_6 / IO35 | 35 |

The firmware assigns those functions deliberately; the original board only names
the selectors SEL1 and SEL2. Label/wire the controls accordingly. Contacts POS1
through POS5 select the settings in README order. COMMON is the wiper; the other
pole of a two-pole selector is unused. POS6 is not a setting in this firmware.

The board has a 10 kΩ pull-up to 3.3 V, position resistors of 0 Ω, 1.8 kΩ,
3.9 kΩ, 6.8 kΩ and 12 kΩ, then a 1 kΩ series resistor and 100 nF ADC filter.
Nominal detent voltages are 0, 503, 926, 1336 and 1800 mV. The independent
calibration arrays in `include/DeviceConfig.h` remain adjustable with `dials`.
These are passive selectors; do not supply an external voltage to the contacts.

Source SHA-256 values recorded during verification:

- `Controller_Rev20.sch`: `48c6597cc9dfa7af5819a225de6bc3bed1c73f9ad0f781e97ec4139443005f32`
- `Controller_Rev20.brd`: `0f9d5e4661ced6c504b683d3fc8014f1bf5f167c80e838d7e9be81b36f53a5e9`
