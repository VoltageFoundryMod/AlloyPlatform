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
//   75  (undefined)                 filtercutoff  (20–16000 Hz)
//   76  (undefined)                 filterres  (0.0–1.0)
//   77  (undefined)                 filtermode  (0-25=OFF, 26-50=LP, 51-76=HP, 77-101=BP, 102-127=NOTCH) — special
//   78  (undefined)                 filtertype  (0-63=SVF, 64-127=LADDER) — special
//   79  (undefined)                 fxfilterpos  (0-63=pre-chorus, 64-127=post-chorus) — special
//   80  (undefined)                 fxdelaypos   (0-63=pre-reverb, 64-127=post-reverb) — special
//   81  (undefined)                 envtype  (0-63=AR, 64-127=ADSR) — special
//   82  (undefined)                 adsrattack   (0.001–4.0 s)
//   83  (undefined)                 adsrdecay    (0.001–4.0 s)
//   84  (undefined)                 adsrsustain  (0.0–1.0)
//   85  (undefined)                 delayon  (≥64=on) — special case
//   95  (undefined)                 adsrrelease  (0.001–4.0 s)
//   86  (undefined)                 delaytime  (10–500 ms)
//   87  (undefined)                 delayfeedback  (0.0–0.95)
//   88  (undefined)                 delaymix  (0.0–1.0)
//   89  (undefined)                 chorusmode  (0-31=OFF, 32-63=I, 64-95=II, 96-127=I+II) — special
//   90  (undefined)                 suboct  (0-63=1 oct below, 64-127=2 oct below) — special
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
    //  cc   min      max     target           name           logScale
    {   1,  0.0f,    1.0f,  &gMotion,       "motion",      false },  // Mod Wheel
    {   7,  0.0f,    1.0f,  &gVolume,       "vol",         false },  // Channel Volume
    {  71,  0.0f,    1.0f,  &gCurve,        "curve",       false },  // Resonance/Timbre
    {  72,  0.25f,   4.0f,  &gCurveTime,    "curvetime",   false },  // Release Time
    {  73,  0.001f,  0.1f,  &gDriftSpeed,   "dspeed",      false },  // Attack Time
    {  74,  0.0f,    1.0f,  &gShape,        "shape",       false },  // Brightness
    {  75,  20.0f, 16000.0f, &gFilterCutoff, "filtercutoff", true },  // Filter cutoff Hz (log)
    {  76,  0.0f,    1.0f,  &gFilterRes,    "filterres",   false },  // Filter resonance
    {  82,  0.001f,  4.0f,  &gAdsrAttack,   "adsrattack",  false },  // ADSR attack (s)
    {  83,  0.001f,  4.0f,  &gAdsrDecay,    "adsrdecay",   false },  // ADSR decay (s)
    {  84,  0.0f,    1.0f,  &gAdsrSustain,  "adsrsustain", false },  // ADSR sustain (0–1)
    {  86,  10.0f, 500.0f,  &gDelayTime,    "delaytime",   false },  // Delay time ms
    {  87,  0.0f,   0.95f,  &gDelayFeedback,"delayfb",     false },  // Delay feedback
    {  88,  0.0f,    1.0f,  &gDelayMix,     "delaymix",    false },  // Delay wet mix
    {  91,  0.0f,    2.0f,  &gSpace,        "space",       false },  // Reverb Depth
    {  92,  0.0f,  200.0f,  &gDetune,       "detune",      false },  // Tremolo Depth
    {  93,  0.0f,    1.0f,  &gFatness,      "fat",         false },  // Chorus Depth
    {  94,  0.0f,   24.0f,  &gRelation,     "rel",         false },  // Celeste/Variation
    {  95,  0.001f,  4.0f,  &gAdsrRelease,  "adsrrelease", false },  // ADSR release (s)
    { 112,  0.1f,    4.0f,  &gRevModSpeed,              "revmodspeed", false },  // M40: reverb LFO rate
    { 113,  0.0f,    1.0f,  &gRevModDepth,              "revmoddepth", false },  // M40: reverb LFO depth
    { 117,  0.0f,    1.0f,  const_cast<float*>(&gRevMix),     "revmix",      false },  // Reverb wet mix (volatile)
    { 118,  0.0f,    1.0f,  &gRevSize,                  "revsize",     false },  // Reverb size
    { 120,  0.0f,    1.0f,  &gRevDamping,               "revdamping",  false },  // Reverb HF damping
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
