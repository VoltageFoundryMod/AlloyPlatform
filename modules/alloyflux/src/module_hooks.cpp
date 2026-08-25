// AlloyFlux's half of the platform's generic mechanisms (M63f).
//
// USB MIDI, the SysEx patch protocol, preset slots and CC feedback all live in
// platform/src/ and are driven by the generated parameter manifest. What is
// left is what the manifest cannot express: what a note means here, which CCs
// are actions rather than parameters, and what a preset blob contains.
//
// See platform/include/ModuleHooks.h for the contract.

#include "ModuleHooks.h"

#include "VoiceMode.h"
#include "alloy_config.h"
#include "dsp/ChorusEngine.h"
#include "dsp/CurveEngine.h"
#include "dsp/FilterEngine.h"
#include "dsp/FxChain.h"
#include "dsp/ReverbEngine.h"
#include "io/usb_midi.h" // gMidiChannel
#include "params.h"
#include "scale_quantizer.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
const char *const kModuleManufacturer = "Voltage Foundry Modular";
const char *const kModuleProduct      = "Alloy Flux";
const char *const kModuleMidiName     = "AlloyFlux MIDI";

// SysEx device signature — 'A','F'. The Web Configurator reads this to decide
// which parameter map to load, so it must stay unique across modules.
const uint8_t kSysExDevId0 = 'A';
const uint8_t kSysExDevId1 = 'F';

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------

// Scale quantizer globals (M49) — defined here because this is the only
// translation unit that both reads and writes them.
ScaleId gQuantizeScale = ScaleId::CHROMATIC;
int8_t  gTranspose     = 0;

uint8_t sActiveNote = 255; // 255 = no note currently held (see params.h)

// Convert MIDI note number (0–127) to frequency in Hz.
// Standard equal-temperament: A4 (note 69) = 440 Hz.
static inline float midiNoteToHz(uint8_t note)
{ return 440.0f * powf(2.0f, ((int8_t)note - 69) / 12.0f); }

void moduleHook_noteOn(uint8_t note, uint8_t velocity)
{
    if(modeUsesPolySlots(gVoiceMode))
    {
        const float freq = constrain(
            midiNoteToHz(quantizeNote(note, gQuantizeScale, gTranspose)),
            20.0f,
            8000.0f);
        // subMult depends on voice sub-octave param — use 0.5 (default, -1 oct)
        // as a safe approximation; control() will correct the sub freq next tick.
        polyNoteOn(freq,
                   gVelocitySensitive ? (velocity / 127.0f) : 1.0f,
                   0.5f,
                   note,
                   polySlotLimit(gVoiceMode));
        // The latch that stops CLOUD's drone (M78). POLY has no drone to stop
        // and ignores the flag entirely, so setting it for both costs nothing
        // and keeps CLOUD on the same rule every other mode follows: the first
        // note takes the module off drone, and only CC 119 or MODE+SHIFT hands
        // it back.
        gGatePatched = true;
        return;
    }

    sActiveNote = note;
    gBaseFreq   = constrain(
        midiNoteToHz(quantizeNote(note, gQuantizeScale, gTranspose)),
        20.0f,
        8000.0f);
    gMidiVelocity = gVelocitySensitive ? (velocity / 127.0f) : 1.0f;
    gGatePatched  = true; // arm envelope — MIDI is now the gate source
    gGateHigh     = true;
    gCurveEng->setGate(true); // arm attack immediately — closes ISR window
}

void moduleHook_noteOff(uint8_t note)
{
    if(modeUsesPolySlots(gVoiceMode))
    {
        for(uint8_t i = 0; i < 6; i++)
        {
            if(sPolySlots[i].midiNote == note)
            {
                sPolyEnvs[i]->setGate(false);
                sPolySlots[i].midiNote = kPolySlotFree;
            }
        }
        return;
    }
    // Monophonic last-note priority: only release if this is the active note.
    if(sActiveNote == note)
    {
        gGateHigh = false;
        gCurveEng->setGate(
            false); // arm release immediately — closes ISR window
        sActiveNote = 255;
    }
}

bool moduleHook_controlChange(uint8_t cc, uint8_t value)
{
    switch(cc)
    {
        case 64: // Sustain pedal — arms gate; release only on pedal-up (value < 64)
            gGatePatched = true;
            gGateHigh    = (value >= 64);
            return true;
        case 104: // Transpose (M49) — 0–48 encodes −24…+24 semitones
            gTranspose = (int8_t)constrain((int)value - 24, -24, 24);
            return true;
        case 114: // Reverb freeze — M41: ≥64 = freeze on, <64 = freeze off
            gRevFrozen = (value >= 64);
            return true;
        case 119: // Drone return — clears gGatePatched, module returns to drone
            returnToDrone();
            return true;
        case 123: // All Notes Off / panic
            gGateHigh   = false;
            sActiveNote = 255;
            for(uint8_t i = 0; i < 6; i++)
            {
                if(sPolyEnvs[i])
                {
                    sPolyEnvs[i]->setGate(false);
                    sPolyEnvs[i]->reset();
                }
                sPolySlots[i].midiNote = kPolySlotFree;
            }
            sPolyRR = 0;
            return true;
        default: return false;
    }
}

void moduleHook_programChange(uint8_t program)
{
    // Programs 1–6 map to VoiceMode PAIR/CLOUD/CHORD/CASCADE/STRING/POLY.
    if(program >= 1 && program <= 6)
        gVoiceMode = static_cast<VoiceMode>(program - 1);
}

uint8_t moduleHook_extraPatchPairs(uint8_t *buf, uint8_t maxBytes)
{
    uint8_t n   = 0;
    auto    add = [&](uint8_t cc, uint8_t value)
    {
        if(n + 2 > maxBytes)
            return;
        buf[n++] = cc;
        buf[n++] = value;
    };
    add(114, gRevFrozen ? 127 : 0); // reverb freeze
    // Transpose: 0–48 encodes −24…+24 semitones (offset 24).
    add(104, (uint8_t)constrain((int)gTranspose + 24, 0, 48));
    return n;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

const uint16_t kEngineId      = kAlloyFluxEngineId;
const uint16_t kEngineVersion = kAlloyFluxEngineVersion;

uint16_t moduleHook_packConfig(void *blob, uint16_t maxBytes)
{
    if(maxBytes < sizeof(AlloyConfig))
        return 0;
    AlloyConfig cfg;
    packAlloyConfig(cfg);
    memcpy(blob, &cfg, sizeof(cfg));
    return (uint16_t)sizeof(cfg);
}

void moduleHook_applyConfig(const void *blob, uint16_t bytes)
{
    if(bytes < sizeof(AlloyConfig))
        return;
    AlloyConfig cfg;
    memcpy(&cfg, blob, sizeof(cfg));
    applyAlloyConfig(cfg);
}

void moduleHook_applyDefaults()
{ applyAlloyDefaults(); }
