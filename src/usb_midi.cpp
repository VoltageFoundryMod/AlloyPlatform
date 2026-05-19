#ifdef USE_TINYUSB

#include "io/usb_midi.h"
#include "io/param_map.h"
#include "params.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <math.h>

// ---------------------------------------------------------------------------
// USB MIDI transport + MIDI interface
// ---------------------------------------------------------------------------

static Adafruit_USBD_MIDI sUsbMidiTransport;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, sUsbMidiTransport, MidiUsb);

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static uint8_t sActiveNote = 255; // 255 = no note currently held

// Returns true if an incoming message on `ch` should be processed.
// gMidiChannel == 0 means omni (accept all); otherwise match exactly.
static inline bool channelMatches(uint8_t ch) {
    return gMidiChannel == 0 || ch == gMidiChannel;
}

// Convert MIDI note number (0–127) to frequency in Hz.
// Standard equal-temperament: A4 (note 69) = 440 Hz.
static inline float midiNoteToHz(uint8_t note) {
    return 440.0f * powf(2.0f, ((int8_t)note - 69) / 12.0f);
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

static void onNoteOn(byte channel, byte note, byte velocity) {
    if (!channelMatches(channel))
        return;
    if (velocity == 0) {
        // NoteOn with velocity 0 is a NoteOff (running-status MIDI convention).
        if (sActiveNote == note) {
            gGateHigh = false;
            gCurveEng->setGate(false); // arm release immediately — closes ISR window
            sActiveNote = 255;
        }
        return;
    }
    sActiveNote = note;
    gBaseFreq = constrain(midiNoteToHz(note), 20.0f, 8000.0f);
    gMidiVelocity = velocity / 127.0f; // scale output volume by note velocity
    gGatePatched = true;               // arm envelope — MIDI is now the gate source
    gGateHigh = true;
    gCurveEng->setGate(true); // arm attack immediately — closes ISR window
}

static void onNoteOff(byte channel, byte note, byte /*velocity*/) {
    if (!channelMatches(channel))
        return;
    // Monophonic last-note priority: only release if this is the active note.
    if (sActiveNote == note) {
        gGateHigh = false;
        gCurveEng->setGate(false); // arm release immediately — closes ISR window
        sActiveNote = 255;
    }
}

static void onControlChange(byte channel, byte cc, byte value) {
    if (!channelMatches(channel))
        return;
    // Continuous parameters — delegated to the central CC map (param_map.cpp).
    if (paramMap_dispatchCC(cc, value))
        return;

    // Special cases: not simple float parameters.
    switch (cc) {
    case 64: // Sustain pedal — arms gate; release only on pedal-up (value < 64)
        gGatePatched = true;
        gGateHigh = (value >= 64);
        break;
    case 114: // Reverb freeze — M41: ≥64 = freeze on, <64 = freeze off
        gRevFrozen = (value >= 64);
        break;
    case 115: // Voice Mode — 0–63 = PAIR, 64–127 = CHORD (ranges subdivide as modes are added)
        gVoiceMode = (value < 64) ? VoiceMode::PAIR : VoiceMode::CHORD;
        break;
    case 119: // Drone return — clears gGatePatched, module returns to continuous drone
        gGatePatched = false;
        gGateHigh = false;
        gMidiVelocity = 1.0f; // restore full volume on return to drone/CV
        sActiveNote = 255;
        break;
    case 123: // All Notes Off / panic
        gGateHigh = false;
        sActiveNote = 255;
        break;
    default:
        break;
    }
}

static void onProgramChange(byte channel, byte program) {
    if (!channelMatches(channel))
        return;
    // Programs 1–5 map to VoiceMode PAIR/CLOUD/CHORD/CASCADE/STRING.
    if (program >= 1 && program <= 5)
        gVoiceMode = static_cast<VoiceMode>(program - 1);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void usbMidi_init() {
    sUsbMidiTransport.setStringDescriptor("AlloyFlux MIDI");
    MidiUsb.begin(MIDI_CHANNEL_OMNI);
    MidiUsb.setHandleNoteOn(onNoteOn);
    MidiUsb.setHandleNoteOff(onNoteOff);
    MidiUsb.setHandleControlChange(onControlChange);
    MidiUsb.setHandleProgramChange(onProgramChange);
    MidiUsb.turnThruOff(); // no MIDI echo back to host

    // Wait for the USB device to fully enumerate with both CDC + MIDI interfaces.
    // Timeout after 2 s so the module boots standalone without USB host.
    const uint32_t t0 = millis();
    while (!TinyUSBDevice.mounted() && (millis() - t0) < 2000) {
        delay(1);
    }
}

void usbMidi_update() {
    MidiUsb.read();
}

#endif // USE_TINYUSB
