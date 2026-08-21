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

/**
 * How a value is *shown*, as opposed to how it is stored.
 *
 * Distinct from `scale` and `skew`, which decide where a value sits on the
 * control: those change the mapping from travel to value, this changes only the
 * number printed next to it. The stored value, the CC on the wire and the float
 * the engine multiplies by are identical either way.
 *
 * It exists because a linear gain is stored the way the DSP wants it and read
 * the way an ear works, and those are not the same number. Alloy Coil's exciter
 * level is the case: the engine wants a multiplier, so the target holds 0–2 and
 * unity is 1.0 — which reads as "already at half" to anyone who has met a fader
 * before. As dB it is −inf…+6 with unity at 0, and the neutral point names
 * itself.
 *
 * ⚠ Not reachable through the existing `display` block in params.json. That one
 * re-parameterises a control by giving it different bounds over the same
 * travel — AlloyFlux's root is Hz stored and semitones shown — and it works
 * because both parameterisations are affine in travel. dB of a square-law taper
 * is not: it goes to −inf at the bottom stop, which no min/max pair expresses.
 */
enum class ParamDisplay : uint8_t
{
    Direct = 0, ///< the number shown IS the stored value
    GainDb,     ///< stored value is a linear gain; shown as 20·log10(v)
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
     * Display transform. Trailing member with a 0 default, so a row written
     * without it is Direct — which is what all but one of them are.
     */
    ParamDisplay display;

    /**
     * Parameter value → the number a UI prints. See ParamDisplay.
     *
     * Returns −INFINITY for a zero gain, which is the honest answer and is what
     * the two front ends render as "−inf dB". Callers that format this must
     * therefore test isfinite() before reaching for a printf.
     */
    float toDisplay(float v) const
    {
        if(display == ParamDisplay::GainDb)
            return v > 0.0f ? 20.0f * log10f(v) : -INFINITY;
        return v;
    }

    /// The inverse of toDisplay() — for typed-in values in a UI's text entry.
    float fromDisplay(float d) const
    {
        if(display == ParamDisplay::GainDb)
            return powf(10.0f, d / 20.0f);
        return d;
    }

    /**
     * How many decimals a readout should carry.
     *
     * Taken from the parameter's *range and CC resolution*, never from the
     * current value, so a control keeps one precision all the way round instead
     * of gaining and losing digits as it is turned.
     *
     * The resolution half matters more than it looks. A CC is 7 bits, so a
     * parameter is only known to one part in 127 of its range, and printing past
     * that claims accuracy the wire cannot carry. AlloyFlux's root is the case
     * that gives it away: ±48 semitones across 128 steps, so exact centre would
     * need CC 63.5 and an initialised patch comes back as CC 64 — 0.378 st.
     * Shown to one decimal that reads as a module sitting 38 cents sharp on a
     * default patch. One CC step there is 0.76 st, so the honest rendering is a
     * whole number, and 0.378 rounds to the 0 the user expects.
     *
     * ⚠ displayDigits() in paramMapTypes.ts is the mirror of this, and is a
     * superset: it also handles `step` and `ccRange`, two columns the web map
     * carries and this descriptor does not. The two therefore agree exactly for
     * any parameter declaring neither — which is all of Alloy Coil's. A module
     * that uses either needs those columns here before its VCV build can rely
     * on the readouts matching the configurator's.
     */
    int displayDigits() const
    {
        // A dB readout has no single resolution to derive: on a square-law
        // taper the same CC step is a fraction of a dB at the top of the travel
        // and tens of dB near the bottom. One decimal is the finest reading
        // that is honest anywhere on the sweep.
        if(display == ParamDisplay::GainDb)
            return 1;
        const float span = fmaxf(fabsf(minVal), fabsf(maxVal));
        if(span >= 100.0f)
            return 0;
        // A log parameter's step varies across its travel, so it falls back to
        // the range test above.
        if(scale == ParamScale::Log)
            return span >= 10.0f ? 1 : 2;
        const float res = (maxVal - minVal) / 127.0f;
        if(res >= 0.5f)
            return 0;
        if(res >= 0.05f)
            return 1;
        return res >= 0.005f ? 2 : 3;
    }

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
