#pragma once

#include "VoiceMode.h"        // VoiceMode enum
#include "dsp/ChorusEngine.h" // ChorusMode enum
#include "dsp/CurveEngine.h"  // EnvelopeType enum (M5x)
#include "dsp/FilterEngine.h" // FilterMode, FilterType enums (M26a / M5x)

/**
 * Shared synthesis parameters — defined in main.cpp.
 *
 * Naming convention:
 *   gXxx  goal values: written by serial console / knobs / MIDI in updateControl()
 *   sXxx  smoothed values: read by updateAudio(), one-pole LPF applied in updateControl()
 */

// Voice pitch & detune
extern float gBaseFreq; // Hz, voice 1 root pitch (20–8000)
extern float gDetune;   // Hz, symmetric fine spread (v1 = base−d/2, v2 = base+d/2)

// Voice mode and RELATION
extern VoiceMode gVoiceMode; // synthesis personality (default: PAIR)
extern float gRelation;      // semitones above ROOT for voice 2: 0=unison, 7=fifth, 12=octave, 24=max (PAIR mode)

// Timbre
extern float gShape;       // 0.0 = sine  0.25 = tri  0.50 = saw  0.75 = pulse  1.0 = hollow
extern float gFatness;     // 0.0 = no sub osc  …  1.0 = sub at 50% of main level
extern uint8_t gSubOctave; // 1 = one octave below (÷2), 2 = two octaves below (÷4)

// Animation
extern float gMotion;     // 0.0 = static  …  1.0 = full drift + chorus depth
extern float gDriftSpeed; // one-pole glide coeff: 0.001 (slow) … 0.10 (fast), default 0.025

// Chorus
extern ChorusMode gChorusMode; // OFF / I / II / I_II  (default: I_II)

// Space
extern float gSpace; // stereo width: 0.0 = mono, 1.0 = identity, 2.0 = hyper-wide (default: 1.0)

// Envelope / VCA (Milestone 15)
extern float gCurve;               // 0.0 = pluck … 1.0 = swell
extern float gCurveTime;           // envelope time scale: 0.25=4×faster  1.0=default  4.0=4×slower
extern volatile bool gGateHigh;    // true while gate is asserted (attack phase)
extern volatile bool gGatePatched; // false = drone (bypass VCA); true = AR envelope active
// Active envelope engine pointer (concrete type: AREnvelope or ADSREnvelope).
// Arming setGate() on this from any gate source ensures the ISR never sees
// gGatePatched=true while the envelope is still in IDLE (which caused a click).
extern EnvelopeEngine *gCurveEng;

// Level
extern float gVolume;       // 0.0 – 1.0 master output
extern float gMidiVelocity; // 0.0 – 1.0 per-note MIDI velocity scale (1.0 = full, reset on drone return)
                            // Always 1.0 for CV / drone / button gate sources.

// MIDI configuration
extern uint8_t gMidiChannel; // 0 = omni (all channels), 1–16 = specific channel

// CPU profiling — defined in main.cpp, only present when CPU_PROFILE is set.
// gAudioElapsedUs : µs spent inside the last updateAudio() call
// gAudioOverruns  : calls that exceeded the 30µs audio budget
#ifdef CPU_PROFILE
extern volatile bool gPerformancePrintEnabled; // set by cmd_performance_print, read by updateAudio()
extern volatile uint32_t gAudioElapsedUs;
extern volatile uint32_t gAudioOverruns;
#endif

// ---------------------------------------------------------------------------
// M26 Post-effects section
// ---------------------------------------------------------------------------

// Filter (M26a / M5x)
extern float gFilterCutoff;    // Hz, 20–16000, default 8000 (i.e. OFF-but-ready)
extern float gFilterRes;       // 0.0 (flat) – 1.0 (near self-oscillation), default 0.0
extern FilterMode gFilterMode; // OFF by default — zero CPU cost when bypassed
extern FilterType gFilterType; // SVF (default) or LADDER — runtime-selectable (M5x)

// Abstract filter instance pointer — dereferences to SVFFilter or OTALadder.
// Defined in main.cpp.  commands.cpp uses this to avoid including the concrete headers.
class FilterEngine;
extern FilterEngine *gFilterInst;

// Envelope / VCA type (M5x — runtime-selectable)
extern EnvelopeType gEnvelopeType; // AR (default) or ADSR

// Abstract envelope instance pointer — dereferences to AREnvelope or ADSREnvelope.
class EnvelopeEngine;
extern EnvelopeEngine *gCurveEng;

// ADSR envelope parameters (used when gEnvelopeType == ADSR)
extern float gAdsrAttack;  // seconds, 0.001–10.0
extern float gAdsrDecay;   // seconds, 0.001–10.0
extern float gAdsrSustain; // 0.0–1.0
extern float gAdsrRelease; // seconds, 0.001–10.0
extern bool gAdsrLoop;     // loop mode: envelope restarts automatically after release

// Reverb (M26b — active when gRevEnabled; NullReverb stub until DattorroReverb)
extern float gRevSize;     // 0.0 (small room) – 1.0 (long plate), default 0.5
extern float gRevDamping;  // 0.0 (bright) – 1.0 (dark HF loss), default 0.5
extern float gRevModSpeed; // 0.1 (glacial) – 4.0 (fast shimmer), default 1.0 (M40)
extern float gRevModDepth; // 0.0 (static) – 1.0 (full ±8 sample swing), default 1.0 (M40)
extern bool gRevFrozen;    // false = normal; true = infinite sustain (M41)
// gRevMix / gRevEnabled are volatile — declared in ReverbEngine.h

// Delay (M26c — pass-through stub; parameters stored ready for implementation)
extern float gDelayTime;     // ms, 10–DELAY_MAX_MS, default 100
extern float gDelayFeedback; // 0.0 (single echo) – 0.95 (long decay), default 0.5
extern float gDelayMix;      // 0.0 (off) – 1.0 (full wet), default 0.0
