#pragma once

#include "SynthEngine.h"     // SynthParams
#include "dsp/DelayEngine.h" // DELAY_MAX_MS
#include "io/HardwareIO.h"   // PotId/CVId positional slots
#include "io/PanelMap.h"     // Pot::/Cv:: — AlloyFlux's slot names

#include <math.h> // exp2f

// Clamp helper — available as std::clamp in C++17, but we target C++11.
static inline float clampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------------------
// fillSynthParams — shared hardware-IO → SynthParams translation.
//
// Fills all knob/CV-driven fields of `p` from the given IHardwareIO.
// The remaining MIDI- and serial-driven fields (ADSR times, filter, reverb
// damping/modulation, delay feedback, …) are managed separately per-platform:
// gXxx globals on hardware, additional context-menu/param entries in VCV
// Rack (M37h+).
//
// Called by:
//   src/main.cpp                   via HardwarePicoIO
//   vcv-plugin/src/AlloyFlux.cpp   via VCVRackIO
// ---------------------------------------------------------------------------
inline void fillSynthParams(IHardwareIO &io, SynthParams &p)
{
    // -----------------------------------------------------------------------
    // Pitch
    // ROOT pot: 0–1 normalised → ±4 V/Oct range.
    //   0.0 ≈ A0 (27.5 Hz),  0.5 = A4 (440 Hz),  1.0 ≈ A8 (7040 Hz)
    float voct = (io.readPot(Pot::ROOT) * 8.0f) - 4.0f;
    // VOCT CV jack: V/Oct offset added directly (already in volts).
    if(io.isPatched(Cv::VOCT))
        voct += io.readCV(Cv::VOCT);
    p.baseFreq = 440.0f * exp2f(voct);

    // -----------------------------------------------------------------------
    // Harmony / voice 2
    // RELATION pot: 0–1 → 0–24 semitones.
    p.relation = io.readPot(Pot::RELATION) * 24.0f;
    if(io.isPatched(Cv::RELATION))
        p.relation
            = clampf(p.relation + io.readCV(Cv::RELATION) * 12.0f, 0.0f, 24.0f);

    // -----------------------------------------------------------------------
    // Timbre
    // SHAPE pot: 0–1 direct.
    p.shape = io.readPot(Pot::SHAPE);
    if(io.isPatched(Cv::SHAPE))
        p.shape = clampf(p.shape + io.readCV(Cv::SHAPE) * 0.5f, 0.0f, 1.0f);

    // FATNESS (SHIFT+SHAPE on hardware; hidden param / context menu in VCV).
    p.fatness = io.readPot(Pot::FATNESS);

    // COLOR pot: 0–1 direct (FM depth / Hz spread).
    p.color = io.readPot(Pot::COLOR);
    if(io.isPatched(Cv::FM))
        p.color = clampf(p.color + io.readCV(Cv::FM) * 0.5f, 0.0f, 1.0f);

    // -----------------------------------------------------------------------
    // Animation
    // MOTION pot: 0–1 direct.
    p.motion = io.readPot(Pot::MOTION);
    if(io.isPatched(Cv::MOTION))
        p.motion = clampf(p.motion + io.readCV(Cv::MOTION) * 0.5f, 0.0f, 1.0f);

    // DRIFTSPEED (SHIFT+MOTION on hardware; context menu in VCV).
    // Hardware range: 0.001–0.10 coeff; pot maps linearly across that range.
    p.driftSpeed = 0.001f + io.readPot(Pot::DRIFTSPEED) * (0.10f - 0.001f);

    // -----------------------------------------------------------------------
    // Envelope character
    // CURVE pot: 0–1 direct.
    p.curve = io.readPot(Pot::CURVE);

    // CURVETIME (SHIFT+CURVE on hardware; context menu in VCV): 0.25–4.0×.
    p.curveTime = 0.25f + io.readPot(Pot::CURVETIME) * (4.0f - 0.25f);

    // -----------------------------------------------------------------------
    // Spatial / output
    // SPACE pot: 0–1 → 0–2 (stereo width; >1 = hyper-wide).
    p.space = io.readPot(Pot::SPACE) * 2.0f;
    if(io.isPatched(Cv::SPACE))
        p.space = clampf(p.space + io.readCV(Cv::SPACE) * 1.0f, 0.0f, 2.0f);

    // VOL (SHIFT+SPACE on hardware; context menu in VCV).
    p.volume = io.readPot(Pot::VOL);

    // -----------------------------------------------------------------------
    // Effects sends (M56)
    // DELAY / REVERB pots: 0–1 wet send, fully CCW = hard bypass (zero CPU).
    // These add wet on top of a full-gain dry; they are not dry/wet crossfades.
    // Everything else about the two effects (feedback, damping, modulation)
    // stays on the MIDI/serial side — only mix and the shift-secondary are
    // on the panel.
    p.delayMix = io.readPot(Pot::DELAY);
    // DELAYTIME (SHIFT+DELAY on hardware; context menu in VCV): 10–DELAY_MAX_MS.
    p.delayTime
        = 10.0f + io.readPot(Pot::DELAYTIME) * ((float)DELAY_MAX_MS - 10.0f);

    p.revMix = io.readPot(Pot::REVERB);
    // revEnabled is derived from the mix so a fully-CCW knob costs no CPU.
    p.revEnabled = (p.revMix > 0.001f);
    // REVERBSIZE (SHIFT+REVERB on hardware; context menu in VCV).
    p.revSize = io.readPot(Pot::REVERBSIZE);

    // -----------------------------------------------------------------------
    // Gate / drone mode
    p.gatePatched = io.isPatched(Cv::GATE);
    p.gateHigh    = p.gatePatched && (io.readCV(Cv::GATE) >= 0.5f);
}
