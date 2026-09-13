#pragma once
#ifndef INFS_COIL_CONTROL_SMOOTHER_H
#define INFS_COIL_CONTROL_SMOOTHER_H

#include "CoilHot.h"
#include "DSPUtils.h" // onepole_coef_t60
#include "FeedbackSynthEngine.h"
#include "dsp.h" // fonepole
#include "params.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// Control smoothing — the goal values reach the engine through a one-pole per
// parameter, not as steps.
//
// Upstream runs every parameter through SmoothedValue via ParameterRegistry,
// with a per-parameter t60 chosen in registerParams(). Those two files are
// Daisy-specific (std::function, std::unordered_map, daisy_seed.h) and were
// deliberately left behind when the engine was vendored — but the smoothing
// coefficients went out with them and were not replaced, so every parameter
// except the two the engine smooths internally arrived at the engine as a
// step. On a Karplus-Strong string whose delay length *is* the pitch, and a
// pair of biquads whose coefficients are re-solved on every write, that is
// audible as zipper on any move.
//
// The t60 values below are upstream's, verbatim. Two are already handled
// inside the engine and are deliberately smoothed here as well, because
// upstream smooths them at both stages too:
//
//   feedback delay  1.0 s here, then the engine's own 0.2 s tau
//   echo time       0.1 s here, then EchoDelay's 0.5 s lag
//
// ---------------------------------------------------------------------------
// Why this is stepped at the audio block rate and not the control tick
//
// A one-pole cannot glide faster than its update rate, and upstream's update
// rate was 48000/4 = 12 kHz — it re-ran the whole registry every 4-frame audio
// block. This module's control tick is 128 Hz, where a 7.8 ms period is longer
// than the 7.2 ms tau that a 50 ms t60 asks for: onepole_coef() clamps to 1.0
// and the smoother degenerates into exactly the step it exists to remove. Nine
// of the twelve parameters use that 50 ms default, so stepping this at the
// control tick would have been a no-op on almost all of it.
//
// So the goal values are still read at 128 Hz — that is an I/O rate, set by
// how often a knob or a CC needs looking at — and the interpolation runs at the
// audio block rate, 48000/32 = 1500 Hz. That is 75 steps across a 50 ms glide.
// It is 8x coarser than upstream's 12 kHz and inaudible for every parameter
// here, because each one is converging exponentially: the step size shrinks as
// it approaches the goal.
//
// ---------------------------------------------------------------------------
// Why the deadband and the throttle are not optional
//
// Raising the push rate from 128 Hz to 1500 Hz is a 12x increase in setter
// calls, and four of the twelve setters end in a transcendental. On the RP2350
// those cost about 90 us apiece — an order of magnitude more than a host
// benchmark predicts, because x86 has them in hardware and the M33 does not.
//
// Measured on the board, same patch, 32-frame block against a 666 us budget:
//
//   all twelve setters, every step      942 us   -41 % headroom, DMA underruns
//   deadband, settled patch (0 setters) 578 us   +13 % headroom, silent
//
// So the smoother is only affordable because it almost never actually pushes.
// The deadband handles the settled case; the one-costly-setter-per-step
// throttle in Step() handles the other end, where several knobs move together
// and 13 % of headroom cannot absorb four 90 us calls in one block.
// ---------------------------------------------------------------------------

namespace infrasonic {
namespace FeedbackSynth {

class ControlSmoother {

    public:

