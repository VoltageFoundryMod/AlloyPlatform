#pragma once

// Arduino.h arrives transitively via ButtonEngine.h (INPUT_PULLUP, digitalRead).
// This header must only be compiled in the hardware (Arduino-Pico) build.
#include "io/ButtonEngine.h"
#include "io/HardwareIO.h" // PotId/CVId/ButtonId/LightId positional slots
#include "io/PanelMap.h"   // Pot::/Cv::/Btn::/Led:: — Alloy Coil's slot names
#include "io/param_map.h"  // paramMap_findByName()
#include "params.h"

// ---------------------------------------------------------------------------
// Panel button GPIOs.
//
// Not Alloy Coil's numbers — the board's. Alloy Coil and AlloyFlux share the
// MainPCB, so these are SW2 and SW3 whichever firmware is flashed, on the same
// two pins AlloyFlux reads them from. Only the silkscreen differs: SW2 says
// MODE on one panel and WARP on the other, which is precisely the distinction
// io/PanelMap.h exists to hold.
//
// Overridable at build time for a board revision that moves them.
// ---------------------------------------------------------------------------
#ifndef PIN_BUTTON_WARP
#define PIN_BUTTON_WARP 10 // GP10, board SW2 — AlloyFlux calls this slot MODE
#endif
#ifndef PIN_BUTTON_SHIFT
#define PIN_BUTTON_SHIFT 11 // GP11, board SW3
#endif

// The multiplexed ADC driver is not wired yet — see readPotRaw(). Flipping
// this to 1 is the whole seam: nothing else in this class knows the difference.
#ifndef POT_ADC_PRESENT
#define POT_ADC_PRESENT 0
#endif

// ---------------------------------------------------------------------------
// Pot slot → params.json `name`.
//
// The indirection is deliberate: it means readPot() carries no curve of its
// own. Every taper lives in params.json, ParamDescriptor applies it in both
// directions, and test/params_check.cpp already asserts that IOBridge and the
// descriptor agree to within float noise across the whole travel. A
// hand-written inverse per knob — which is what AlloyFlux's version of this
// class does — is a second copy of twelve curves that can drift from the
// manifest, and drift is exactly what params_check exists to catch.
//
// Pot::VOL, FBHPF and EXCITE are the SHIFT-secondaries; they are ordinary slots
// here, and only SHIFT decides which of a shared pair a reading belongs to.
//
// File scope rather than a class member so the table needs no out-of-class
// definition when it is odr-used — the same reason AlloyFlux keeps kShiftPairs
// out here.
// ---------------------------------------------------------------------------
struct CoilPotParam
{
    PotId       id;
    const char *name;
};
static constexpr uint8_t     kCoilPotParamCount             = 12;
static constexpr CoilPotParam kCoilPotParams[kCoilPotParamCount] = {
    {Pot::PITCH, "pitch"},
    {Pot::FBGAIN, "fbgain"},
    {Pot::FBBODY, "fbbody"},
    {Pot::ECHOTIME, "echotime"},
    {Pot::ECHOSEND, "echosend"},
    {Pot::ECHOFB, "echofb"},
    {Pot::REVDECAY, "revdecay"},
    {Pot::REVMIX, "revmix"},
    {Pot::FBLPF, "fblpf"},
    {Pot::VOL, "vol"},
    {Pot::FBHPF, "fbhpf"},
    {Pot::EXCITE, "excite"},
};

// ---------------------------------------------------------------------------
// HardwarePicoIO — IHardwareIO for Alloy Coil on the Raspberry Pi Pico 2.
//
// Partial by design, and the part that is real is the part that is wired: the
// two panel switches are on the proto board, the pot mux and the CV jacks are a
// later phase. So buttons read the hardware and everything else answers the way
// an unwired panel should — which is *not* the same as answering zero.
//
// A pot with no ADC reports the position its own parameter currently holds, via
// that parameter's row in params.json. That is what makes the class safe to
// hand to fillCoilParams() whenever the driver lands, or before: the bridge
// converts position → value with exactly the curve the descriptor converts
// value → position with, so the round trip is the identity and a knob that does
// not exist cannot overwrite a value MIDI just set. The alternative — a stub
// returning 0.5 — would snap all twelve parameters to mid-travel on the first
// control tick, which is a trap sitting one call away from a working module.
//
// The firmware does not call fillCoilParams() yet regardless; it calls
// fillCoilButtons(), which is the buttons alone. See modules/alloycoil/src/main.cpp.
//
// ButtonEngine references are injected by main.cpp so the same two objects back
// both this class and any edge/combo handling there — one debounce state, not
// two that can disagree.
// ---------------------------------------------------------------------------
class HardwarePicoIO : public IHardwareIO
{
  public:
    HardwarePicoIO(ButtonEngine &btnWarp, ButtonEngine &btnShift)
    : _btnWarp(btnWarp), _btnShift(btnShift)
    {
    }

