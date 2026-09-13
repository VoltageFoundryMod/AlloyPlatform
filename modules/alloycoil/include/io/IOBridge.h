#pragma once

#include "io/HardwareIO.h" // PotId/CVId positional slots
#include "io/PanelMap.h"   // Pot::/Cv:: — Alloy Coil's slot names
#include "params.h"

#include <math.h>

// ---------------------------------------------------------------------------
// fillCoilParams — the single hardware-IO → parameter translation, shared by
// the firmware and the VCV module exactly as AlloyFlux's IOBridge is.
//
// Writes into the caller's CoilParams. It used to write the gXxx globals
// directly, on the argument that Alloy Coil has no smoothing layer of its own
// to feed and a struct in between would be ceremony. The smoothing layer
// arrived (ControlSmoother, M63i) and so did the second module in a rack: with
// one shared set of goals, two Alloy Coils wrote each other's parameters, and
// because the control divider and the smoother divider are different periods
// the damage was not even consistent from block to block. Which instance's
// knobs these are is now in the signature.
//
// Every knob range here must match the corresponding row in params.json — the
// manifest and the panel are two views of the same parameter, and if they
// disagree a MIDI CC and a knob will land on different values.
// ---------------------------------------------------------------------------

static inline float alloycoilClampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

/**
 * Panel button levels as of the previous call, owned by whoever is driving the
 * panel — the firmware has one, each VCV module instance has its own.
 *
 * Deliberately not a static inside fillCoilButtons(). This was already true
 * back when the parameters themselves were shared globals — edge state hidden
 * in a header would have meant the second module saw no edges and inherited the
 * first one's warp — and it is the same rule the parameters now follow: what
 * belongs to one panel is passed in, not reached for.
 */
struct CoilButtonState
{
    bool warpPrev = false;
    /**
     * Set by whoever recognised a *different* gesture on this same press — on
     * hardware, the 3 s hold that opens the BLE pairing window — to suppress the
     * warp toggle that the release would otherwise produce. Cleared on that
     * release, so it lasts exactly one press. AlloyFlux's `sModeConsumed` is the
     * same flag for the same reason.
     */
    bool warpConsumed = false;
};

/**
 * The panel switches, on their own.
 *
 * Split out of fillCoilParams() below because the two halves of the panel
 * arrive on different schedules: the buttons are wired on the proto board now,
 * the pot mux and the CV jacks are a later phase. The firmware calls this every
 * control tick and leaves fillCoilParams() alone until there is an ADC to feed
 * it; VCV has the whole panel and calls fillCoilParams(), which calls this. One
 * implementation either way — a second copy of the WARP line living in main.cpp
 * is exactly the drift this file exists to prevent.
 *
 * ---- WARP — the doppler warp -------------------------------------------
 *
 * Upstream Audrey II has a panel toggle wired to exactly this
 * (`kDelaySwitchPin`, scaling echo time by 0.5), and it is the one performance
 * gesture the engine has.
 *
 * The button writes the parameter; the parameter does the halving, in
 * ControlSmoother::Step(). It used to scale echoTime in place, which made warp
 * a thing only a panel could do — and echoTime is what packCoilConfig() saves,
 * so a preset taken with the button down came back half as long. Now the panel
 * and CC 20 are two ways to set one flag.
 *
 * The effect is a side effect of how a delay line works: shortening the time
 * while the buffer is full drags the read head toward the write head, so
 * everything already in the line is re-read faster and pitches up, then settles
 * at the new time. EchoDelay glides delay time over 0.5 s (SetLagTime in
 * Engine::Init), and that glide is the sweep — nothing here has to implement it.
 *
 * ⚠ On the **edges**, not the level, and the difference is the whole reason
 * this needs state. warp has two writers — this button and CC 20 — and a level
 * assignment means the one that runs every control tick wins every control
 * tick: an un-pressed button would hold warp at zero 128 times a second and the
 * CC could never take. Writing only when the button *changes* is the same rule
 * PotTakeover applies to a knob against the web, reduced to two positions: the
 * panel takes the parameter when it is touched and leaves it alone otherwise.
 *
 * So a tap flips the flag and between taps CC 20 owns it and latches, which is
 * what the original toggle did. Touching the button takes it back.
 *
 * ---- SHIFT ---------------------------------------------------------------
 *
 * Nothing to write. SHIFT means "read this knob as its other parameter", so it
 * has no effect of its own and nothing to do until there are knobs — its state
 * is read where it is used: by the ADC driver, when that lands, and by
 * io/CoilLeds.h, which turns the reverb LED white while it is held.
 */
