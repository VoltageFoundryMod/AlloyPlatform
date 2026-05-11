# Alloy Flux Command Reference

This document lists the commands available in the serial console, MIDI Mapping and I2C commands which control the module parameters.

## Module Controls

- Modular Level V/Oct and Gate controls the relative pitch and on/off state of the voice.
- MIDI Controls - The Module accepts MIDI note and control change messages over USB and DIN MIDI. See MIDI Mapping below for details. When using MIDI, the user can control up to 4 voices with independent pitch and gate, and also control the timbre and level of the output. Each voice is monophonic, so if more than 4 notes are played, the oldest note will be dropped.
- Serial Console - The module also has a serial console which can be accessed over USB. This allows the user to control all parameters of the module, including voice pitch, timbre, and level, as well as access some additional features like V/Oct calibration and performance testing. See Serial Console Commands below for details.
- I2C Commands - The module also supports control over I2C, which allows for more advanced control and integration with other devices like Monome ecosystem (Teletype). See I2C Commands below for details.


## Summary of Controls

| Control Type | MIDI        | I2C | Serial  | Description                                                                                                      |
| ------------ | ----------- | --- | ------- | ---------------------------------------------------------------------------------------------------------------- |
| V/Oct + Gate | Note On/Off | ?   | `pitch` | Control the pitch and on/off state of the voice. MIDI allows for up to 4 voices with independent pitch and gate. |

## MIDI Mapping

## I2C Commands

## Serial Console Commands
