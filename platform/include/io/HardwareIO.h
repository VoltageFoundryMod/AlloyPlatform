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
    // ---- 9 physical panel knobs (same on hardware and VCV) ----
    ROOT = 0, ///< Root pitch (V/Oct centre)
    RELATION, ///< RELATION — semitone offset for voice 2 (0–24 st)
    SHAPE,    ///< Waveform shape (sine → tri → saw → pulse → hollow) [0–1]
    MOTION,   ///< Drift + chorus depth [0–1]
    COLOR,    ///< FM depth / ensemble Hz spread [0–1]
    CURVE,    ///< Envelope curve character (pluck ↔ swell) [0–1]
    SPACE,    ///< Stereo width [0–1]
    DELAY,    ///< Delay wet mix [0–1]   — 0 = hard bypass (M56)
    REVERB,   ///< Reverb wet mix [0–1]  — 0 = hard bypass (M56)
    // ---- SHIFT-secondary parameters (VCV: hidden params / context menu sliders;
    //      hardware: same physical knob read when SHIFT held) ----
    FATNESS,    ///< Sub-oscillator level [0–1]  (SHIFT+SHAPE on hardware)
    DRIFTSPEED, ///< Drift glide rate [0–1]       (SHIFT+MOTION on hardware)
    CURVETIME, ///< Envelope time scale [0–1] → 0.25–4× (SHIFT+CURVE on hardware)
    VOL,       ///< Master output volume [0–1]   (SHIFT+SPACE on hardware)
    DELAYTIME,  ///< Delay time [0–1] → 10–300 ms (SHIFT+DELAY on hardware)
    REVERBSIZE, ///< Reverb plate size [0–1]      (SHIFT+REVERB on hardware)
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

/**
 * RGB LED identifiers (APA102/SK9822 on hardware; light widget in VCV).
 *
 * Order matches LedId in io/LedEngine.h one-for-one — LedEngine::writeTo()
 * casts between them, so the two enums must stay in sync.
 *
 *      VOICE_L ·  ·  · VOICE_R      top:        voice activity
 *     MOD_L  ·  ·  ·  · MOD_R       mid-top:    motion / modulation
 *        MODE  ·   SHIFT            mid-bottom: mode / shift-drone
 *           · CENTRE ·              bottom:     heartbeat / global
 */
enum class LightId : uint8_t
{
    VOICE_L = 0, ///< D12 / VCV LED1 — ROOT voice activity, left channel
    VOICE_R,     ///< D22 / VCV LED7 — RELATION voice activity, right channel
    MOD_L,       ///< D13 / VCV LED2 — motion and modulation depth
    MOD_R,       ///< D21 / VCV LED6 — secondary modulation / stereo position
    MODE,        ///< D14 / VCV LED3 — current voice mode (near MODE_SW)
    SHIFT,       ///< D16 / VCV LED5 — shift state / drone (near SHIFT_SW)
    CENTRE,      ///< D15 / VCV LED4 — heartbeat / global confirmation
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
     *
     * On a clocked LED chain this only stages the colour; nothing reaches the
     * panel until showLights() latches the frame.
     */
    virtual void writeLight(LightId id, float r, float g, float b) = 0;

    /**
     * Latches every staged writeLight() as one frame.
     * Called once per control tick, after the last writeLight().
     *
     * Required by daisy-chained LEDs (APA102/SK9822), where the whole chain is
     * one shift register: shifting on every writeLight() would push each colour
     * through the wrong LED on its way down and tear the frame. Platforms whose
     * lights are individually addressable — VCV — need nothing here, hence the
     * default no-op rather than a pure virtual.
     */
    virtual void showLights() {}

    virtual ~IHardwareIO() = default;
};
