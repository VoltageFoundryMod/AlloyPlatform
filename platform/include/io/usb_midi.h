#pragma once

#include <stdint.h>

/**
 * USB MIDI interface — Milestone 29, promoted to the platform in M63f.
 *
 * Compiled only when USE_TINYUSB is defined (-DUSE_TINYUSB in build_flags).
 * The host sees a composite USB device: CDC (serial console) + MIDI.
 *
 * Everything here is module-independent. Parameters come from the generated
 * manifest (`param_map.h`), and anything the manifest cannot express — what a
 * note means, which CCs are actions, what identity to enumerate as — comes
 * from the module via `ModuleHooks.h`.
 *
 * Handled generically:
 *   Note On / Note Off        → moduleHook_noteOn / moduleHook_noteOff
 *   CC in the manifest        → paramMap_dispatchCC()
 *   CC not in the manifest    → moduleHook_controlChange()
 *   Program Change            → moduleHook_programChange()
 *   SysEx 7D <id0> <id1> …    → patch dump / apply, preset save / load / reset,
 *                               MIDI channel set
 */

#ifdef USE_TINYUSB

/** Register the USB MIDI descriptor and set up MIDI callbacks.
 *  Call once in setup(), before the audio driver starts. */
void usbMidi_init();

/** Poll for incoming MIDI messages and dispatch them.
 *  Call from updateControl() — runs on the control core at the control rate. */
void usbMidi_update();

/** Send CC feedback for any parameters that changed since the last call.
 *  Diffs current values against a cached snapshot; only emits changed CCs.
 *  Call from updateControl() after usbMidi_update(), once per tick. */
void usbMidi_sendFeedback();

/** Transmit a full PATCH_DUMP. Exposed so a module can push state to the host
 *  after something the platform cannot see has changed it. */
void usbMidi_sendPatchDump();

#endif // USE_TINYUSB

/**
 * MIDI receive channel: 0 = omni (accept all), 1–16 = that channel only.
 *
 * Platform-owned rather than module-owned — it is a property of the transport,
 * every module needs exactly the same behaviour from it, and the SysEx command
 * that sets it lives in the platform. Modules read it only to persist it.
 * Defined in platform/src/usb_midi.cpp.
 */
extern uint8_t gMidiChannel;
