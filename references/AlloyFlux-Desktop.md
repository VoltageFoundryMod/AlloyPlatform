# Desktop Alloy Flux

This doc evaluates the gaps and needs to create a desktop version of the Alloy Flux firmware and hardware, which can be used as a standalone software synth controlled by MIDI controller and/or CV or computer via app or DAW.

## Hardware

- Power supply - Figure this out since the desktop might have battery or only USB power and we need -10V for references.
- Headphone/line output with volume control (output amp) and 3.5mm stereo jack
- Expose the mappable CV Input
- MIDI Out/Thru (TRS)

## Firmware

## Software

- Desktop Web Configurator (Electron)
- VST3 Plugin to control module's parameters from DAW via CC

## Already implemented

- Oscillator Voice
- Filter Section
- Envelope Section
- VCA
- Effects (Delay, Reverb, Chorus)

## To do

Where to plug external 5V? Input of the AP63205?
How to handle CV inputs without -10V

- Charge pump IC (e.g. LM2662 or ICL7660) — generates −5V from +5V, no inductors, tiny footprint. Not −10V but enough to bias op-amps for ±2.5V CV range, which covers most desktop use.
- LT1054 — similar charge pump but gets you closer to −9V from a 5V input, which is enough to run the −10V reference and V/OCT circuit at reduced range.
- Desktop mode firmware bypass — just disable the V/OCT ADC path in the DESKTOP_EDITION build and accept that pitch tracking only comes from MIDI. No hardware change needed for that use case.
