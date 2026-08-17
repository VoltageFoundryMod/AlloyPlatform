#pragma once

#include "io/HardwareIO.h" // PotId/CVId positional slots
#include "io/PanelMap.h"   // Pot::/Cv:: — Audrey's slot names
#include "params.h"

#include <math.h>

// ---------------------------------------------------------------------------
// fillAudreyParams — the single hardware-IO → parameter translation, shared by
// the firmware and the VCV module exactly as AlloyFlux's IOBridge is.
//
// Writes the gXxx goal values directly rather than a params struct: Audrey has
// no smoothing layer of its own to feed, and updateControl() pushes these into
// the engine on the very next line. A struct in between would be ceremony.
//
// Every knob range here must match the corresponding row in params.json — the
// manifest and the panel are two views of the same parameter, and if they
// disagree a MIDI CC and a knob will land on different values.
// ---------------------------------------------------------------------------

static inline float audreyClampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

inline void fillAudreyParams(IHardwareIO &io)
{
    // -----------------------------------------------------------------------
    // Resonator pitch. Knob is 0–1 over MIDI notes 16–72; the V/Oct jack adds
    // on top at the usual 1 V per octave = 12 semitones.
    float note = 16.0f + io.readPot(Pot::PITCH) * (72.0f - 16.0f);
    if(io.isPatched(Cv::VOCT))
        note += io.readCV(Cv::VOCT) * 12.0f;
    gStringPitch = audreyClampf(note, 16.0f, 72.0f);

    // -----------------------------------------------------------------------
    // Feedback loop.  Gain takes the most performative CV on the module —
    // sweeping it toward unity is the instrument.
    float gain = io.readPot(Pot::FBGAIN);
    if(io.isPatched(Cv::FBGAIN))
        gain = audreyClampf(gain + io.readCV(Cv::FBGAIN) * 0.2f, 0.0f, 1.0f);
    gFeedbackGain = -30.0f + gain * (12.0f - -30.0f);
    // Log knobs, matching params.json's `scale: "log"`, so the knob and the
    // CC agree end to end.
    gFeedbackDelay = 0.001f * powf(0.1f / 0.001f, io.readPot(Pot::FBBODY));
    gFeedbackLPF   = 100.0f * powf(18000.0f / 100.0f, io.readPot(Pot::FBLPF));
    gFeedbackHPF   = 10.0f * powf(4000.0f / 10.0f, io.readPot(Pot::FBHPF));

    // -----------------------------------------------------------------------
    // Echo.  Send is a square-law taper (params.json `skew: 2.0`) — squaring
    // the knob here is that same curve.
    float send = io.readPot(Pot::ECHOSEND);
    if(io.isPatched(Cv::ECHOSEND))
        send = audreyClampf(send + io.readCV(Cv::ECHOSEND) * 0.2f, 0.0f, 1.0f);
    gEchoSend = send * send;

    gEchoTime = 0.05f * powf((float)AUDREY_ECHO_MAX_S / 0.05f,
                             io.readPot(Pot::ECHOTIME));

    float echoFb = io.readPot(Pot::ECHOFB);
    if(io.isPatched(Cv::ECHOFB))
        echoFb = audreyClampf(echoFb + io.readCV(Cv::ECHOFB) * 0.2f, 0.0f, 1.0f);
    gEchoFeedback = echoFb * 1.2f;

    // -----------------------------------------------------------------------
    // Reverb.  Decay carries `skew: 0.5`, i.e. a square root.
    gReverbMix = io.readPot(Pot::REVMIX);

    float decay = io.readPot(Pot::REVDECAY);
    if(io.isPatched(Cv::REVDECAY))
        decay = audreyClampf(decay + io.readCV(Cv::REVDECAY) * 0.2f, 0.0f, 1.0f);
    gReverbDecay = 0.2f + sqrtf(decay) * (1.0f - 0.2f);

    // -----------------------------------------------------------------------
    // Output.  Square-law audio taper, `skew: 2.0`.
    const float vol = io.readPot(Pot::VOL);
    gOutputLevel    = vol * vol;

    // -----------------------------------------------------------------------
    // Exciter level, 0–2 with the same square-law taper (`skew: 2.0`). Only
    // the gain lives here; the sample itself is gExciterIn, written per frame
    // in VCV and at the control tick on hardware.
    const float exc = io.readPot(Pot::EXCITE);
    gExciterLevel   = exc * exc * 2.0f;
}
