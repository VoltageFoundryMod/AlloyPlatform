#pragma once

#include <math.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// ParamDescriptor — one row per externally-controllable parameter.
//
// A module declares its parameters once in params.json; tools/gen_params.py
// emits the table (param_manifest.generated.h) that every transport then reads.
// Nothing else in the firmware should carry a per-parameter list.
//
// The descriptor deliberately carries UI metadata (label, category, unit)
// alongside the DSP range. Those used to live only in the web configurator's
// hand-maintained mirror of the CC table, which is exactly the pair that drifts.
// ---------------------------------------------------------------------------

enum class ParamScale : uint8_t
{
    Linear = 0,
    Log, ///< value = min * (max/min)^t — for frequencies
};

struct ParamDescriptor
{
    uint8_t     cc;    ///< MIDI CC number; also the SysEx patch-dump identity
    const char *name;  ///< serial command name and web key — stable API
    const char *label; ///< human-readable label for UIs
    const char *category; ///< UI grouping
    float       minVal;   ///< value at CC 0
    float       maxVal;   ///< value at CC 127
    float       defVal;   ///< factory default
    ParamScale  scale;
    const char *unit;   ///< display suffix, nullptr when unitless
    float      *target; ///< where a decoded value is written

    /**
     * Travel skew: control position is raised to this power before the range
     * is applied. 1.0 is no skew and is what almost every parameter wants.
     *
     * This exists for the case where a range is honest but its *useful* part
     * is bunched at one end, so most of the control does nothing audible.
     * Alloy Coil's feedback gain is the example: −60…+12 dB is the real range, but
     * a feedback loop does not start ringing until roughly −15 dB, so with a
     * plain linear mapping the bottom 62 % of the travel is dead.
     *
     *   skew < 1  value rises fast then slows — expands the TOP of the range
     *             across the control. Use when the action is near max.
     *   skew > 1  the mirror image — expands the BOTTOM.
     *
     * Note this is not a substitute for `scale`: Log handles ranges that are
     * multiplicative (frequencies), and cannot be used at all where the range
     * crosses or touches zero. Skew works on any range, including dB values
     * that go negative.
     */
    float skew;

    /**
     * Control position 0–1 → parameter value, honouring scale and skew.
     *
     * "Control position" is knob travel, CC/127, or slider travel — the same
     * quantity in all three, which is the point: a knob, a CC and the web
     * slider only agree because they all pass through this one curve.
     */
    float fromPos(float t) const
    {
        if(t < 0.0f)
            t = 0.0f;
        else if(t > 1.0f)
            t = 1.0f;
        if(skew != 1.0f && t > 0.0f)
            t = powf(t, skew);
        if(scale == ParamScale::Log)
            return minVal * powf(maxVal / minVal, t);
        return minVal + t * (maxVal - minVal);
    }

    /**
     * Parameter value → control position 0–1, the inverse of fromPos().
     *
     * This is what a UI needs to place a control at a given value — notably the
     * VCV module's `configParam` defaults, which are positions, not values.
     * Deriving them keeps a curve change in params.json from silently moving
     * every knob's boot position.
     */
    float toPos(float v) const
    {
        float t;
        if(scale == ParamScale::Log)
            t = logf(v / minVal) / logf(maxVal / minVal);
        else
            t = (v - minVal) / (maxVal - minVal);
        if(t <= 0.0f)
            return 0.0f;
        if(t >= 1.0f)
            return 1.0f;
        if(skew != 1.0f)
            t = powf(t, 1.0f / skew);
        return t;
    }

    /// CC 0–127 → parameter value.
    float fromCC(uint8_t v) const { return fromPos((float)v / 127.0f); }

    /// Parameter value → CC 0–127, the inverse of fromCC().
    uint8_t toCC(float v) const
    {
        const float t = toPos(v);
        if(t <= 0.0f)
            return 0;
        if(t >= 1.0f)
            return 127;
        return (uint8_t)(t * 127.0f + 0.5f);
    }
};

// ---------------------------------------------------------------------------
// Discrete parameters — enums, bools and small integer selects.
//
// Each option owns a CC band, so a controller sweeping 0–127 steps cleanly
// through the choices, and `value` is what actually gets stored. The two are
// separate because the stored encoding is not always the option index: the
// sub-oscillator stores 1 or 2, and the scale quantizer stores a ScaleId whose
// band is a single CC wide.
// ---------------------------------------------------------------------------

struct ParamOption
{
    const char *label;
    uint8_t     ccMin; ///< inclusive
    uint8_t     ccMax; ///< inclusive
    uint8_t     value; ///< written to the target when this option is selected
};

struct EnumParamDescriptor
{
    uint8_t     cc;
    const char *name;
    const char *label;
    const char *category;
    /// Target must be one byte wide: a `bool`, a `uint8_t`, or an enum class
    /// with an explicit `: uint8_t` underlying type. All of AlloyFlux's are.
    uint8_t           *target;
    const ParamOption *options;
    uint8_t            optionCount;
    uint8_t            defValue; ///< stored when no band matches
    /// Optional side effect, run after the store. Null when there is none.
    void (*onChange)();

    /// CC 0–127 → stored value.
    uint8_t fromCC(uint8_t v) const
    {
        for(uint8_t i = 0; i < optionCount; i++)
        {
            if(v >= options[i].ccMin && v <= options[i].ccMax)
                return options[i].value;
        }
        return defValue;
    }

    /// Stored value → the CC that selects it (the low edge of its band).
    uint8_t toCC(uint8_t stored) const
    {
        for(uint8_t i = 0; i < optionCount; i++)
        {
            if(options[i].value == stored)
                return options[i].ccMin;
        }
        return 0;
    }
};
