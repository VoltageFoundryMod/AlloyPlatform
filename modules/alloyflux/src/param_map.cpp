#include "io/param_map.h"
#include "dsp/CurveEngine.h" // EnvelopeEngine::reset() for the envtype hook
#include "params.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Side effects a parameter needs beyond storing its value. Referenced by name
// from params.json so the generated table can point at them.
// ---------------------------------------------------------------------------

/// Switching AR ↔ ADSR mid-note leaves the old envelope mid-stage; reset so the
/// new one starts from a known state instead of inheriting a level.
void paramHook_envelopeTypeChanged()
{
    if(gCurveEng)
        gCurveEng->reset();
}

/// Turning velocity sensitivity off must restore full level immediately —
/// otherwise a note already sounding stays stuck at its struck velocity.
void paramHook_velocitySensitivityChanged()
{
    if(!gVelocitySensitive)
        gMidiVelocity = 1.0f;
}

#include "param_manifest.generated.h"

// ---------------------------------------------------------------------------
// The table is generated from params.json — see io/param_map.h. This file only
// provides lookup and dispatch over it.
//
// CC assignments follow General MIDI / MMA conventions where a standard meaning
// exists and are repurposed where it does not but the data type matches; the
// rationale for each lives next to its row in params.json.
// ---------------------------------------------------------------------------

const ParamDescriptor *const kParamTable = kParamManifest;
const uint8_t                kParamCount = kParamManifestCount;

const EnumParamDescriptor *const kEnumTable = kEnumManifest;
const uint8_t                    kEnumCount = kEnumManifestCount;

const ParamDescriptor *paramMap_findByCC(uint8_t cc)
{
    for(uint8_t i = 0; i < kParamManifestCount; i++)
    {
        if(kParamManifest[i].cc == cc)
            return &kParamManifest[i];
    }
    return nullptr;
}

const ParamDescriptor *paramMap_findByName(const char *name)
{
    if(!name)
        return nullptr;
    for(uint8_t i = 0; i < kParamManifestCount; i++)
    {
        if(strcmp(kParamManifest[i].name, name) == 0)
            return &kParamManifest[i];
    }
    return nullptr;
}

const EnumParamDescriptor *paramMap_findEnumByCC(uint8_t cc)
{
    for(uint8_t i = 0; i < kEnumManifestCount; i++)
    {
        if(kEnumManifest[i].cc == cc)
            return &kEnumManifest[i];
    }
    return nullptr;
}

const EnumParamDescriptor *paramMap_findEnumByName(const char *name)
{
    if(!name)
        return nullptr;
    for(uint8_t i = 0; i < kEnumManifestCount; i++)
    {
        if(strcmp(kEnumManifest[i].name, name) == 0)
            return &kEnumManifest[i];
    }
    return nullptr;
}

bool paramMap_dispatchCC(uint8_t cc, uint8_t value)
{
    if(const ParamDescriptor *p = paramMap_findByCC(cc))
    {
        *p->target = p->fromCC(value);
        return true;
    }
    if(const EnumParamDescriptor *e = paramMap_findEnumByCC(cc))
    {
        *e->target = e->fromCC(value);
        if(e->onChange)
            e->onChange();
        return true;
    }
    return false;
}
