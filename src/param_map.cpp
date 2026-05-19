#include "io/param_map.h"
#include "dsp/ReverbEngine.h"
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
//   85  (undefined)                 delayon  (≥64=on) — special case
//   86  (undefined)                 delaytime  (10–500 ms)
//   87  (undefined)                 delayfeedback  (0.0–0.95)
//   88  (undefined)                 delaymix  (0.0–1.0)
//   89  (undefined)                 chorusmode  (0-31=OFF, 32-63=I, 64-95=II, 96-127=I+II) — special
//   91  Reverb Send Depth           space   (stereo width)
//   92  Tremolo Send Depth          detune  (symmetric fine-spread in Hz)
//   93  Chorus Send Depth           fat     (sub oscillator level)
//   94  Celeste (Detune) Depth      rel     (RELATION: semitones above ROOT)
//  112  (undefined)                 revmodspeed  (M40: LFO rate 0.1–4.0)
//  113  (undefined)                 revmoddepth  (M40: LFO depth 0.0–1.0)
//  116  (undefined)                 revon  (≥64=on) — special case
//  117  (undefined)                 revmix  (0.0–1.0)
//  118  (undefined)                 revsize  (0.0–1.0)
//  120  (undefined)                 revdamping  (0.0–1.0)
// ---------------------------------------------------------------------------

// clang-format off
const CCParam kCCParams[] = {
    //  cc   min      max     target           name
    {   1,  0.0f,    1.0f,  &gMotion,       "motion"      },  // Mod Wheel
    {   7,  0.0f,    1.0f,  &gVolume,       "vol"         },  // Channel Volume
    {  71,  0.0f,    1.0f,  &gCurve,        "curve"       },  // Resonance/Timbre
    {  72,  0.25f,   4.0f,  &gCurveTime,    "curvetime"   },  // Release Time
    {  73,  0.001f,  0.1f,  &gDriftSpeed,   "dspeed"      },  // Attack Time
    {  74,  0.0f,    1.0f,  &gShape,        "shape"       },  // Brightness
    {  86,  10.0f, 500.0f,  &gDelayTime,    "delaytime"   },  // Delay time ms
    {  87,  0.0f,   0.95f,  &gDelayFeedback,"delayfb"     },  // Delay feedback
    {  88,  0.0f,    1.0f,  &gDelayMix,     "delaymix"    },  // Delay wet mix
    {  91,  0.0f,    2.0f,  &gSpace,        "space"       },  // Reverb Depth
    {  92,  0.0f,  200.0f,  &gDetune,       "detune"      },  // Tremolo Depth
    {  93,  0.0f,    1.0f,  &gFatness,      "fat"         },  // Chorus Depth
    {  94,  0.0f,   24.0f,  &gRelation,     "rel"         },  // Celeste/Variation
    { 112,  0.1f,    4.0f,  &gRevModSpeed,              "revmodspeed" },  // M40: reverb LFO rate
    { 113,  0.0f,    1.0f,  &gRevModDepth,              "revmoddepth" },  // M40: reverb LFO depth
    { 117,  0.0f,    1.0f,  const_cast<float*>(&gRevMix),     "revmix"      },  // Reverb wet mix (volatile)
    { 118,  0.0f,    1.0f,  &gRevSize,                  "revsize"     },  // Reverb size
    { 120,  0.0f,    1.0f,  &gRevDamping,               "revdamping"  },  // Reverb HF damping
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
