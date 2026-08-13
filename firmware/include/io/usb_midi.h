#pragma once

/**
 * USB MIDI interface — Milestone 29.
 *
 * Compiled only when USE_TINYUSB is defined (-DUSE_TINYUSB in build_flags).
 * The host sees a composite USB device: CDC (serial console) + MIDI.
 *
 * Supported messages:
 *   Note On / Note Off  → gBaseFreq + gGateHigh (monophonic; last-note priority)
 *   CC 1  (Mod Wheel)   → gMotion  (0–127 → 0.0–1.0)
 *   CC 74               → gShape   (0–127 → 0.0–1.0)
 *   CC 91               → gSpace   (0–127 → 0.0–2.0)
 *   CC 94               → gRelation (0–127 → 0.0–24.0 semitones)
 *   CC 123              → All Notes Off / panic
 *   Program Change 1–5  → gVoiceMode (PAIR/CLOUD/CHORD/CASCADE/STRING)
 */

#ifdef USE_TINYUSB

/** Register the USB MIDI descriptor and set up MIDI callbacks.
 *  Call once in setup(), before the audio driver starts. */
void usbMidi_init();

/** Poll for incoming MIDI messages and dispatch them.
 *  Call from updateControl() — runs on Core 0 at the control rate. */
void usbMidi_update();

/** Send CC feedback for any parameters that changed since the last call.
 *  Diffs current values against a cached snapshot; only emits changed CCs.
 *  Call from updateControl() after usbMidi_update(), once per tick. */
void usbMidi_sendFeedback();

#endif // USE_TINYUSB
