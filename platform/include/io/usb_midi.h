#pragma once

#include <stdint.h>

// gMidiChannel and the whole Alloy MIDI protocol moved to midi_core.h in
// M78a.  Re-exported here so every existing includer — both modules'
// commands.cpp, config_store.cpp and main.cpp — keeps compiling unchanged.
#include "io/midi_core.h"

/**
 * USB MIDI transport — Milestone 29, promoted to the platform in M63f,
 * reduced to a transport in M78a.
 *
 * Compiled only when USE_TINYUSB is defined (-DUSE_TINYUSB in build_flags).
 * The host sees a composite USB device: CDC (serial console) + MIDI.
 *
 * This header is now small on purpose.  Everything a *message* means —
 * parameter dispatch, the SysEx patch protocol, preset commands, the CC
 * feedback diff — belongs to `io/midi_core.h` and is shared with any other
 * transport (BLE MIDI, M78b).  What remains here is TinyUSB enumeration and
 * draining the USB FIFO.
 *
 * Handled generically by midi_core, from any transport:
 *   Note On / Note Off        → moduleHook_noteOn / moduleHook_noteOff
 *   CC in the manifest        → paramMap_dispatchCC()
 *   CC not in the manifest    → moduleHook_controlChange()
 *   Program Change            → moduleHook_programChange()
 *   SysEx 7D <id0> <id1> …    → patch dump / apply, preset save / load / reset,
 *                               MIDI channel set
 */

#ifdef USE_TINYUSB

/** Register the USB MIDI descriptor, set up MIDI callbacks, and register the
 *  USB MidiPort with midi_core.
 *  Call once in setup(), before the audio driver starts. */
void usbMidi_init();

/** Poll for incoming MIDI messages and dispatch them.
 *  Call from updateControl() — runs on the control core at the control rate. */
void usbMidi_update();

#endif // USE_TINYUSB
