#include "param_map.h"
#include "params.h"

// ---------------------------------------------------------------------------
// CC Parameter Table
//
// One row per continuous parameter.  All transports iterate this table —
// no CC numbers or ranges live anywhere else in the codebase.
//
// CC assignments follow General MIDI / MMA conventions where a standard
// meaning exists; repurposed where it does not but the data-type matches.
//
//  CC   Standard meaning (GM/MMA)   Our use
//  ---  --------------------------  -------
//    1  Modulation Wheel            motion  (drift + chorus depth)
//    7  Channel Volume              vol
//   71  Resonance / Timbre          curve   (envelope shape: pluck → swell)
//   72  Release Time                curvetime
//   73  Attack Time                 dspeed  (drift glide speed)
//   74  Brightness / Filter cutoff  shape   (waveform morph)
//   91  Reverb Send Depth           space   (stereo width)
//   92  Tremolo Send Depth          detune  (symmetric fine-spread in Hz)
//   93  Chorus Send Depth           fat     (sub oscillator level)
//   94  Celeste (Detune) Depth      rel     (RELATION: semitones above ROOT)
// ---------------------------------------------------------------------------

// clang-format off
const CCParam kCCParams[] = {
    //  cc   min      max     target         name
    {   1,  0.0f,    1.0f,  &gMotion,     "motion"    },  // Mod Wheel
    {   7,  0.0f,    1.0f,  &gVolume,     "vol"       },  // Channel Volume
    {  71,  0.0f,    1.0f,  &gCurve,      "curve"     },  // Resonance/Timbre
    {  72,  0.25f,   4.0f,  &gCurveTime,  "curvetime" },  // Release Time
    {  73,  0.001f,  0.1f,  &gDriftSpeed, "dspeed"    },  // Attack Time
    {  74,  0.0f,    1.0f,  &gShape,      "shape"     },  // Brightness
    {  91,  0.0f,    2.0f,  &gSpace,      "space"     },  // Reverb Depth
    {  92,  0.0f,  200.0f,  &gDetune,     "detune"    },  // Tremolo Depth
    {  93,  0.0f,    1.0f,  &gFatness,    "fat"       },  // Chorus Depth
    {  94,  0.0f,   24.0f,  &gRelation,   "rel"       },  // Celeste/Variation
};
// clang-format on

const uint8_t kCCParamCount = sizeof(kCCParams) / sizeof(kCCParams[0]);

bool paramMap_dispatchCC(uint8_t cc, uint8_t value) {
    for (uint8_t i = 0; i < kCCParamCount; i++) {
        if (kCCParams[i].cc == cc) {
            const CCParam &p = kCCParams[i];
            *p.target = p.valMin + (value / 127.0f) * (p.valMax - p.valMin);
            return true;
        }
    }
    return false;
}
