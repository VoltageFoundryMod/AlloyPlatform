#ifdef USE_TINYUSB

#include "io/usb_midi.h"
#include "config_store.h"
#include "dsp/ChorusEngine.h"
#include "dsp/ReverbEngine.h"
#include "io/param_map.h"
#include "params.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <math.h>

// ---------------------------------------------------------------------------
// SysEx patch dump — AlloyFlux protocol
//
// Format (body between F0 and F7):
//   7D 41 46 <cmd> [cc0 val0 cc1 val1 ...]
//   7D = non-commercial manufacturer ID
//   41 46 = 'A' 'F' (AlloyFlux device signature)
//   cmd:
//     01 = REQUEST_DUMP  (host → device: request full patch dump)
//     02 = PATCH_DUMP    (device → host: full patch as CC pairs)
//     03 = APPLY_PATCH   (host → device: load CC pairs into parameters)
//
// All CC and value bytes are 7-bit safe (0–127).
// ---------------------------------------------------------------------------

static constexpr uint8_t kSysExMfr = 0x7D; // non-commercial
static constexpr uint8_t kSysExDevA = 'A';
static constexpr uint8_t kSysExDevF = 'F';
static constexpr uint8_t kSysExCmdRequestDump = 0x01;
static constexpr uint8_t kSysExCmdPatchDump = 0x02;
static constexpr uint8_t kSysExCmdApplyPatch = 0x03;
static constexpr uint8_t kSysExCmdPresetSave = 0x04;     // payload[0] = slot 0-9
static constexpr uint8_t kSysExCmdPresetLoad = 0x05;     // payload[0] = slot 0-9; responds with PATCH_DUMP
static constexpr uint8_t kSysExCmdPresetReset = 0x06;    // payload[0] = slot 0-9, or 0x7F = all
static constexpr uint8_t kSysExCmdSetMidiChannel = 0x07; // payload[0] = 0 (omni) or 1-16

// Convert a float parameter value into a 7-bit CC value.
static inline uint8_t sFloatToCC(float val, float minV, float maxV, bool logScale = false) {
    if (maxV <= minV)
        return 0;
    float t;
    if (logScale && minV > 0.0f)
        t = logf(val / minV) / logf(maxV / minV);
    else
        t = (val - minV) / (maxV - minV);
    const int v = (int)(127.0f * t + 0.5f);
    return (uint8_t)(v < 0 ? 0 : v > 127 ? 127
                                         : v);
}

