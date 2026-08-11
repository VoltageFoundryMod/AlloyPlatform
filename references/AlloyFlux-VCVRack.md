# AlloyFlux for VCV Rack

AlloyFlux is a versatile multi-voice oscillator module for VCV Rack, inspired by classic analog textures and modern digital features. This repository contains the firmware for the physical module, as well as a web-based configurator for patch management and MIDI control.

## Features

- 4-voice polyphony with unison and detune
- Multiple voice modes: PAIR, CLOUD, CHORD, CASCADE, STRING, POLY
- Real-time control over parameters via MIDI CC and USB serial console
- Preset management with 10 flash slots and SysEx patch dump/restore
- Web-based configurator for patch editing, MIDI mapping, and console monitoring
- Signal flow visualizer and interactive MIDI keyboard in the web configurator

## Connecting the Web Configurator to the Rack module

The module and the Web Configurator talk over ordinary MIDI, so they need a virtual MIDI cable between them: **loopMIDI** on Windows, the **IAC Driver** on macOS, or ALSA virtual ports on Linux.

**Use two ports, not one.** On Windows a MIDI output device can only be opened by a single process at a time. As soon as Rack's MIDI input is set, the module auto-claims the *same-named* output port for its parameter feedback (`syncMidiOutput()`), and the browser can then no longer open that port to send on. The failure is silent and asymmetrical, and looks exactly like this: knob moves in Rack appear in the browser, nothing sent from the browser arrives, and the port's byte counter never moves for outbound traffic.

Create two ports, e.g. `AlloyFlux-In` and `AlloyFlux-Out`, then:

| Endpoint                         | Setting                                 |
| -------------------------------- | --------------------------------------- |
| Rack module → MIDI input         | `AlloyFlux-In`                          |
| Rack module → MIDI output        | `AlloyFlux-Out` — **set this manually** |
| Web Configurator → MIDI dropdown | `AlloyFlux-In`                          |

`syncMidiOutput()` only auto-assigns the output when it has not been set, so an explicit choice in Rack's MIDI menu is respected. The Web Configurator has no input selector because it listens on *every* input port, so `AlloyFlux-Out` is picked up with no configuration.

The **MIDI Monitor** drawer pinned at the bottom of the configurator shows decoded traffic in both directions, tagged with the port each message used, plus **TX** and **RX** byte counters on its tab. Those counters run whether the drawer is open or not, so a glance is enough:

- **RX climbing, TX flat** — the browser is not transmitting. Wrong output port, or the port is held by another process (the case described above).
- **TX climbing, your virtual port's counter flat** — the bytes are leaving, but to a different port than the one you are watching.
- **Both flat** — nothing is connected at either end.

**Clear** in the monitor zeroes both counters, which makes "did that action send anything?" a one-glance question.

If the browser had the port open before Rack claimed it (or vice versa), restarting the browser releases it.

A single shared port does work when the physical module is not involved and Rack's MIDI output is left unset, but the moment feedback is enabled the two ends contend for it.

## Documentation

- [User Manual](../Manual.md): Panel controls, voice modes, MIDI CC map
- [Serial Reference](AlloyFlux-serial-reference.md): USB serial console commands and configuration
- [Web Configurator](../web-configurator/README.md): Instructions for using the browser-based editor to manage patches and control the module

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
