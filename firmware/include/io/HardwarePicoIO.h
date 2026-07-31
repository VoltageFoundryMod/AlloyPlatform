#pragma once

// Arduino.h pulled in transitively via ButtonEngine.h (INPUT_PULLUP, digitalRead).
// This header must only be compiled in the hardware (Mozzi / Arduino-Pico) build.
#include "ButtonEngine.h"
#include "dsp/DelayEngine.h" // DELAY_MAX_MS (SHIFT+DELAY normalisation)
#include "io/Apa102.h"       // M30 — Dotstar chain driver
#include "io/HardwareIO.h"
#include "params.h" // gBaseFreq, gGateHigh, gGatePatched, gShape, …

#include <math.h> // log2f

// Panel LED wiring — from hardware/MainPCB (LEDs.kicad_sch / MCU.kicad_sch).
// Override at build time if a board revision moves them.
#ifndef PIN_LED_DATA
#define PIN_LED_DATA 6 // GP6, pin 9  — Dotstar chain DI
#endif
#ifndef PIN_LED_CLK
#define PIN_LED_CLK 7 // GP7, pin 10 — Dotstar chain CI
#endif
#ifndef PIN_LED_MODE_BTN
#define PIN_LED_MODE_BTN 12 // GP12, pin 16 — MODE switch backlight (via R35)
#endif
#ifndef PIN_LED_SHIFT_BTN
#define PIN_LED_SHIFT_BTN 13 // GP13, pin 17 — SHIFT switch backlight (via R36)
#endif

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
            case PotId::DELAY: return gDelayMix;            // already 0–1
            case PotId::REVERB: return gRevMix;             // already 0–1
            case PotId::FATNESS: return gFatness;           // already 0–1
            case PotId::DRIFTSPEED:
                // gDriftSpeed is in [0.001, 0.10] coeff; normalise to 0–1
                return (gDriftSpeed - 0.001f) / (0.10f - 0.001f);
            case PotId::VOL: return gVolume; // already 0–1
            case PotId::DELAYTIME:
                // gDelayTime is in ms over [10, DELAY_MAX_MS]; normalise to 0–1
                return (gDelayTime - 10.0f) / ((float)DELAY_MAX_MS - 10.0f);
            case PotId::REVERBSIZE: return gRevSize; // already 0–1
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
    // Panel LEDs (M30) — 7× APA102/SK9822 Dotstars plus the two illuminated
    // switch backlights. All colour decisions are made upstream in LedEngine
    // (common/include/io/LedEngine.h); this end only transports them.
    // -----------------------------------------------------------------------

    /** Claims the LED GPIOs and blanks the panel. Call once from setup(). */
    void begin()
    {
        _dotstars.begin(PIN_LED_DATA, PIN_LED_CLK);
        pinMode(PIN_LED_MODE_BTN, OUTPUT);
        pinMode(PIN_LED_SHIFT_BTN, OUTPUT);
        analogWrite(PIN_LED_MODE_BTN, 0);
        analogWrite(PIN_LED_SHIFT_BTN, 0);
    }

    /**
     * Stages one Dotstar. Nothing reaches the panel until showLights().
     *
     * LightId is in panel-role order, which is NOT the order the LEDs are
     * daisy-chained in on the PCB, so it is remapped here — see kChainPos.
     * The MODE and SHIFT roles additionally drive the backlight inside their
     * switch: those are single-colour LEDs on a plain PWM pin, so they follow
     * the luminance of the Dotstar beside them and pick up the mode flash and
     * the shift/drone breathe for free.
     */
    void writeLight(LightId id, float r, float g, float b) override
    {
        // LightId (panel role) -> position in the physical daisy chain.
        // Chain runs D12 -> D13 -> D14 -> D15 -> D16 -> D21 -> D22, while
        // LightId is ordered VOICE_L(D12) VOICE_R(D22) MOD_L(D13) MOD_R(D21)
        // MODE(D14) SHIFT(D16) CENTRE(D15). Traced from the schematic netlist;
        // if a board revision reroutes the chain, this table is the only thing
        // that changes.
        static const uint8_t kChainPos[(uint8_t)LightId::LIGHT_COUNT] = {
            0, // VOICE_L -> D12, first on the chain
            6, // VOICE_R -> D22, last
            1, // MOD_L   -> D13
            5, // MOD_R   -> D21
            2, // MODE    -> D14
            4, // SHIFT   -> D16
            3, // CENTRE  -> D15
        };

        const uint8_t i = (uint8_t)id;
        if(i >= (uint8_t)LightId::LIGHT_COUNT)
            return;
        _dotstars.setPixel(kChainPos[i], r, g, b);

        if(id == LightId::MODE)
            _btnLedMode = _luma8(r, g, b);
        else if(id == LightId::SHIFT)
            _btnLedShift = _luma8(r, g, b);
    }

    /** Latches the staged frame — one shift-out for the whole chain. */
    void showLights() override
    {
        _dotstars.show();
        analogWrite(PIN_LED_MODE_BTN, _btnLedMode);
        analogWrite(PIN_LED_SHIFT_BTN, _btnLedShift);
    }

  private:
    /** Rec.601 luminance of a normalised colour, as 0–255 PWM. */
    static uint8_t _luma8(float r, float g, float b)
    {
        float y = 0.299f * r + 0.587f * g + 0.114f * b;
        if(y <= 0.0f)
            return 0u;
        if(y >= 1.0f)
            return 255u;
        return (uint8_t)(y * 255.0f + 0.5f);
    }

    ButtonEngine &_btnMode;
    ButtonEngine &_btnShift;

    Apa102<(uint8_t)LightId::LIGHT_COUNT> _dotstars;
    uint8_t                               _btnLedMode  = 0u;
    uint8_t                               _btnLedShift = 0u;
};