inline void fillCoilButtons(IHardwareIO &io, CoilButtonState &st, CoilParams &p)
{
    const bool warpDown = io.readButton(Btn::WARP);
    if(warpDown != st.warpPrev)
    {
        st.warpPrev = warpDown;
        // Toggle on the RELEASE edge; the press only arms it.
        //
        // It used to be momentary — down meant on, up meant off — which made
        // the panel the odd one out: CC 20, the Alloy Controller's switch and
        // the preset blob all treat warp as a latched flag, so the one place
        // you could not leave it on was the module itself. Toggling makes the
        // four agree.
        //
        // Release-edge, like AlloyFlux's MODE cycle, and for the same reason:
        // this button also carries the 3 s BLE pairing hold (M78b), and a press
        // edge cannot tell a tap from the start of a hold. It fired first and
        // the hold handler then flipped warp back, so pairing audibly shortened
        // the echo for three seconds and restored it. Waiting for the release
        // means the gesture that already happened decides whether the toggle
        // happens at all — the tap costs the length of the tap, which is
        // shorter than the fix it replaces.
        if(!warpDown)
        {
            if(!st.warpConsumed)
                p.warp = p.warp ? 0 : 1;
            st.warpConsumed = false; // one press, one suppression
        }
    }
}

inline void fillCoilParams(IHardwareIO &io, CoilButtonState &btn, CoilParams &p)
{
    // -----------------------------------------------------------------------
    // Resonator pitch. Knob is 0–1 over MIDI notes 16–72; the V/Oct jack adds
    // on top at the usual 1 V per octave = 12 semitones.
    float note = 16.0f + io.readPot(Pot::PITCH) * (72.0f - 16.0f);
    if(io.isPatched(Cv::VOCT))
        note += io.readCV(Cv::VOCT) * 12.0f;
    p.stringPitch = alloycoilClampf(note, 16.0f, 72.0f);

    // -----------------------------------------------------------------------
    // Feedback loop.  Gain takes the most performative CV on the module —
    // sweeping it toward unity is the instrument.
    float gain = io.readPot(Pot::FBGAIN);
    if(io.isPatched(Cv::FBGAIN))
        gain = alloycoilClampf(gain + io.readCV(Cv::FBGAIN) * CvRange::kModToUnit,
                            0.0f,
                            1.0f);
    p.feedbackGain = -30.0f + gain * (12.0f - -30.0f);

    // Body is a square-law taper (params.json `skew: 2.0`), which is upstream's
    // Mapping::EXP. CV is summed *before* the squaring, not after, so a volt
    // moves the knob rather than the value — the same position-space sum the
    // manifest's fromCC() does, and the only way a CC and a CV can agree.
    float body = io.readPot(Pot::FBBODY);
    if(io.isPatched(Cv::FBBODY))
        body = alloycoilClampf(body + io.readCV(Cv::FBBODY) * CvRange::kModToUnit,
                            0.0f,
                            1.0f);
    p.feedbackDelay = 0.001f + body * body * (0.1f - 0.001f);

    // Log knobs, matching params.json's `scale: "log"`, so the knob and the CC
    // agree end to end. These two are frequencies, where a constant ratio per
    // unit of travel is what the ear actually hears.
    p.feedbackLPF = 100.0f * powf(18000.0f / 100.0f, io.readPot(Pot::FBLPF));
    p.feedbackHPF = 10.0f * powf(4000.0f / 10.0f, io.readPot(Pot::FBHPF));

    // -----------------------------------------------------------------------
    // Echo.  Send is a square-law taper (params.json `skew: 2.0`) — squaring
    // the knob here is that same curve.
    float send = io.readPot(Pot::ECHOSEND);
    if(io.isPatched(Cv::ECHOSEND))
        send = alloycoilClampf(send + io.readCV(Cv::ECHOSEND) * CvRange::kModToUnit,
                            0.0f,
                            1.0f);
    p.echoSend = send * send;

    // Square-law too (`skew: 2.0`), matching upstream's Mapping::EXP.
    {
        const float t = io.readPot(Pot::ECHOTIME);
        p.echoTime = 0.05f + t * t * ((float)COIL_ECHO_MAX_S - 0.05f);
    }

    // WARP and SHIFT — see fillCoilButtons() above, which the firmware also
    // calls on its own while it has buttons but no ADC.
    fillCoilButtons(io, btn, p);

    // Echo feedback is knob-only. It had a CV and lost it when the panel spent
    // its four generic jacks elsewhere; MIDI CC 87 still reaches it.
    p.echoFeedback = io.readPot(Pot::ECHOFB) * 1.2f;

    // -----------------------------------------------------------------------
    // Reverb.
    float mix = io.readPot(Pot::REVMIX);
    if(io.isPatched(Cv::REVMIX))
        mix = alloycoilClampf(mix + io.readCV(Cv::REVMIX) * CvRange::kModToUnit,
                           0.0f,
                           1.0f);
    p.reverbMix = mix;

    // Decay carries `skew: 0.5`, i.e. a square root. Knob-only for the same
    // reason as echo feedback above; CC 92 still reaches it.
    p.reverbDecay = 0.2f + sqrtf(io.readPot(Pot::REVDECAY)) * (1.0f - 0.2f);

    // -----------------------------------------------------------------------
    // Output.  Square-law audio taper, `skew: 2.0`.
    const float vol = io.readPot(Pot::VOL);
    p.outputLevel = vol * vol;

    // -----------------------------------------------------------------------
    // Exciter level, 0–2 with the same square-law taper (`skew: 2.0`). Only
    // the gain lives here; the sample itself is p.exciterIn, written per frame
    // in VCV and at the control tick on hardware.
    const float exc = io.readPot(Pot::EXCITE);
    p.exciterLevel = exc * exc * 2.0f;
}
