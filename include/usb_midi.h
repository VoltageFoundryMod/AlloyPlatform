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
 *  Call once in setup(), before startMozzi(). */
void usbMidi_init();

/** Poll for incoming MIDI messages and dispatch them.
 *  Call from updateControl() — runs on Core 0 at MOZZI_CONTROL_RATE. */
void usbMidi_update();

#endif // USE_TINYUSB