// Fill buf[] with (cc, value) pairs for all patchable parameters.
// Returns the total number of bytes written (always even).
static uint8_t sBuildPatchPairs(uint8_t *buf) {
    uint8_t n = 0;
    // Continuous float params from the central CC table
    for (uint8_t i = 0; i < kCCParamCount; i++) {
        const CCParam &p = kCCParams[i];
        buf[n++] = p.cc;
        buf[n++] = sFloatToCC(*p.target, p.valMin, p.valMax, p.logScale);
    }
    // Special / select params not in kCCParams
    // CC 77 — filter mode: OFF=0, LP=26, HP=51, BP=77, NOTCH=102
    buf[n++] = 77;
    buf[n++] = (gFilterMode == FilterMode::OFF) ? 0 : (gFilterMode == FilterMode::LP) ? 26
                                                  : (gFilterMode == FilterMode::HP)   ? 51
                                                  : (gFilterMode == FilterMode::BP)   ? 77
                                                                                      : 102;
    // CC 78 — filter type: SVF=0, LADDER=96
    buf[n++] = 78;
    buf[n++] = (gFilterType == FilterType::SVF) ? 0 : 96;
    // CC 79 — fxorder filter pos: pre-chorus=0, post-chorus=96
    buf[n++] = 79;
    buf[n++] = gFxOrder.filterPostChorus ? 96 : 0;
    // CC 80 — fxorder delay pos: pre-reverb=0, post-reverb=96
    buf[n++] = 80;
    buf[n++] = gFxOrder.delayPostReverb ? 96 : 0;
    // CC 81 — envelope type: AR=0, ADSR=96
    buf[n++] = 81;
    buf[n++] = (gEnvelopeType == EnvelopeType::ADSR) ? 96 : 0;
    // CC 85 — delay on/off
    buf[n++] = 85;
    buf[n++] = (gDelayMix > 0.001f) ? 127 : 0;
    // CC 89 — chorus mode: OFF=0, I=48, II=80, I+II=112
    buf[n++] = 89;
    buf[n++] = (gChorusMode == ChorusMode::OFF) ? 0 : (gChorusMode == ChorusMode::I) ? 48
                                                  : (gChorusMode == ChorusMode::II)  ? 80
                                                                                     : 112;
    // CC 90 — sub octave: 1 oct below=0, 2 oct below=96
    buf[n++] = 90;
    buf[n++] = (gSubOctave >= 2) ? 96 : 0;
    // CC 114 — reverb freeze
    buf[n++] = 114;
    buf[n++] = gRevFrozen ? 127 : 0;
    // CC 115 — voice mode: 6 bands of 21: PAIR=10, CLOUD=31, CHORD=52, CASCADE=73, STRING=94, POLY=116
    buf[n++] = 115;
    buf[n++] = (gVoiceMode == VoiceMode::PAIR)      ? 10
               : (gVoiceMode == VoiceMode::CLOUD)   ? 31
               : (gVoiceMode == VoiceMode::CHORD)   ? 52
               : (gVoiceMode == VoiceMode::CASCADE) ? 73
               : (gVoiceMode == VoiceMode::STRING)  ? 94
                                                    : 116; // POLY
    // CC 116 — reverb on/off
    buf[n++] = 116;
    buf[n++] = gRevEnabled ? 127 : 0;
    // CC 110 — MIDI receive channel (0 = omni, 1–16 = specific channel)
    buf[n++] = 110;
    buf[n++] = gMidiChannel; // 0-16 fits in 7 bits
    return n;
}

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
        if (gVoiceMode == VoiceMode::POLY) {
            for (uint8_t i = 0; i < 4; i++) {
                if (sPolySlots[i].midiNote == note) {
                    sPolyEnvs[i]->setGate(false);
                    sPolySlots[i].midiNote = 255;
                }
            }
        } else if (sActiveNote == note) {
            gGateHigh = false;
            gCurveEng->setGate(false);
            sActiveNote = 255;
        }
        return;
    }

    if (gVoiceMode == VoiceMode::POLY) {
        // POLY voice allocation: find a free slot ('free' = midiNote 255).
        // If all busy, steal the round-robin next slot (oldest by sPolyRR).
        uint8_t slot = 255;
        for (uint8_t i = 0; i < 4; i++) {
            if (sPolySlots[i].midiNote == 255) {
                slot = i;
                break;
            }
        }
        if (slot == 255) {
            // All slots occupied — steal round-robin
            slot = sPolyRR % 4;
        }
        sPolyRR = (sPolyRR + 1) % 4;
        sPolySlots[slot].freq = constrain(midiNoteToHz(note), 20.0f, 8000.0f);
        sPolySlots[slot].velocity = gVelocitySensitive ? (velocity / 127.0f) : 1.0f;
        sPolySlots[slot].midiNote = note;
        sPolyEnvs[slot]->setGate(true);
        return;
    }

    sActiveNote = note;
    gBaseFreq = constrain(midiNoteToHz(note), 20.0f, 8000.0f);
    gMidiVelocity = gVelocitySensitive ? (velocity / 127.0f) : 1.0f;
    gGatePatched = true; // arm envelope — MIDI is now the gate source
    gGateHigh = true;
    gCurveEng->setGate(true); // arm attack immediately — closes ISR window
}

