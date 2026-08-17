#pragma once

#include "io/HardwareIO.h"
#include <rack.hpp>

// ---------------------------------------------------------------------------
// VCVRackIO — IHardwareIO implementation for VCV Rack 2.
//
// Platform code, not a module''s: it maps the positional PotId/CVId/ButtonId/
// LightId slots onto Rack param/input/light indices through assignment tables
// that each module fills in its own constructor. Nothing here knows what a
// slot means, which is what lets AlloyFlux and Audrey share it.
//
// Pot normalisation:  readPot() maps the param's [minVal, maxVal] range → 0–1
//                     so that IOBridge::fillSynthParams() sees the same 0–1
//                     contract regardless of individual param ranges.
// CV reads:           readCV() returns the raw port voltage (V/Oct for VOCT,
//                     gate voltage for GATE, ±V for modulation CVs).
// ---------------------------------------------------------------------------
class VCVRackIO : public IHardwareIO
{
  public:
    explicit VCVRackIO(rack::engine::Module *m) : _m(m)
    {
        for(int i = 0; i < int(PotId::POT_COUNT); ++i)
        {
            _potParam[i] = kNone;
            _potMin[i]   = 0.f;
            _potMax[i]   = 1.f;
        }
        for(int i = 0; i < int(CVId::CV_COUNT); ++i)
            _cvInput[i] = kNone;
        for(int i = 0; i < int(ButtonId::BUTTON_COUNT); ++i)
            _btnParam[i] = kNone;
        for(int i = 0; i < int(LightId::LIGHT_COUNT); ++i)
            _lightBase[i] = kNone;
    }

    // -----------------------------------------------------------------------
    // Registration — call these in the module's constructor, after config().
    // -----------------------------------------------------------------------

    /** Map a pot to a Rack param.  minVal/maxVal must match configParam(). */
    void assignPot(PotId pot, int paramIdx, float minVal, float maxVal)
    {
        _potParam[int(pot)] = paramIdx;
        _potMin[int(pot)]   = minVal;
        _potMax[int(pot)]   = maxVal;
    }

    /** Map a CV id to a Rack input port index. */
    void assignCV(CVId cv, int inputIdx) { _cvInput[int(cv)] = inputIdx; }

    /** Map a button id to a Rack param (e.g. a BefacoButton param). */
    void assignButton(ButtonId btn, int paramIdx)
    { _btnParam[int(btn)] = paramIdx; }

    /**
     * Map an LED to a Rack light index.
     * The light at lightIdx is expected to be an RGB triplet
     * (lightIdx+0 = R, lightIdx+1 = G, lightIdx+2 = B).
     */
    void assignLight(LightId id, int lightIdx)
    { _lightBase[int(id)] = lightIdx; }

    // -----------------------------------------------------------------------
    // IHardwareIO implementation
    // -----------------------------------------------------------------------

    /**
     * Returns 0–1 normalised knob position.
     * Normalisation: (value - minVal) / (maxVal - minVal).
     * Unregistered pots return 0.5 (safe mid position).
     */
    float readPot(PotId id) override
    {
        int idx = _potParam[int(id)];
        if(idx == kNone)
            return 0.5f;
        float v  = _m->params[idx].getValue();
        float mn = _potMin[int(id)];
        float mx = _potMax[int(id)];
        if(mx == mn)
            return 0.f;
        return (v - mn) / (mx - mn);
    }

    /**
     * Returns the raw port voltage.
     *   VOCT : volts, signed (V/Oct; add directly to a V/Oct accumulator)
     *   GATE : gate voltage (check ≥ 0.5 for high)
     *   Other: raw bipolar voltage
     * Returns 0.0 when unpatched or unregistered.
     */
    float readCV(CVId id) override
    {
        int idx = _cvInput[int(id)];
        if(idx == kNone)
            return 0.f;
        return _m->inputs[idx].getVoltage();
    }

    /** Returns true when a cable is connected to the given input port. */
    bool isPatched(CVId id) override
    {
        int idx = _cvInput[int(id)];
        if(idx == kNone)
            return false;
        return _m->inputs[idx].isConnected();
    }

    /**
     * Returns true when the param value is > 0.5  (momentary button pressed).
     * Returns false for unregistered buttons.
     */
    bool readButton(ButtonId id) override
    {
        int idx = _btnParam[int(id)];
        if(idx == kNone)
            return false;
        return _m->params[idx].getValue() > 0.5f;
    }

    /**
     * Sets an RGB Rack light brightness.
     * Expects three consecutive light indices: base+0=R, base+1=G, base+2=B.
     */
    void writeLight(LightId id, float r, float g, float b) override
    {
        int base = _lightBase[int(id)];
        if(base == kNone)
            return;
        _m->lights[base + 0].setBrightness(r);
        _m->lights[base + 1].setBrightness(g);
        _m->lights[base + 2].setBrightness(b);
    }

  private:
    static constexpr int kNone = -1;

    rack::engine::Module *_m;

    int   _potParam[int(PotId::POT_COUNT)];
    float _potMin[int(PotId::POT_COUNT)];
    float _potMax[int(PotId::POT_COUNT)];

    int _cvInput[int(CVId::CV_COUNT)];
    int _btnParam[int(ButtonId::BUTTON_COUNT)];
    int _lightBase[int(LightId::LIGHT_COUNT)];
};
