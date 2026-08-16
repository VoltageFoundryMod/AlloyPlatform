#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// I/O identifiers — positional, deliberately meaningless.
//
// These name *slots*, not functions.  The platform hosts more than one module
// and has no business knowing that slot 1 is a pitch knob on one of them; a
// module maps its own vocabulary onto these in its own header (AlloyFlux:
// modules/alloyflux/include/io/PanelMap.h) so `Pot::ROOT` reads the same as it
// always did at the call site while the HAL stays generic.
//
// The counts are ceilings for the platform, not per-module truth: a module
// declares its own count and simply leaves the rest unassigned.  Unregistered
// slots return safe defaults (0.5 for pots, 0 for CV).
//
// One consequence worth stating: **slot order is the flash format.** Preset
// blobs and the takeover state array are indexed by these, so inserting a slot
// in the middle of a module's map renumbers everything after it. Append.
// ---------------------------------------------------------------------------

/**
 * Knob slots.  Sized for nine physical knobs plus their SHIFT-secondaries,
 * which is the most a Eurorack panel of this size can carry.
 */
enum class PotId : uint8_t
{
    POT_1 = 0,
    POT_2,
    POT_3,
    POT_4,
    POT_5,
    POT_6,
    POT_7,
    POT_8,
    POT_9,
    POT_10,
    POT_11,
    POT_12,
    POT_13,
    POT_14,
    POT_15,
    POT_16,
    POT_COUNT
};

/** CV input jack slots. */
enum class CVId : uint8_t
{
    CV_1 = 0,
    CV_2,
    CV_3,
    CV_4,
    CV_5,
    CV_6,
    CV_7,
    CV_8,
    CV_COUNT
};

/** Panel button / momentary switch slots. */
enum class ButtonId : uint8_t
{
    BUTTON_1 = 0,
    BUTTON_2,
    BUTTON_3,
    BUTTON_4,
    BUTTON_COUNT
};

/** RGB LED slots (APA102/SK9822 on hardware; light widgets in VCV). */
enum class LightId : uint8_t
{
    LIGHT_1 = 0,
    LIGHT_2,
    LIGHT_3,
    LIGHT_4,
    LIGHT_5,
    LIGHT_6,
    LIGHT_7,
    LIGHT_8,
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
