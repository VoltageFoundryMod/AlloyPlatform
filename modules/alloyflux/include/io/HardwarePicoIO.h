#pragma once

// Arduino.h pulled in transitively via ButtonEngine.h (INPUT_PULLUP, digitalRead).
// This header must only be compiled in the hardware (Arduino-Pico) build.
#include "io/ButtonEngine.h"
#include "dsp/DelayEngine.h" // DELAY_MAX_MS (SHIFT+DELAY normalisation)
#include "io/Apa102.h"       // M30 — Dotstar chain driver
#include "io/HardwareIO.h"   // PotId/CVId/… positional slots
#include "io/PanelMap.h"     // Pot::/Cv::/Btn::/Led:: — AlloyFlux's slot names
#include "io/PotTakeover.h"  // M62 — knob vs. web/MIDI arbitration
#include "params.h"          // gBaseFreq, gGateHigh, gGatePatched, gShape, …

#include <math.h> // log2f, exp2f

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

// The multiplexed ADC driver is not wired yet.  Until it is, readPotRaw()
// reports the parameter's own position, which makes the takeover layer inert:
// knob and parameter never disagree, so nothing ever detaches.  The driver
// only has to fill in readPotRaw() — every other piece is already in place.
#ifndef POT_ADC_PRESENT
#define POT_ADC_PRESENT 0
#endif

// Physical knobs that serve two parameters — {primary, SHIFT-secondary}.
// One ADC channel each; the SHIFT edge detaches both sides so neither
// parameter inherits the position the other left the knob in.  The ADC driver
// reuses this table to map both ids onto a single mux channel.
static constexpr uint8_t kShiftPairCount                 = 6;
static constexpr PotId   kShiftPairs[kShiftPairCount][2] = {
    {Pot::SHAPE, Pot::FATNESS},
    {Pot::MOTION, Pot::DRIFTSPEED},
    {Pot::CURVE, Pot::CURVETIME},
    {Pot::SPACE, Pot::VOL},
    {Pot::DELAY, Pot::DELAYTIME},
    {Pot::REVERB, Pot::REVERBSIZE},
};