    /**
     * Cache the descriptor row behind each pot slot. Call once from setup(),
     * after the manifest is available (it is static, so any time before the
     * first control tick).
     *
     * Cached rather than looked up per read: readPot() runs Pot::kCount times
     * per control tick, and paramMap_findByName() is a strcmp walk. Sixteen
     * walks of a twelve-row table, 128 times a second, to answer a question
     * whose answer never changes.
     */
    void begin()
    {
        for(uint8_t i = 0; i < Pot::kCount; i++)
            _potRow[i] = nullptr;
        for(uint8_t i = 0; i < kCoilPotParamCount; i++)
            _potRow[(uint8_t)kCoilPotParams[i].id]
                = paramMap_findByName(kCoilPotParams[i].name);
    }

    // -----------------------------------------------------------------------
    // Pots — no ADC yet. See the class comment for why this is not a stub.
    // -----------------------------------------------------------------------
    float readPot(PotId id) override { return readPotRaw(id); }

    /**
     * Physical knob position, 0–1 — the ADC seam.
     *
     * When the multiplexed ADC driver lands this is the only thing that has to
     * change: sample the mux channel for `id`, scale to 0–1, and dead-band it
     * before returning. On a shared (SHIFT-secondary) knob both PotIds of a
     * Pot::kShiftPairs entry read the same physical channel, and SHIFT decides
     * which of the two the reading belongs to.
     */
    float readPotRaw(PotId id)
    {
#if POT_ADC_PRESENT
#error "ADC mux driver not implemented — see readPotRaw()"
#else
        const uint8_t i = (uint8_t)id;
        if(i >= Pot::kCount || _potRow[i] == nullptr)
            return 0.5f; // POT_10..POT_13 — unassigned, and nothing reads them
        return _potRow[i]->toPos(*_potRow[i]->target);
#endif
    }

    // -----------------------------------------------------------------------
    // CV jacks — none wired. Unpatched rather than zero: fillCoilParams() gates
    // every CV sum on isPatched(), so this leaves the knob path alone entirely
    // instead of summing a fabricated 0 V into it.
    // -----------------------------------------------------------------------
    float readCV(CVId) override { return 0.0f; }
    bool  isPatched(CVId) override { return false; }

    // -----------------------------------------------------------------------
    // Buttons — the wired part. Instantaneous debounced state, not edges:
    // WARP is held rather than pressed, and IOBridge wants "is it down now".
    // -----------------------------------------------------------------------
    bool readButton(ButtonId id) override
    {
        switch(id)
        {
            case Btn::WARP: return _btnWarp.isDown();
            case Btn::SHIFT: return _btnShift.isDown();
            default: return false;
        }
    }

    // -----------------------------------------------------------------------
    // LEDs — the APA102 chain and the two switch backlights are not driven
    // yet. AlloyFlux's HardwarePicoIO already has the transport (io/Apa102.h)
    // and Alloy Coil already has the colour language (io/CoilLeds.h); what is
    // missing is only the wiring between them, which is the same phase as the
    // pot mux. Staged writes are dropped and showLights() latches nothing.
    // -----------------------------------------------------------------------
    void writeLight(LightId, float, float, float) override {}

  private:
    /// Descriptor row behind each pot slot, or nullptr for an unassigned one.
    /// Filled by begin(); see kCoilPotParams above.
    const ParamDescriptor *_potRow[Pot::kCount];

    ButtonEngine &_btnWarp;
    ButtonEngine &_btnShift;
};