static void onNoteOff(byte channel, byte note, byte /*velocity*/) {
    if (!channelMatches(channel))
        return;
    if (gVoiceMode == VoiceMode::POLY) {
        for (uint8_t i = 0; i < 4; i++) {
            if (sPolySlots[i].midiNote == note) {
                sPolyEnvs[i]->setGate(false);
                sPolySlots[i].midiNote = 255;
            }
        }
        return;
    }
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
    case 65: // Portamento Switch — velocity sensitivity on (>=64) / off (<64)
        gVelocitySensitive = (value >= 64);
        if (!gVelocitySensitive) {
            gMidiVelocity = 1.0f; // immediately restore full volume for live notes
        }
        break;
    case 77: // Filter mode — 5 options spread evenly across 0–127
        if (value < 26)
            gFilterMode = FilterMode::OFF;
        else if (value < 51)
            gFilterMode = FilterMode::LP;
        else if (value < 77)
            gFilterMode = FilterMode::HP;
        else if (value < 102)
            gFilterMode = FilterMode::BP;
        else
            gFilterMode = FilterMode::NOTCH;
        break;
    case 78: // Filter type — 0-63 = SVF, 64-127 = LADDER
        gFilterType = (value < 64) ? FilterType::SVF : FilterType::LADDER;
        break;
    case 79: // FxOrder filter position — 0-63 = pre-chorus (default), 64-127 = post-chorus
        gFxOrder.filterPostChorus = (value >= 64);
        break;
    case 80: // FxOrder delay position — 0-63 = pre-reverb (default), 64-127 = post-reverb
        gFxOrder.delayPostReverb = (value >= 64);
        break;
    case 81: // Envelope type — 0-63 = AR, 64-127 = ADSR
        gEnvelopeType = (value < 64) ? EnvelopeType::AR : EnvelopeType::ADSR;
        gCurveEng->reset();
        break;
    case 85: { // Delay on/off — ≥64 = on, <64 = off (zeroes mix; CC 88 restores it)
        static float sStoredDelayMix = 0.5f;
        if (value >= 64) {
            gDelayMix = sStoredDelayMix; // restore last-used mix
        } else {
            if (gDelayMix > 0.001f)
                sStoredDelayMix = gDelayMix; // remember before zeroing
            gDelayMix = 0.0f;
        }
        break;
    }
    case 89: // Chorus mode — 0-31=OFF, 32-63=I, 64-95=II, 96-127=I+II
        if (value < 32)
            gChorusMode = ChorusMode::OFF;
        else if (value < 64)
            gChorusMode = ChorusMode::I;
        else if (value < 96)
            gChorusMode = ChorusMode::II;
        else
            gChorusMode = ChorusMode::I_II;
        break;
    case 90: // Sub octave — 0-63 = 1 oct below, 64-127 = 2 oct below
        gSubOctave = (value >= 64) ? 2 : 1;
        break;
    case 114: // Reverb freeze — M41: ≥64 = freeze on, <64 = freeze off
        gRevFrozen = (value >= 64);
        break;
    case 115: // Voice Mode — 6 bands: 0-20=PAIR, 21-41=CLOUD, 42-62=CHORD, 63-83=CASCADE, 84-104=STRING, 105-127=POLY
        if (value < 21)
            gVoiceMode = VoiceMode::PAIR;
        else if (value < 42)
            gVoiceMode = VoiceMode::CLOUD;
        else if (value < 63)
            gVoiceMode = VoiceMode::CHORD;
        else if (value < 84)
            gVoiceMode = VoiceMode::CASCADE;
        else if (value < 105)
            gVoiceMode = VoiceMode::STRING;
        else
            gVoiceMode = VoiceMode::POLY;
        break;
    case 116: // Reverb on/off — ≥64 = on
        gRevEnabled = (value >= 64);
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
    // Programs 1–6 map to VoiceMode PAIR/CLOUD/CHORD/CASCADE/STRING/POLY.
    if (program >= 1 && program <= 6)
        gVoiceMode = static_cast<VoiceMode>(program - 1);
}

