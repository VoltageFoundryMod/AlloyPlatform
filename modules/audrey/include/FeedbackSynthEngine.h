#pragma once
#ifndef IFS_FEEDBACK_SYNTH_ENGINE_H
#define IFS_FEEDBACK_SYNTH_ENGINE_H

#include "delayline.h"
#include "dsp.h"
#include "overdrive.h"
#include "reverbsc.h"
#include "whitenoise.h"

#include "BiquadFilters.h"
#include "EchoDelay.h"
#include "KarplusString.h"

namespace infrasonic {
namespace FeedbackSynth {

class Engine {

    public:
        Engine() = default;
        ~Engine() = default;

        void Init(const float sample_rate);

        void SetStringPitch(const float nn);

        void SetFeedbackGain(const float gain_dbfs);

        void SetFeedbackDelay(const float delay_s);
        void SetFeedbackLPFCutoff(const float cutoff_hz);
        void SetFeedbackHPFCutoff(const float cutoff_hz);

        void SetEchoDelayTime(const float echo_time);
        void SetEchoDelayFeedback(const float echo_fb);
        void SetEchoDelaySendAmount(const float echo_send);

        // Both range 0-1
        void SetReverbMix(const float mix);
        void SetReverbFeedback(const float time);

        void SetOutputLevel(const float level);

        void Process(float in, float &outL, float &outR);

    public:
        // Maximum echo time, in seconds at 48 kHz. Upstream is 5.0; this is the
        // one lever left once the echo is decimated and quantised, and it costs
        // ~11.7 KiB per second of stereo echo. Overridable at build time —
        // -DAUDREY_ECHO_MAX_S=4 — because it is a voicing decision, not a
        // technical one, and it is the difference between fitting the part
        // comfortably and fitting it by a hair.
#ifndef AUDREY_ECHO_MAX_S
#define AUDREY_ECHO_MAX_S 4
#endif

        // Master soft clip at the module's output edge (renderAudio()). On by
        // default: the engine peaks around 1.34 at useful settings even with
        // output level at 0.5, and the driver hard-clamps anything past ±1.0,
        // which crackles. Defined here rather than in main.cpp so the host
        // harness sees the same default and reports what the firmware really
        // does — it lived in main.cpp first and the harness kept reporting
        // clipping the firmware no longer had.
#ifndef AUDREY_OUTPUT_SOFTCLIP
#define AUDREY_OUTPUT_SOFTCLIP 1
#endif

    private:
        // long enough for 250ms at 48kHz
        static constexpr size_t kMaxFeedbackDelaySamp = 12000;
        static constexpr size_t kMaxEchoDelaySamp
            = static_cast<size_t>(48000 * AUDREY_ECHO_MAX_S);

        float sample_rate_;
        float fb_gain_ = 0.0f;
        float echo_send_ = 0.0f;
        float verb_mix_ = 0.0f;
        float output_level_ = 0.5f;

        float fb_delay_smooth_coef_;
        float fb_delay_samp_ = 1000.f;
        float fb_delay_samp_target_ = 64.f;

        infrasonic::KarplusString strings_[2];
        daisysp::WhiteNoise noise_;
        daisysp::DelayLine<float, kMaxFeedbackDelaySamp> fb_delayline_[2];
        daisysp::Overdrive overdrive_[2];

        LPF12 fb_lpf_;
        HPF12 fb_hpf_;

        // Concrete members, not unique_ptr.  Upstream heap-allocated these
        // three into the Daisy's external SDRAM because they are far too big
        // for its internal RAM.  The RP2350 has no SDRAM and no heap worth
        // using in an audio path, so they become static storage and their size
        // becomes a link-time fact instead of a run-time surprise.
        daisysp::ReverbSc verb_;
        EchoDelay<kMaxEchoDelaySamp> echo_delay_[2];

        Engine(const Engine &other) = delete;
        Engine(Engine &&other) = delete;
        Engine& operator=(const Engine &other) = delete;
        Engine& operator=(Engine &&other) = delete;
};

}
}

#endif
