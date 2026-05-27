#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// I/O identifiers
//
// These enums define every hardware point that the IHardwareIO interface can
// address.  All IDs are defined up-front (M37d) even if not yet wired on both
// platforms; unregistered slots return safe defaults (0.5 for pots, 0 for CV).
// ---------------------------------------------------------------------------

/** Panel knobs (pots).  Order reflects physical left→right / top→bottom layout. */
enum class PotId : uint8_t
{
    // ---- 7 physical panel knobs (same on hardware and VCV) ----
    ROOT = 0, ///< Root pitch (V/Oct centre)
    RELATION, ///< RELATION — semitone offset for voice 2 (0–24 st)
    SHAPE,    ///< Waveform shape (sine → tri → saw → pulse → hollow) [0–1]
    MOTION,   ///< Drift + chorus depth [0–1]
    COLOR,    ///< FM depth / ensemble Hz spread [0–1]
    CURVE,    ///< Envelope curve character (pluck ↔ swell) [0–1]
    SPACE,    ///< Stereo width [0–1]
    // ---- SHIFT-secondary parameters (VCV: hidden params / context menu sliders;
    //      hardware: same physical knob read when SHIFT held) ----
    FATNESS,    ///< Sub-oscillator level [0–1]  (SHIFT+SHAPE on hardware)
    DRIFTSPEED, ///< Drift glide rate [0–1]       (SHIFT+MOTION on hardware)
    VOL,        ///< Master output volume [0–1]   (SHIFT+SPACE on hardware)
    POT_COUNT
};

/** CV input jacks. */
enum class CVId : uint8_t
{
    VOCT = 0, ///< V/Oct pitch — readCV() returns volts (bipolar, ±5 V typical)
    GATE,     ///< Gate / trigger — readCV() returns 0.0 or ≥1.0 (high = gate)
    REL_CV,   ///< RELATION CV — bipolar, normalised to ±1.0
    SHP_CV,   ///< SHAPE CV   — bipolar, normalised to ±1.0
    MTN_CV,   ///< MOTION CV  — bipolar, normalised to ±1.0
    SPC_CV,   ///< SPACE CV   — bipolar, normalised to ±1.0
    FM_IN,    ///< FM / COLOR CV — bipolar, normalised to ±1.0
    CV_COUNT
};

/** Panel buttons / momentary switches. */
enum class ButtonId : uint8_t
{
    MODE = 0, ///< Mode cycle button (GP10 on hardware)
    SHIFT,    ///< Shift / trig button (GP11 on hardware)
    BUTTON_COUNT
};

/** RGB LED identifiers (APA102/SK9822 on hardware; light widget in VCV). */
enum class LightId : uint8_t
{
    LED0 = 0,
    LED1,
    LED2,
    LED3,
    LED4,
    LIGHT_COUNT
};

// ---------------------------------------------------------------------------
// IHardwareIO — pure interface for all platform I/O.
//
// Implementations:
//   HardwarePicoIO  (include/io/HardwarePicoIO.h) — Raspberry Pi Pico 2 hardware
//   VCVRackIO       (vcv-plugin/src/VCVRackIO.h)  — VCV Rack 2 module
//
// Shared logic that reads from this interface lives in io/IOBridge.h and is
// #include'd by both main.cpp and AlloyFlux.cpp.
// ---------------------------------------------------------------------------
struct IHardwareIO
{
    /**
     * Returns a 0.0–1.0 normalised knob position for the given pot.
     * Unregistered pots return 0.5 (mid position).
     */
    virtual float readPot(PotId id) = 0;

    /**
     * Returns the conditioned CV value for the given jack.
     *   VOCT  : volts, signed (V/Oct standard — add directly to a V/Oct sum)
     *   GATE  : 0.0 (low) or the raw voltage (typically ≥1.0 when high)
     *   Other : raw voltage; implementations may normalise to a ±1 range
     * Returns 0.0 when the jack is unpatched or unregistered.
     */
    virtual float readCV(CVId id) = 0;

    /**
     * Returns true when a cable / jack is present for the given CV input.
     * Used to distinguish "drone" (unpatched gate) from "envelope" mode.
     */
    virtual bool isPatched(CVId id) = 0;

    /**
     * Returns the instantaneous debounced state of a panel button.
     * true = button is currently pressed / held.
     */
    virtual bool readButton(ButtonId id) = 0;

    /**
     * Writes a normalised RGB colour (0.0–1.0 per channel) to a panel LED.
     * No-op for unregistered or unimplemented lights.
     */
    virtual void writeLight(LightId id, float r, float g, float b) = 0;

    virtual ~IHardwareIO() = default;
};
