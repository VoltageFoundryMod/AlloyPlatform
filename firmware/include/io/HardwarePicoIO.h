#pragma once

// Arduino.h pulled in transitively via ButtonEngine.h (INPUT_PULLUP, digitalRead).
// This header must only be compiled in the hardware (Mozzi / Arduino-Pico) build.
#include "ButtonEngine.h"
#include "io/HardwareIO.h"
#include "params.h" // gBaseFreq, gGateHigh, gGatePatched, gShape, …

#include <math.h> // log2f

// ---------------------------------------------------------------------------
// HardwarePicoIO — IHardwareIO implementation for Raspberry Pi Pico 2.
//
// M37d bootstrap: pot reads derive from the gXxx globals (set by MIDI/serial)
// normalised back to the 0–1 contract that IOBridge expects.  When the ADC
// mux driver lands (future hardware pass), readPot() will query the physical
// multiplexed ADC directly and will be the primary writer of the gXxx pitch
// and timbre globals.
//
// ButtonEngine references (MODE, SHIFT) are injected by main.cpp so that the
// same button objects are shared between HardwarePicoIO and the combo logic.
// ---------------------------------------------------------------------------
class HardwarePicoIO : public IHardwareIO
{
  public:
    HardwarePicoIO(ButtonEngine &btnMode, ButtonEngine &btnShift)
    : _btnMode(btnMode), _btnShift(btnShift)
    {
    }

    // -----------------------------------------------------------------------
    // Pot reads — normalise engineering-unit gXxx globals → 0–1.
    // Each conversion is the exact inverse of the scaling applied in
    // IOBridge::fillSynthParams(), so the round-trip is lossless.
    // -----------------------------------------------------------------------
    float readPot(PotId id) override
    {
        switch(id)
        {
            case PotId::ROOT:
            {
                // gBaseFreq (Hz) → V/Oct → 0–1 normalised
                //   voct  = log2(f / 440)
                //   norm  = (voct + 4) / 8      (range ±4 V maps to 0–1)
                float voct = log2f(gBaseFreq / 440.0f);
                return (voct + 4.0f) / 8.0f;
            }
            case PotId::RELATION: return gRelation / 24.0f; // 0–24 st → 0–1
            case PotId::SHAPE: return gShape;               // already 0–1
            case PotId::MOTION: return gMotion;             // already 0–1
            case PotId::COLOR: return gColor;               // already 0–1
            case PotId::CURVE: return gCurve;               // already 0–1
            case PotId::SPACE: return gSpace / 2.0f;        // 0–2 → 0–1
            case PotId::FATNESS: return gFatness;           // already 0–1
            case PotId::DRIFTSPEED:
                // gDriftSpeed is in [0.001, 0.10] coeff; normalise to 0–1
                return (gDriftSpeed - 0.001f) / (0.10f - 0.001f);
            case PotId::VOL: return gVolume; // already 0–1
            default: return 0.5f;
        }
    }

    // -----------------------------------------------------------------------
    // CV reads — hardware ADC mux not yet wired (M37d bootstrap).
    // GATE: reflects volatile gGateHigh written by MIDI / button handlers.
    // VOCT: no hardware V/Oct jack implemented yet; returns 0 (pitch from pot).
    // -----------------------------------------------------------------------
    float readCV(CVId id) override
    {
        switch(id)
        {
            case CVId::VOCT:
                return 0.0f; // pitch from ROOT pot only until ADC lands
            case CVId::GATE: return gGateHigh ? 5.0f : 0.0f;
            default: return 0.0f;
        }
    }

    bool isPatched(CVId id) override
    {
        switch(id)
        {
            case CVId::VOCT: return false;        // no V/Oct jack yet
            case CVId::GATE: return gGatePatched; // set by MIDI / button combo
            default: return false;
        }
    }

    // -----------------------------------------------------------------------
    // Button reads — wraps ButtonEngine instances owned by main.cpp.
    // Returns instantaneous debounced isDown() state (not edge events).
    // -----------------------------------------------------------------------
    bool readButton(ButtonId id) override
    {
        switch(id)
        {
            case ButtonId::MODE: return _btnMode.isDown();
            case ButtonId::SHIFT: return _btnShift.isDown();
            default: return false;
        }
    }

    // -----------------------------------------------------------------------
    // LED writes — stub until APA102 SPI driver lands (M30).
    // -----------------------------------------------------------------------
    void writeLight(LightId id, float r, float g, float b) override
    {
        (void)id;
        (void)r;
        (void)g;
        (void)b;
    }

  private:
    ButtonEngine &_btnMode;
    ButtonEngine &_btnShift;
};
