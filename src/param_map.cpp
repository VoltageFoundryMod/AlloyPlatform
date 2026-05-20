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
//  CC   Standard meaning (GM/MMA)       Our use
//  ---  -------------------------------- -------
//    1  Modulation Wheel                 motion  (drift + chorus depth)
//    5  Portamento Time                  glidetime  (portamento slide speed 0–2 s)
//    7  Channel Volume                   vol
//    8  Balance                          space   (stereo width 0.0–2.0)
//   65  Portamento On/Off                glideon  (≥64=on / <64=off) — special
//   71  Resonance / Timbre               curve   (envelope shape: pluck → swell)
//   72  Release Time                     adsrrelease  (ADSR release time 0.001–4.0 s)
//   73  Attack Time                      adsrattack   (ADSR attack time 0.001–4.0 s)
//   74  Brightness / VCF Cutoff          filtercutoff (20–16000 Hz, log)
//   75  Sound Controller 6               filterres  (0.0–1.0)
//   76  Sound Controller 7               filtermode  (0-25=OFF…102-127=NOTCH) — special
//   77  Sound Controller 8               filtertype  (0-63=SVF, 64-127=LADDER) — special
//   78  Sound Controller 9               shape   (waveform morph)
//   79  Sound Controller 10              fxfilterpos  (0-63=pre, 64-127=post) — special
//   80  General Purpose                  fxdelaypos   (0-63=pre, 64-127=post) — special
//   81  General Purpose                  envtype  (0-63=AR, 64-127=ADSR) — special
//   82  General Purpose                  adsrdecay    (0.001–4.0 s)
//   83  General Purpose                  adsrsustain  (0.0–1.0)
//   84  General Purpose                  fat  (sub oscillator level 0.0–1.0)
//   85  (undefined)                      delayon  (≥64=on) — special case
//   86  (undefined)                      delaytime  (10–500 ms)
//   87  (undefined)                      delayfeedback  (0.0–0.95)
//   88  (undefined)                      curvetime  (0.25–4.0×)
//   89  (undefined)                      dspeed   (drift glide speed 0.001–0.1)
//   90  (undefined)                      suboct  (0-63=1 oct, 64-127=2 oct) — special
//   91  Effect 1 Depth (Reverb)          revmix  (reverb wet 0.0–1.0)
//   92  Effect 2 Depth (Tremolo)         color   (FM depth / Hz fine spread)
//   93  Effect 3 Depth (Chorus)          chorusmode  (0-31=OFF…96-127=I+II) — special
//   94  Effect 4 Depth (Detune/Celeste)  rel     (RELATION: semitones above ROOT)
//   95  Effect 5 Depth                   delaymix  (0.0–1.0)
//  102  (undefined)                      veloc  (velocity sensitivity ≥64=on) — special
//  110  (undefined)                      midichan  (0=omni, 1–16) — SysEx only
//  112  (undefined)                      revmodspeed  (M40: LFO rate 0.1–4.0)
//  113  (undefined)                      revmoddepth  (M40: LFO depth 0.0–1.0)
//  114  (undefined)                      revfreeze  (≥64=on) — special case
//  115  (undefined)                      voicemode  (6 bands) — special
//  116  (undefined)                      revon  (≥64=on) — special case
//  117  (undefined)                      revsize  (0.0–1.0)
//  118  (undefined)                      revdamping  (0.0–1.0)  ← moved from CC 120
//  119  (undefined)                      dronereturn — special
// ---------------------------------------------------------------------------

// clang-format off
const CCParam kCCParams[] = {
    //  cc   min      max       target                      name           logScale
    {   1,  0.0f,    1.0f,    &gMotion,                   "motion",      false },  // Mod Wheel
    {   5,  0.0f,    2.0f,    &gGlideTime,                "glidetime",   false },  // Portamento Time
    {   7,  0.0f,    1.0f,    &gVolume,                   "vol",         false },  // Channel Volume
    {   8,  0.0f,    2.0f,    &gSpace,                    "space",       false },  // Balance → stereo width
    {  71,  0.0f,    1.0f,    &gCurve,                    "curve",       false },  // Timbre/Resonance
    {  72,  0.001f,  4.0f,    &gAdsrRelease,              "adsrrelease", false },  // Release Time ✓
    {  73,  0.001f,  4.0f,    &gAdsrAttack,               "adsrattack",  false },  // Attack Time ✓
    {  74,  20.0f, 16000.0f,  &gFilterCutoff,             "filtercutoff", true },  // VCF Cutoff ✓
    {  75,  0.0f,    1.0f,    &gFilterRes,                "filterres",   false },  // SC6
    {  78,  0.0f,    1.0f,    &gShape,                    "shape",       false },  // SC9 waveform
    {  82,  0.001f,  4.0f,    &gAdsrDecay,                "adsrdecay",   false },  // General Purpose
    {  83,  0.0f,    1.0f,    &gAdsrSustain,              "adsrsustain", false },  // General Purpose
    {  84,  0.0f,    1.0f,    &gFatness,                  "fat",         false },  // General Purpose
    {  86,  10.0f, 500.0f,    &gDelayTime,                "delaytime",   false },  // Delay time ms
    {  87,  0.0f,   0.95f,    &gDelayFeedback,            "delayfb",     false },  // Delay feedback
    {  88,  0.25f,   4.0f,    &gCurveTime,                "curvetime",   false },  // Envelope time scale
    {  89,  0.001f,  0.1f,    &gDriftSpeed,               "dspeed",      false },  // Drift speed
    {  91,  0.0f,    1.0f,    const_cast<float*>(&gRevMix), "revmix",    false },  // Effect 1 = Reverb ✓
    {  92,  0.0f,    1.0f,    &gColor,                    "color",       false },  // Effect 2 → COLOR
    {  94,  0.0f,   24.0f,    &gRelation,                 "rel",         false },  // Effect 4 = Detune ✓
    {  95,  0.0f,    1.0f,    &gDelayMix,                 "delaymix",    false },  // Effect 5 depth
    { 112,  0.1f,    4.0f,    &gRevModSpeed,              "revmodspeed", false },  // Reverb LFO rate
    { 113,  0.0f,    1.0f,    &gRevModDepth,              "revmoddepth", false },  // Reverb LFO depth
    { 117,  0.0f,    1.0f,    &gRevSize,                  "revsize",     false },  // Reverb size
    { 118,  0.0f,    1.0f,    &gRevDamping,               "revdamping",  false },  // Reverb HF damping
};
// clang-format on

const uint8_t kCCParamCount = sizeof(kCCParams) / sizeof(kCCParams[0]);

bool paramMap_dispatchCC(uint8_t cc, uint8_t value) {
    for (uint8_t i = 0; i < kCCParamCount; i++) {
        if (kCCParams[i].cc == cc) {
            const CCParam &p = kCCParams[i];
            const float t = value / 127.0f;
            if (p.logScale) {
                // Log interpolation: valMin * (valMax/valMin)^t
                *p.target = p.valMin * powf(p.valMax / p.valMin, t);
            } else {
                *p.target = p.valMin + t * (p.valMax - p.valMin);
            }
            return true;
        }
    }
    return false;
}