// ---------------------------------------------------------------------------
// HardwarePicoIO — IHardwareIO implementation for Raspberry Pi Pico 2.
//
// The gXxx globals are the single source of truth for every parameter: the
// engine, preset save, SysEx dump, CC feedback and the LEDs all read them.
// The knobs are one writer among several (web, MIDI, serial, preset recall),
// so the only question a physical knob raises is *when it may write* — which
// is what PotTakeover answers.  updatePots() is the one place knob positions
// become parameter values; readPot() stays a pure read of the globals, so
// IOBridge::fillSynthParams() is unaffected either way.
//
// M37d bootstrap: pot reads derive from the gXxx globals (set by MIDI/serial)
// normalised back to the 0–1 contract that IOBridge expects.  When the ADC mux
// driver lands, readPotRaw() queries the physical multiplexed ADC and knob
// motion starts flowing through updatePots() into those same globals.
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

    /**
     * IHardwareIO pot read — the parameter's current position, 0–1.
     * Always reads the gXxx globals, never the ADC: knob motion reaches the
     * globals through updatePots() first, so this stays the one consistent
     * view for fillSynthParams() regardless of takeover state.
     */
    float readPot(PotId id) override { return readPotNorm(id); }

    // -----------------------------------------------------------------------
    // M62 — knob takeover.
    //
    // Call once per control tick, before fillSynthParams().  For every pot:
    // read the knob, ask PotTakeover what the parameter should now be, and
    // write the result back.  External writes need no cooperation from the
    // writer — they are detected by diffing the parameter against what the
    // takeover layer last wrote, so MIDI CC, SysEx apply, the serial console
    // and preset recall are all covered by the same three lines.
    // -----------------------------------------------------------------------
    void updatePots()
    {
        _takeover.setMode(gPotTakeoverMode);

        // SHIFT edge: a shared knob now addresses its other parameter.  The
        // one it just stopped addressing has been left wherever it was, and
        // the one it now addresses has not seen the knob move — so neither may
        // inherit the other's position.  Detaching both sides of every pair on
        // the edge makes SHIFT+knob obey exactly the same takeover rule as the
        // web does, which is the behaviour the shared knob needs anyway.
        const bool shiftNow = _btnShift.isDown();
        if(shiftNow != _shiftPrev)
        {
            _shiftPrev = shiftNow;
            for(uint8_t i = 0; i < kShiftPairCount; i++)
            {
                _takeover.detach(kShiftPairs[i][0]);
                _takeover.detach(kShiftPairs[i][1]);
            }
        }

        // Pot::kCount, not PotId::POT_COUNT — the HAL sizes for the largest
        // panel it expects to host; only AlloyFlux's own slots are assigned.
        for(uint8_t i = 0; i < Pot::kCount; i++)
        {
            const PotId id = (PotId)i;
            // ROOT is deliberately not takeover-managed.  Pitch does not flow
            // knob → global → engine like the others: fillSynthParams() sums
            // the ROOT knob with the V/Oct jack straight into p.baseFreq, and
            // gBaseFreq belongs to MIDI Note On, which main.cpp gives priority
            // while a note is held.  Writing gBaseFreq from the knob here
            // would fight every note-on.  The knob's position still reaches
            // the web: CC 16 is in the patch dump and the CC feedback diff.
            if(id == Pot::ROOT)
                continue;

            const float phys = readPotRaw(id);
            const float cur  = readPotNorm(id);
            const float next = _takeover.process(id, phys, cur);
            if(next != cur)
                writePotNorm(id, next);
        }
    }

    /** Declare the knobs to be the truth now — backs `pot sync`. */
    void reattachPots() { _takeover.reattachAll(); }

    /** True while this knob is waiting to be moved before it takes over. */
    bool potDetached(PotId id) const { return _takeover.isDetached(id); }

    // -----------------------------------------------------------------------
    // Pot normalisation — engineering-unit gXxx globals ↔ 0–1.
    // Each conversion is the exact inverse of the scaling applied in
    // IOBridge::fillSynthParams(), so the round-trip is lossless.
    // -----------------------------------------------------------------------
    float readPotNorm(PotId id)
    {
        switch(id)
        {
            case Pot::ROOT:
            {
                // gBaseFreq (Hz) → V/Oct → 0–1 normalised
                //   voct  = log2(f / 440)
                //   norm  = (voct + 4) / 8      (range ±4 V maps to 0–1)
                float voct = log2f(gBaseFreq / 440.0f);
                return (voct + 4.0f) / 8.0f;
            }
            case Pot::RELATION: return gRelation / 24.0f; // 0–24 st → 0–1
            case Pot::SHAPE: return gShape;               // already 0–1
            case Pot::MOTION: return gMotion;             // already 0–1
            case Pot::COLOR: return gColor;               // already 0–1
            case Pot::CURVE: return gCurve;               // already 0–1
            case Pot::SPACE: return gSpace / 2.0f;        // 0–2 → 0–1
            case Pot::DELAY: return gDelayMix;            // already 0–1
            case Pot::REVERB: return gRevMix;             // already 0–1
            case Pot::FATNESS: return gFatness;           // already 0–1
            case Pot::DRIFTSPEED:
                // gDriftSpeed is in [0.001, 0.10] coeff; normalise to 0–1
                return (gDriftSpeed - 0.001f) / (0.10f - 0.001f);
            case Pot::CURVETIME:
                // gCurveTime is a 0.25–4.0× time scale; normalise to 0–1
                return (gCurveTime - 0.25f) / (4.0f - 0.25f);
            case Pot::VOL: return gVolume; // already 0–1
            case Pot::DELAYTIME:
                // gDelayTime is in ms over [10, DELAY_MAX_MS]; normalise to 0–1
                return (gDelayTime - 10.0f) / ((float)DELAY_MAX_MS - 10.0f);
            case Pot::REVERBSIZE: return gRevSize; // already 0–1
            default: return 0.5f;
        }
    }

    /**
     * Write a 0–1 knob position back into the parameter it drives — the exact
     * inverse of readPotNorm(). Only updatePots() calls this; every other
     * writer (MIDI, serial, SysEx, preset load) sets the globals directly and
     * is detected as an external write on the following tick.
     */
    void writePotNorm(PotId id, float v)
    {
        switch(id)
        {
            case Pot::ROOT:
                // norm → ±4 V/Oct → Hz.  Not reached today (updatePots skips
                // ROOT); kept exact so the inverse pair stays complete.
                gBaseFreq = 440.0f * exp2f(v * 8.0f - 4.0f);
                break;
            case Pot::RELATION: gRelation = v * 24.0f; break;
            case Pot::SHAPE: gShape = v; break;
            case Pot::MOTION: gMotion = v; break;
            case Pot::COLOR: gColor = v; break;
            case Pot::CURVE: gCurve = v; break;
            case Pot::SPACE: gSpace = v * 2.0f; break;
            case Pot::DELAY: gDelayMix = v; break;
            case Pot::REVERB: gRevMix = v; break;
            case Pot::FATNESS: gFatness = v; break;
            case Pot::DRIFTSPEED:
                gDriftSpeed = 0.001f + v * (0.10f - 0.001f);
                break;
            case Pot::CURVETIME: gCurveTime = 0.25f + v * (4.0f - 0.25f); break;
            case Pot::VOL: gVolume = v; break;
            case Pot::DELAYTIME:
                gDelayTime = 10.0f + v * ((float)DELAY_MAX_MS - 10.0f);
                break;
            case Pot::REVERBSIZE: gRevSize = v; break;
            default: break;
        }
    }

    /**
     * Physical knob position, 0–1 — the ADC seam.
     *
     * When the multiplexed ADC driver lands this becomes the only thing that
     * changes: sample the mux channel for `id`, scale to 0–1, and slew-limit
     * or dead-band it before returning.  PotTakeover expects a de-noised
     * value; its movement threshold decides when a knob was *touched*, and
     * cannot double as a filter for raw ADC jitter.
     *
     * On a shared (SHIFT-secondary) knob both PotIds of the pair read the same
     * physical channel — the SHIFT edge handling in updatePots() is what keeps
     * them from stealing each other's position.
     */
    float readPotRaw(PotId id)
    {
#if POT_ADC_PRESENT
#error "ADC mux driver not implemented — see readPotRaw()"
#else
        // No ADC yet: report the parameter's own position so knob and value
        // never disagree and the takeover layer stays inert.
        return readPotNorm(id);
#endif
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
            case Cv::VOCT:
                return 0.0f; // pitch from ROOT pot only until ADC lands
            case Cv::GATE: return gGateHigh ? 5.0f : 0.0f;
            default: return 0.0f;
        }
    }

    bool isPatched(CVId id) override
    {
        switch(id)
        {
            case Cv::VOCT: return false;        // no V/Oct jack yet
            case Cv::GATE: return gGatePatched; // set by MIDI / button combo
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
            case Btn::MODE: return _btnMode.isDown();
            case Btn::SHIFT: return _btnShift.isDown();
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
        // Panel role -> position in the physical daisy chain.
        // Chain runs D12 -> D13 -> D14 -> D15 -> D16 -> D21 -> D22, while
        // the panel map is ordered VOICE_L(D12) VOICE_R(D22) MOD_L(D13)
        // MOD_R(D21) MODE(D14) SHIFT(D16) CENTRE(D15). Traced from the
        // schematic netlist; if a board revision reroutes the chain, this
        // table is the only thing that changes.
        static const uint8_t kChainPos[Led::kCount] = {
            0, // VOICE_L -> D12, first on the chain
            6, // VOICE_R -> D22, last
            1, // MOD_L   -> D13
            5, // MOD_R   -> D21
            2, // MODE    -> D14
            4, // SHIFT   -> D16
            3, // CENTRE  -> D15
        };

        const uint8_t i = (uint8_t)id;
        if(i >= Led::kCount)
            return;
        _dotstars.setPixel(kChainPos[i], r, g, b);

        if(id == Led::MODE)
            _btnLedMode = _luma8(r, g, b);
        else if(id == Led::SHIFT)
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

    PotTakeover _takeover;          // M62 — knob vs. web/MIDI arbitration
    bool        _shiftPrev = false; // SHIFT state at the last updatePots()

    Apa102<Led::kCount> _dotstars;
    uint8_t             _btnLedMode  = 0u;
    uint8_t             _btnLedShift = 0u;
};