        /**
         * @param step_rate_hz  how often Step() will be called, in Hz. Not the
         *        sample rate and not the control-tick rate — see above.
         */
        void Init(const float step_rate_hz)
        {
            // Upstream's registerParams() smoothing times. The 0.05 f entries
            // are ParameterRegistry::Register()'s default, which upstream took
            // for everything it did not name explicitly.
            coef_[kPitch]    = onepole_coef_t60(0.20f, step_rate_hz);
            coef_[kFbGain]   = onepole_coef_t60(0.05f, step_rate_hz);
            coef_[kFbBody]   = onepole_coef_t60(1.00f, step_rate_hz);
            coef_[kFbLpf]    = onepole_coef_t60(0.05f, step_rate_hz);
            coef_[kFbHpf]    = onepole_coef_t60(0.05f, step_rate_hz);
            coef_[kRevMix]   = onepole_coef_t60(0.05f, step_rate_hz);
            coef_[kRevDecay] = onepole_coef_t60(0.05f, step_rate_hz);
            coef_[kEchoSend] = onepole_coef_t60(0.05f, step_rate_hz);
            coef_[kEchoTime] = onepole_coef_t60(0.10f, step_rate_hz);
            coef_[kEchoFb]   = onepole_coef_t60(0.05f, step_rate_hz);
            coef_[kVol]      = onepole_coef_t60(0.05f, step_rate_hz);
            // No upstream equivalent — the exciter gain is this port's own
            // control. Given the default, which is what an unnamed parameter
            // got upstream.
            coef_[kExcite] = onepole_coef_t60(0.05f, step_rate_hz);

            // Deadbands — how far a smoothed value must move from the last one
            // handed to the engine before the setter is called again.
            //
            // Without these the smoother pays for all twelve setters on every
            // step forever, even on a patch nobody is touching: a one-pole
            // approaches its goal asymptotically and never arrives, so the
            // value keeps changing in the tenth decimal place and every setter
            // keeps firing. Four of them are not cheap — SetFeedbackLPFCutoff
            // and SetFeedbackHPFCutoff each re-solve a biquad through tanf(),
            // SetStringPitch calls mtof() -> powf(), SetFeedbackGain calls
            // pow10f() -> expf() — so that is four transcendentals per step,
            // 1500 times a second, to arrive at the numbers the engine already
            // had.
            //
            // With a deadband the glide converges, goes quiet, and costs twelve
            // compares. Every value below is well under its parameter's
            // just-noticeable difference, and because convergence is geometric
            // any non-zero threshold is reached in a bounded number of steps —
            // ~90 ms for the widest range here.
            eps_[kPitch]    = 0.002f;   // semitones; JND is ~0.05
            eps_[kFbGain]   = 0.01f;    // dB
            eps_[kFbBody]   = 2.0e-6f;  // s — 0.1 sample at 48 kHz
            eps_[kFbLpf]    = 0.05f;    // Hz — 0.05 % at the 100 Hz end
            eps_[kFbHpf]    = 0.02f;    // Hz — same, on a 10 Hz floor
            eps_[kRevMix]   = 1.0e-4f;  // 0..1
            eps_[kRevDecay] = 1.0e-4f;  // tank feedback coefficient
            eps_[kEchoSend] = 1.0e-4f;  // 0..1
            eps_[kEchoTime] = 2.0e-5f;  // s — EchoDelay lags this by 0.5 s anyway
            eps_[kEchoFb]   = 1.0e-4f;  // 0..1.2
            eps_[kVol]      = 1.0e-4f;  // 0..1
            eps_[kExcite]   = 1.0e-4f;  // 0..2

            Snap();
        }

        /// Setters called on the most recent Step(). 0 on a settled patch, up
        /// to kCount while something is moving — the number that says whether
        /// the deadband above is doing its job. Diagnostic only.
        uint8_t LastPushCount() const { return push_count_; }

        /**
         * Take the goal values as they stand, with no glide, on the next Step().
         *
         * This is upstream's `immediate` flag on ParameterRegistry::Update().
         * Wanted wherever a value changes for a reason other than someone
         * moving a control: boot, preset recall, factory reset, MIDI panic.
         * Gliding a whole preset in over a second — which is what the body's
         * 1.0 s t60 would do — is not a crossfade, it is a smear.
         */
        void Snap() { primed_ = false; }

