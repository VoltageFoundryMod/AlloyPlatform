#include "io/param_map.h"
#include "dsp/CurveEngine.h" // EnvelopeEngine::reset() for the envtype hook
#include "params.h"

// ---------------------------------------------------------------------------
// Side effects a parameter needs beyond storing its value. Referenced by name
// from params.json so the generated table can point at them.
// ---------------------------------------------------------------------------

/// Switching AR <-> ADSR mid-note leaves the old envelope mid-stage; reset so
/// the new one starts from a known state instead of inheriting a level.
void paramHook_envelopeTypeChanged()
{
    if(gCurveEng)
        gCurveEng->reset();
}

/// Turning velocity sensitivity off must restore full level immediately -
/// otherwise a note already sounding stays stuck at its struck velocity.
void paramHook_velocitySensitivityChanged()
{
    if(!gVelocitySensitive)
        gMidiVelocity = 1.0f;
}

#include "param_manifest.generated.h"

// The tables are generated from params.json. Lookup and dispatch over them is
// the platform's - see platform/src/param_map.cpp.
//
// CC assignments follow General MIDI / MMA conventions where a standard meaning
// exists and are repurposed where it does not but the data type matches; the
// rationale for each lives next to its row in params.json.

const ParamDescriptor *const kParamTable = kParamManifest;
const uint8_t                kParamCount = kParamManifestCount;

const EnumParamDescriptor *const kEnumTable = kEnumManifest;
const uint8_t                    kEnumCount = kEnumManifestCount;
