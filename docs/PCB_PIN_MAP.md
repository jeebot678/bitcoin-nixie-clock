# Rotary selector mapping

Verified from the PCB-design session `01a0840e-b8a8-7301-8cc7-f94e807b47d9`,
linked by the user as `codex://threads/01a0840e-b8a8-7301-8cc7-f94e807b47d9`.
The session's `LED_Nixie_Controller_Rev20_Finalization/electronics/Controller_Rev20.brd`
and `Controller_Rev20.sch` agree:

| Control | PCB connection | Filtered net | ESP32 U7 pad | GPIO |
| --- | --- | --- | --- | --- |
| Range | JSEL1 common, through R1ADC | SEL1_ADC / C1ADC | J2_5 / IO34 | 34 |
| Frequency | JSEL2 common, through R2ADC | SEL2_ADC / C2ADC | J2_6 / IO35 | 35 |

The original board only names the selectors SEL1 and SEL2. Firmware 1.0.6 swaps
the functions to match the user's assembled controls: JSEL1 is range; JSEL2 is
frequency. The fresh assembled-device sweep for firmware 1.0.12 confirmed
these GPIO roles: range moved first on GPIO34, then frequency on GPIO35.

The user's Rev20 `Component_Installation_Guide.html` independently confirms
10 kΩ pull-ups, six grounded position tracks (0 Ω, 1.8 kΩ, 3.9 kΩ, 6.8 kΩ,
12 kΩ, 22 kΩ), and each common-to-ADC connection through 1 kΩ with a 100 nF
filter. The manufactured board source named there is
`Controller_Production_Candidate.brd` (SHA-256
`bdb7042236a56f207aaa09973c254fda8605efadfd1a4155382a34c518afdddc`).

The switch's mechanical order is independent of PCB position numbering.
The fresh lowest-to-highest sweep measured both switches as **POS5 → POS1**:

| Physical position | PCB track / resistor | Frequency / GPIO35 | Range / GPIO34 | Selected frequency | Selected range |
| --- | --- | --- | --- | --- | --- |
| Lowest | POS5 / 12 kΩ | 1774 mV | 1775 mV | 0.5 s | 5 min |
| Second | POS4 / 6.8 kΩ | 1328 mV | 1328 mV | 2 s | 30 min |
| Third | POS3 / 3.9 kΩ | 915 mV | 909 mV | 30 s | 1 h |
| Fourth | POS2 / 1.8 kΩ | 490 mV | 493 mV | 5 min | 24 h |
| Highest | POS1 / 0 Ω | 142 mV, raw 0 | 142 mV, raw 0 | 1 h | 7 d |

Independent calibration arrays follow this physical order. The grounded track
keeps an explicit zero marker so its low-voltage band handles the observed
calibrated floor without assuming that ground is the first setting. POS6
(22 kΩ, nominal 2269 mV) and open/high readings are invalid and retain the
last stable setting. COMMON is the wiper; the other pole of a two-pole selector
is unused. These are passive selectors; do not supply an external voltage.

Only samples from the newly requested session were used. See
[the captured calibration report](../test/dial_calibration_report.json).

Source SHA-256 values recorded during verification:

- `Controller_Rev20.sch`: `48c6597cc9dfa7af5819a225de6bc3bed1c73f9ad0f781e97ec4139443005f32`
- `Controller_Rev20.brd`: `0f9d5e4661ced6c504b683d3fc8014f1bf5f167c80e838d7e9be81b36f53a5e9`