        /**
         * One interpolation step, then push the result into the engine.
         *
         * @param p the goal values to glide toward — the instance's own. The
         *        smoother holds no pointer to them between calls, so a host
         *        with several modules can hand each Step() a different set and
         *        nothing leaks across; the interpolator state in value_/pushed_
         *        belongs to the smoother, which is already per-instance.
         */
        COIL_HOT(smoother_step) inline void Step(Engine &engine, const CoilParams &p)
        {
            const float goal[kCount] = {
                readGoal(p.stringPitch),
                readGoal(p.feedbackGain),
                readGoal(p.feedbackDelay),
                readGoal(p.feedbackLPF),
                readGoal(p.feedbackHPF),
                readGoal(p.reverbMix),
                readGoal(p.reverbDecay),
                readGoal(p.echoSend),
                // The one goal that is not read straight through. Warp halves
                // the echo time here rather than at any of the places that
                // *write* echoTime, because those are three (knob, CC, preset)
                // and this is one — and because the stored time should stay the
                // time that was asked for. See CoilParams::warp in params.h.
                //
                // The sweep is not implemented anywhere: this goal steps, the
                // one-pole below glides onto it over its 0.10 s t60, and
                // EchoDelay lags that by another 0.5 s. Dropping the time while
                // the line is full drags the read head toward the write head,
                // and everything already in the buffer comes back faster.
                readGoal(p.echoTime) * (readFlag(p.warp) ? 0.5f : 1.0f),
                readGoal(p.echoFeedback),
                readGoal(p.outputLevel),
                readGoal(p.exciterLevel),
            };

            // `force` on the first step after Init() or Snap(): land on the
            // goals and push every one of them, because the engine may be
            // holding something entirely different (boot, preset recall,
            // panic).
            const bool force = !primed_;
            if(force)
            {
                primed_ = true;
                for(int i = 0; i < kCount; i++)
                    value_[i] = goal[i];
            }
            else
            {
                for(int i = 0; i < kCount; i++)
                    daisysp::fonepole(value_[i], goal[i], coef_[i]);
            }

            // Compared against the last value *pushed*, never against the goal:
            // against the goal this would stall part-way through a glide and
            // leave the engine holding a stale value forever.
            //
            // Costly parameters are additionally rate-limited to one per step.
            // Measured on hardware: the four that call a transcendental cost
            // ~90 us each on the RP2350, against a 666 us block budget with
            // ~88 us of headroom — so *one* of them fits in a step and two do
            // not. Letting all four move at once would put the block over
            // budget exactly when someone is playing the module.
            //
            // The glide itself is unaffected: value_ still advances every step,
            // only delivery is throttled, so a costly parameter reaches the
            // engine at 375 Hz instead of 1500 Hz while three others are also
            // moving. That is still three times the old control tick, and it
            // only degrades in the rare case of several sweeping together.
            uint8_t pushes = 0;

            // Pass 1 — the cheap parameters, all of them, in order.
            for(int i = 0; i < kCount; i++)
            {
                if(costly(i) || !needsPush(i, force))
                    continue;
                pushed_[i] = value_[i];
                pushes++;
                apply(engine, i);
            }

            // Pass 2 — at most one costly parameter, scanned from a rotating
            // cursor so that several sweeping at once take turns rather than
            // the lowest index starving the rest. `force` overrides the limit:
            // a preset recall has to land completely, and one over-budget block
            // at that moment is not worth a half-applied patch.
            for(int n = 0; n < kCount; n++)
            {
                const int i = (costly_cursor_ + n) % kCount;
                if(!costly(i) || !needsPush(i, force))
                    continue;
                pushed_[i] = value_[i];
                pushes++;
                apply(engine, i);
                if(!force)
                {
                    costly_cursor_ = (uint8_t)((i + 1) % kCount);
                    break;
                }
            }

            push_count_ = pushes;
        }

    private:

        /// True when a parameter has drifted far enough from what the engine
        /// last received to be worth another setter call.
        inline bool needsPush(const int i, const bool force) const
        {
            if(force)
                return true;
            const float d = value_[i] - pushed_[i];
            return (d < 0.0f ? -d : d) > eps_[i];
        }

