#pragma once

#include "io/HardwareIO.h"
#include "SynthEngine.h"     // SynthParams
#include "dsp/DelayEngine.h" // DELAY_MAX_MS

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
    float voct = (io.readPot(PotId::ROOT) * 8.0f) - 4.0f;
    // VOCT CV jack: V/Oct offset added directly (already in volts).
    if(io.isPatched(CVId::VOCT))
        voct += io.readCV(CVId::VOCT);
    p.baseFreq = 440.0f * exp2f(voct);

    // -----------------------------------------------------------------------
    // Harmony / voice 2
    // RELATION pot: 0–1 → 0–24 semitones.
    p.relation = io.readPot(PotId::RELATION) * 24.0f;
    if(io.isPatched(CVId::REL_CV))
        p.relation
            = clampf(p.relation + io.readCV(CVId::REL_CV) * 12.0f, 0.0f, 24.0f);

    // -----------------------------------------------------------------------
    // Timbre
    // SHAPE pot: 0–1 direct.
    p.shape = io.readPot(PotId::SHAPE);
    if(io.isPatched(CVId::SHP_CV))
        p.shape = clampf(p.shape + io.readCV(CVId::SHP_CV) * 0.5f, 0.0f, 1.0f);

    // FATNESS (SHIFT+SHAPE on hardware; hidden param / context menu in VCV).
    p.fatness = io.readPot(PotId::FATNESS);

    // COLOR pot: 0–1 direct (FM depth / Hz spread).
    p.color = io.readPot(PotId::COLOR);
    if(io.isPatched(CVId::FM_IN))
        p.color = clampf(p.color + io.readCV(CVId::FM_IN) * 0.5f, 0.0f, 1.0f);

    // -----------------------------------------------------------------------
    // Animation
    // MOTION pot: 0–1 direct.
    p.motion = io.readPot(PotId::MOTION);
    if(io.isPatched(CVId::MTN_CV))
        p.motion
            = clampf(p.motion + io.readCV(CVId::MTN_CV) * 0.5f, 0.0f, 1.0f);

    // DRIFTSPEED (SHIFT+MOTION on hardware; context menu in VCV).
    // Hardware range: 0.001–0.10 coeff; pot maps linearly across that range.
    p.driftSpeed = 0.001f + io.readPot(PotId::DRIFTSPEED) * (0.10f - 0.001f);

    // -----------------------------------------------------------------------
    // Envelope character
    // CURVE pot: 0–1 direct.
    p.curve = io.readPot(PotId::CURVE);

    // CURVETIME (SHIFT+CURVE on hardware; context menu in VCV): 0.25–4.0×.
    p.curveTime = 0.25f + io.readPot(PotId::CURVETIME) * (4.0f - 0.25f);

    // -----------------------------------------------------------------------
    // Spatial / output
    // SPACE pot: 0–1 → 0–2 (stereo width; >1 = hyper-wide).
    p.space = io.readPot(PotId::SPACE) * 2.0f;
    if(io.isPatched(CVId::SPC_CV))
        p.space = clampf(p.space + io.readCV(CVId::SPC_CV) * 1.0f, 0.0f, 2.0f);

    // VOL (SHIFT+SPACE on hardware; context menu in VCV).
    p.volume = io.readPot(PotId::VOL);

    // -----------------------------------------------------------------------
    // Effects sends (M56)
    // DELAY / REVERB pots: 0–1 wet mix, fully CCW = hard bypass (zero CPU).
    // Everything else about the two effects (feedback, damping, modulation)
    // stays on the MIDI/serial side — only mix and the shift-secondary are
    // on the panel.
    p.delayMix = io.readPot(PotId::DELAY);
    // DELAYTIME (SHIFT+DELAY on hardware; context menu in VCV): 10–DELAY_MAX_MS.
    p.delayTime
        = 10.0f + io.readPot(PotId::DELAYTIME) * ((float)DELAY_MAX_MS - 10.0f);

    p.revMix = io.readPot(PotId::REVERB);
    // revEnabled is derived from the mix so a fully-CCW knob costs no CPU.
    p.revEnabled = (p.revMix > 0.001f);
    // REVERBSIZE (SHIFT+REVERB on hardware; context menu in VCV).
    p.revSize = io.readPot(PotId::REVERBSIZE);

    // -----------------------------------------------------------------------
    // Gate / drone mode
    p.gatePatched = io.isPatched(CVId::GATE);
    p.gateHigh    = p.gatePatched && (io.readCV(CVId::GATE) >= 0.5f);
}
