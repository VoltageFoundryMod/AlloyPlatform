#pragma once

#include "io/HardwareIO.h" // PotId/CVId positional slots
#include "io/PanelMap.h"   // Pot::/Cv:: — Alloy Coil's slot names
#include "params.h"

#include <math.h>

// ---------------------------------------------------------------------------
// fillCoilParams — the single hardware-IO → parameter translation, shared by
// the firmware and the VCV module exactly as AlloyFlux's IOBridge is.
//
// Writes the gXxx goal values directly rather than a params struct: Alloy Coil has
// no smoothing layer of its own to feed, and updateControl() pushes these into
// the engine on the very next line. A struct in between would be ceremony.
//
// Every knob range here must match the corresponding row in params.json — the
// manifest and the panel are two views of the same parameter, and if they
// disagree a MIDI CC and a knob will land on different values.
// ---------------------------------------------------------------------------

static inline float alloycoilClampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

inline void fillCoilParams(IHardwareIO &io)
{
    // -----------------------------------------------------------------------
    // Resonator pitch. Knob is 0–1 over MIDI notes 16–72; the V/Oct jack adds
    // on top at the usual 1 V per octave = 12 semitones.
    float note = 16.0f + io.readPot(Pot::PITCH) * (72.0f - 16.0f);
    if(io.isPatched(Cv::VOCT))
        note += io.readCV(Cv::VOCT) * 12.0f;
    gStringPitch = alloycoilClampf(note, 16.0f, 72.0f);

    // -----------------------------------------------------------------------
    // Feedback loop.  Gain takes the most performative CV on the module —
    // sweeping it toward unity is the instrument.
    float gain = io.readPot(Pot::FBGAIN);
    if(io.isPatched(Cv::FBGAIN))
        gain = alloycoilClampf(gain + io.readCV(Cv::FBGAIN) * CvRange::kModToUnit,
                            0.0f,
                            1.0f);
    gFeedbackGain = -30.0f + gain * (12.0f - -30.0f);

    // Log knobs, matching params.json's `scale: "log"`, so the knob and the
    // CC agree end to end. Body's CV is summed *before* the exponent, not
    // after: 1 V then means a constant ratio of delay time wherever the knob
    // is, instead of a constant number of milliseconds that would be the whole
    // range at one end and inaudible at the other.
    float body = io.readPot(Pot::FBBODY);
    if(io.isPatched(Cv::FBBODY))
        body = alloycoilClampf(body + io.readCV(Cv::FBBODY) * CvRange::kModToUnit,
                            0.0f,
                            1.0f);
    gFeedbackDelay = 0.001f * powf(0.1f / 0.001f, body);

    gFeedbackLPF = 100.0f * powf(18000.0f / 100.0f, io.readPot(Pot::FBLPF));
    gFeedbackHPF = 10.0f * powf(4000.0f / 10.0f, io.readPot(Pot::FBHPF));

    // -----------------------------------------------------------------------
    // Echo.  Send is a square-law taper (params.json `skew: 2.0`) — squaring
    // the knob here is that same curve.
    float send = io.readPot(Pot::ECHOSEND);
    if(io.isPatched(Cv::ECHOSEND))
        send = alloycoilClampf(send + io.readCV(Cv::ECHOSEND) * CvRange::kModToUnit,
                            0.0f,
                            1.0f);
    gEchoSend = send * send;

    gEchoTime = 0.05f * powf((float)COIL_ECHO_MAX_S / 0.05f,
                             io.readPot(Pot::ECHOTIME));

    // WARP — doppler warp. Upstream Audrey II has a panel toggle wired to
    // exactly this (`kDelaySwitchPin`, scaling echo time by 0.5), and it is the
    // one performance gesture the engine has.
    //
    // The effect is a side effect of how a delay line works: shortening the
    // time while the buffer is full drags the read head toward the write head,
    // so everything already in the line is re-read faster and pitches up, then
    // settles at the new time. EchoDelay glides delay time over 0.5 s
    // (SetLagTime in Engine::Init), and that glide is the sweep — nothing here
    // has to implement it.
    //
    // Momentary rather than latching, which is a deliberate departure: upstream
    // has a physical toggle and two stable settings, we have a button. Holding
    // gives a dive on press and a rise on release, which is playable in a way a
    // latch is not. Latching instead is a one-line change here.
    if(io.readButton(Btn::WARP))
        gEchoTime *= 0.5f;

    // Echo feedback is knob-only. It had a CV and lost it when the panel spent
    // its four generic jacks elsewhere; MIDI CC 87 still reaches it.
    gEchoFeedback = io.readPot(Pot::ECHOFB) * 1.2f;

    // -----------------------------------------------------------------------
    // Reverb.
    float mix = io.readPot(Pot::REVMIX);
    if(io.isPatched(Cv::REVMIX))
        mix = alloycoilClampf(mix + io.readCV(Cv::REVMIX) * CvRange::kModToUnit,
                           0.0f,
                           1.0f);
    gReverbMix = mix;

    // Decay carries `skew: 0.5`, i.e. a square root. Knob-only for the same
    // reason as echo feedback above; CC 92 still reaches it.
    gReverbDecay = 0.2f + sqrtf(io.readPot(Pot::REVDECAY)) * (1.0f - 0.2f);

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