        COIL_HOT(smoother_apply) inline void apply(Engine &engine, const int i)
        {
            switch(i)
            {
                case kPitch:    engine.SetStringPitch(value_[i]);         break;
                case kFbGain:   engine.SetFeedbackGain(value_[i]);        break;
                case kFbBody:   engine.SetFeedbackDelay(value_[i]);       break;
                case kFbLpf:    engine.SetFeedbackLPFCutoff(value_[i]);   break;
                case kFbHpf:    engine.SetFeedbackHPFCutoff(value_[i]);   break;
                case kRevMix:   engine.SetReverbMix(value_[i]);           break;
                case kRevDecay: engine.SetReverbFeedback(value_[i]);      break;
                case kEchoSend: engine.SetEchoDelaySendAmount(value_[i]); break;
                case kEchoTime: engine.SetEchoDelayTime(value_[i]);       break;
                case kEchoFb:   engine.SetEchoDelayFeedback(value_[i]);   break;
                case kVol:      engine.SetOutputLevel(value_[i]);         break;
                case kExcite:   engine.SetExciterLevel(value_[i]);        break;
                default: break;
            }
        }

        /**
         * Volatile load of a goal value.
         *
         * On hardware Step() runs on the audio core and the goal values are
         * written by the control core, so each read must actually happen rather
         * than being folded away — the same requirement CoilParams::exciterIn
         * carries, and for the same reason. They cannot simply be declared
         * volatile: the platform's ParamDescriptor holds them as plain
         * `float *`, so the qualifier is applied at the read instead. A single
         * aligned float is atomic on the M33, so there is nothing to tear.
         */
        static inline float readGoal(const float &g)
        {
            return *static_cast<const volatile float *>(&g);
        }

        /// The same volatile load for a discrete parameter's uint8_t target —
        /// warp is written by the control core and read here on the audio one.
        static inline bool readFlag(const uint8_t &g)
        {
            return *static_cast<const volatile uint8_t *>(&g) != 0;
        }

        enum Index {
            kPitch = 0,
            kFbGain,
            kFbBody,
            kFbLpf,
            kFbHpf,
            kRevMix,
            kRevDecay,
            kEchoSend,
            kEchoTime,
            kEchoFb,
            kVol,
            kExcite,
            kCount
        };

        /**
         * Which setters are expensive enough to rate-limit.
         *
         * These four are the ones that end in a transcendental:
         *   pitch   -> mtof()   -> powf()
         *   fbgain  -> dbfs2lin -> pow10f() -> expf()
         *   fblpf   -> BiquadCascade::SetCutoff -> tanf()
         *   fbhpf   -> the same
         *
         * Measured at roughly 90 us apiece on the RP2350 at 192 MHz, by
         * difference: removing all twelve unconditional setters from the block
         * took it from 942 us to 578 us against a 666 us budget, and these four
         * are where nearly all of that sat. That is far more than a host
         * benchmark suggests — x86 does these in hardware, the M33 does not —
         * which is exactly why this table is keyed to a hardware measurement
         * and not to an estimate.
         *
         * Everything else is a store or two and is not worth gating.
         *
         * A mask rather than a bool array on purpose: a scalar constexpr used
         * by value is never odr-used, so it needs no out-of-class definition
         * and links under C++11, which is what the VCV plugin builds with.
         */
        static constexpr uint16_t kCostlyMask = (1u << kPitch) | (1u << kFbGain)
                                                | (1u << kFbLpf)
                                                | (1u << kFbHpf);

        static bool costly(const int i)
        { return ((kCostlyMask >> i) & 1u) != 0u; }

        float coef_[kCount]   = {1.0f};
        float value_[kCount]  = {0.0f};
        float eps_[kCount]    = {0.0f};
        /// Last value handed to the engine, per parameter — the deadband's
        /// reference point.
        float pushed_[kCount] = {0.0f};

        volatile uint8_t push_count_    = 0;
        /// Where pass 2 starts scanning, so costly parameters take turns.
        uint8_t          costly_cursor_ = 0;

        /// volatile: Snap() is called from the control core (preset recall,
        /// panic) while Step() reads this on the audio core.
        volatile bool primed_ = false;
};

}
}

#endif
