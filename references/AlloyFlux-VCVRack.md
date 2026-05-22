# AlloyFlux for VCV Rack

AlloyFlux is a versatile multi-voice oscillator module for VCV Rack, inspired by classic analog textures and modern digital features. This repository contains the firmware for the physical module, as well as a web-based configurator for patch management and MIDI control.

## Features

- 4-voice polyphony with unison and detune
- Multiple voice modes: PAIR, CLOUD, CHORD, CASCADE, STRING, POLY
- Real-time control over parameters via MIDI CC and USB serial console
- Preset management with 10 flash slots and SysEx patch dump/restore
- Web-based configurator for patch editing, MIDI mapping, and console monitoring
- Signal flow visualizer and interactive MIDI keyboard in the web configurator

## Documentation

- [User Manual](AlloyFlux-user-manual.md): Panel controls, voice modes, MIDI CC map
- [Serial Reference](AlloyFlux-serial-reference.md): USB serial console commands and configuration
- [Web Configurator](AlloyFlux-VCVRack.md): Instructions for using the browser-based editor to manage patches and control the module

## Development

To build the plugin, make sure you have the [VCV Rack SDK](https://vcvrack.com/downloads) unpacked and exported as `RACK_DIR`. If using Windows, ensure you install the recommended build tools like MSYS2 and required packages as instructed in the Rack plugin [development documentation](https://vcvrack.com/manual/Building).

```bash
export RACK_DIR=/path/to/Rack-SDK
```

To build the plugin, run:

```bash
make
```

To install the plugin to your Rack plugins directory:

```bash
make install
```
