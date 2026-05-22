#pragma once

#include "HardwareIO.h"
#include "SynthEngine.h" // SynthParams

#include <math.h> // exp2f

// ---------------------------------------------------------------------------
// fillSynthParams — shared hardware-IO → SynthParams translation.
//
// Fills the knob/CV-driven fields of `p` from the given IHardwareIO.
// MIDI- and serial-driven fields (filter params, reverb, ADSR, etc.) are
// managed separately by each platform (gXxx globals on hardware; additional
// VCV Rack params added in M37e+).
//
// Called by:
//   src/main.cpp          via HardwarePicoIO
//   vcv-plugin/src/AlloyFlux.cpp   via VCVRackIO
// ---------------------------------------------------------------------------
inline void fillSynthParams(IHardwareIO &io, SynthParams &p) {
    // --- Pitch ---
    // ROOT pot: 0–1 normalised → ±4 V/Oct range.
    //   0.0 ≈ A0 (27.5 Hz),  0.5 = A4 (440 Hz),  1.0 ≈ A8 (7040 Hz)
    float voct = (io.readPot(PotId::ROOT) * 8.0f) - 4.0f;

    // VOCT CV jack: V/Oct offset added directly (already in volts).
    if (io.isPatched(CVId::VOCT))
        voct += io.readCV(CVId::VOCT);

    p.baseFreq = 440.0f * exp2f(voct);

    // --- Gate / drone mode ---
    p.gatePatched = io.isPatched(CVId::GATE);
    p.gateHigh = p.gatePatched && (io.readCV(CVId::GATE) >= 0.5f);
}
