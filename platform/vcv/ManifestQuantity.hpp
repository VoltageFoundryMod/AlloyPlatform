#pragma once
#include <rack.hpp>

#include <cmath>
#include <string>

#include "ParamDescriptor.h"

// ---------------------------------------------------------------------------
// ManifestQuantity — a Rack ParamQuantity that reads its curve off the
// generated parameter manifest.
//
// Every knob in these plugins is configured `0.f, 1.f` and holds a control
// *position*, not a value: the Rack params emulate hardware pots, and IOBridge
// applies whatever curve params.json declares on the way to the engine. That is
// deliberate and load-bearing — knob travel, CC/127 and slider travel are the
// same quantity, which is the only reason a knob, a MIDI CC and the web
// configurator land on the same value.
//
// What it cost was the tooltip. Rack has no idea about the curve, so it printed
// the shaft angle: Body read "0.42" instead of 50 ms, Echo Time "0.35" instead
// of 0.5 s, and Alloy Coil's exciter level read "0.70711" — which is exactly
// toPos(1.0) through a square-law taper, and exactly nobody's idea of unity
// gain. Rack's own displayMultiplier/displayOffset could not fix it: they are
// affine, and all but two of these parameters are not.
//
// So the display goes through the descriptor instead. fromPos() is the same
// curve the CC path and the web UI use, and toDisplay() adds the value→readout
// transform on top, so the number in the tooltip is the number the engine has.
// Text entry runs the pair backwards and lands on the right position.
//
// Usage — inside the module constructor, after the manifest is in scope:
//   auto *q = configParam<ManifestQuantity>(SOME_PARAM, 0.f, 1.f, pos, "Name");
//   q->desc = &kParamManifest[i];
//
// Nothing here caches: `desc` is a pointer into a `static const` table with
// static storage duration, so it outlives every widget that reads it.
// ---------------------------------------------------------------------------
struct ManifestQuantity : rack::engine::ParamQuantity
{
    /** Not owned — points into the module's kParamManifest. */
    const ParamDescriptor *desc = nullptr;

    float getDisplayValue() override
    {
        if(!desc)
            return rack::engine::ParamQuantity::getDisplayValue();
        return desc->toDisplay(desc->fromPos(getValue()));
    }

    void setDisplayValue(float displayValue) override
    {
        if(!desc)
        {
            rack::engine::ParamQuantity::setDisplayValue(displayValue);
            return;
        }
        setValue(desc->toPos(desc->fromDisplay(displayValue)));
    }

    /**
     * Precision comes off the descriptor, not from this widget, so a knob in
     * Rack and the same knob in the web configurator round to the same digit.
     * They used to disagree — which meant the two references for the hardware
     * disagreed about what the hardware was doing.
     */
    std::string getDisplayValueString() override
    {
        if(!desc)
            return rack::engine::ParamQuantity::getDisplayValueString();
        const float v = getDisplayValue();
        // A zero gain is -inf dB, and that is the honest reading rather than
        // some very large negative number — toDisplay() returns the infinity on
        // purpose. ASCII minus, matching the negatives %f prints just below.
        if(!std::isfinite(v))
            return v < 0.0f ? "-inf" : "inf";
        return rack::string::f("%.*f", desc->displayDigits(), v);
    }
};