// Build and transmit a PATCH_DUMP SysEx response.
// Must be placed after MIDI_CREATE_INSTANCE since it calls MidiUsb.sendSysEx.
static void sSendPatchDump() {
    // Header (4) + float params (24×2=48) + select params (14×2=28) = 80 + 4 = 84 bytes; use 92.
    static uint8_t sBuf[92];
    sBuf[0] = kSysExMfr;
    sBuf[1] = kSysExDevA;
    sBuf[2] = kSysExDevF;
    sBuf[3] = kSysExCmdPatchDump;
    const uint8_t pairBytes = sBuildPatchPairs(sBuf + 4);
    MidiUsb.sendSysEx(4 + pairBytes, sBuf, false); // library adds F0/F7
}

// SysEx handler — AlloyFlux patch dump protocol.
// The Arduino MIDI Library v5 passes data[] with F0 at [0] and F7 at [length-1].
// We skip boundaries so the body always starts at [1] and ends before F7.
static void onSysEx(uint8_t *data, unsigned int length) {
    // Skip leading F0 if the library includes it
    uint8_t *body = data;
    unsigned int bodyLen = length;
    if (bodyLen > 0 && body[0] == 0xF0) {
        body++;
        bodyLen--;
    }
    if (bodyLen > 0 && body[bodyLen - 1] == 0xF7) {
        bodyLen--;
    }

    // Validate 3-byte header: 7D 41('A') 46('F') <cmd>
    if (bodyLen < 4)
        return;
    if (body[0] != kSysExMfr || body[1] != kSysExDevA || body[2] != kSysExDevF)
        return;

    const uint8_t cmd = body[3];
    const uint8_t arg0 = (bodyLen > 4) ? (body[4] & 0x7F) : 0;

    if (cmd == kSysExCmdRequestDump) {
        sSendPatchDump();
    } else if (cmd == kSysExCmdApplyPatch) {
        // Payload: interleaved (cc, value) pairs starting at body[4]
        for (unsigned int i = 4; i + 1 < bodyLen; i += 2) {
            const uint8_t cc = body[i] & 0x7F;
            const uint8_t val = body[i + 1] & 0x7F;
            onControlChange(1, cc, val); // reuse existing dispatch
        }
    } else if (cmd == kSysExCmdPresetSave) {
        configStore_save(arg0);
    } else if (cmd == kSysExCmdPresetLoad) {
        configStore_load(arg0);
        sSendPatchDump(); // auto-refresh web UI after load
    } else if (cmd == kSysExCmdPresetReset) {
        const uint8_t fwSlot = (arg0 == 0x7F) ? 255 : arg0;
        configStore_reset(fwSlot);
        // For live slot (0) or full reset, apply defaults immediately and
        // respond with a dump so the web UI syncs without a page reload.
        if (arg0 == 0 || arg0 == 0x7F) {
            configStore_applyDefaults();
            sSendPatchDump();
        }
    } else if (cmd == kSysExCmdSetMidiChannel) {
        const uint8_t ch = arg0 & 0x7F;
        // Accept 0 (omni) or 1-16; ignore invalid values silently.
        if (ch <= 16) {
            gMidiChannel = ch;
            configStore_save(0); // persist; rate-limit may throttle but that's fine
        }
        sSendPatchDump(); // echo back so UI confirms the new value
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void usbMidi_init() {
    TinyUSBDevice.setManufacturerDescriptor("Voltage Foundry Modular");
    TinyUSBDevice.setProductDescriptor("Alloy Flux");
    sUsbMidiTransport.setStringDescriptor("AlloyFlux MIDI");
    MidiUsb.begin(MIDI_CHANNEL_OMNI);
    MidiUsb.setHandleNoteOn(onNoteOn);
    MidiUsb.setHandleNoteOff(onNoteOff);
    MidiUsb.setHandleControlChange(onControlChange);
    MidiUsb.setHandleProgramChange(onProgramChange);
    MidiUsb.setHandleSystemExclusive(onSysEx);
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
