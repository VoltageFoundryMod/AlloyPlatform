// Host build check for the vendored Audrey engine (M63e).
//
// Proves the engine compiles and runs with no Daisy headers, no SDRAM
// allocator and no heap — which is the whole point of the vendoring step, and
// the thing that is cheapest to verify here rather than on a flashed board.
//
// It also prints the static footprint, which is the number that decides
// whether M63f is possible at all: the RP2350 has 520 KB total and AlloyFlux
// already uses ~348 KB of it, so Audrey has to fit in what a *separate*
// firmware image leaves after the platform's own overhead.
//
//   make audrey-host

#include "FeedbackSynthEngine.h"

#include <cmath>
#include <cstdio>

using infrasonic::FeedbackSynth::Engine;

// Static, not stack: this is how the firmware will hold it, and a multi-hundred
// kilobyte object would blow the default stack anywhere.
static Engine gEngine;

int main()
{
    constexpr float kSampleRate = 48000.0f;
    constexpr int   kSeconds    = 10;
    constexpr int   kFrames     = (int)kSampleRate * kSeconds;

    // Per-component footprint.  The RP2350 has 520 KB of SRAM in total, so
    // these are the numbers M63f has to shrink; upstream never had to care,
    // because the Daisy pushes all three big members into external SDRAM.
    auto kib = [](size_t b) { return b / 1024.0; };
    std::printf("--- static footprint (RP2350 has 520 KB total) ---\n");
    std::printf("  build: echo %d s, decimation /%d, %s storage, shaping %s\n",
                (int)AUDREY_ECHO_MAX_S,
                (int)AUDREY_ECHO_DECIMATION,
                AUDREY_ECHO_Q15 ? "int16" : "float",
                AUDREY_ECHO_NOISE_SHAPE ? "on" : "off");
    std::printf("  EchoDelay<%ds>      x2 = %8zu B  (%7.1f KiB)\n",
                (int)AUDREY_ECHO_MAX_S,
                sizeof(infrasonic::EchoDelay<48000 * AUDREY_ECHO_MAX_S>) * 2,
                kib(sizeof(infrasonic::EchoDelay<48000 * AUDREY_ECHO_MAX_S>) * 2));
    std::printf("  daisysp::ReverbSc     = %8zu B  (%7.1f KiB)\n",
                sizeof(daisysp::ReverbSc),
                kib(sizeof(daisysp::ReverbSc)));
    std::printf("  DelayLine<f,12000> x2 = %8zu B  (%7.1f KiB)\n",
                sizeof(daisysp::DelayLine<float, 12000>) * 2,
                kib(sizeof(daisysp::DelayLine<float, 12000>) * 2));
    std::printf("  KarplusString      x2 = %8zu B  (%7.1f KiB)\n",
                sizeof(infrasonic::KarplusString) * 2,
                kib(sizeof(infrasonic::KarplusString) * 2));
    std::printf("  sizeof(Engine)        = %8zu B  (%7.1f KiB)\n",
                sizeof(Engine),
                kib(sizeof(Engine)));

    gEngine.Init(kSampleRate);

    // Drive it the way the milestone says to stress it: maximum feedback gain
    // and echo feedback above unity, which is where a resonator with two
    // waveshapers inside its own loop either settles or runs away.
    gEngine.SetStringPitch(40.0f);
    gEngine.SetFeedbackGain(0.0f); // 0 dBFS — unity round the loop
    gEngine.SetFeedbackDelay(0.05f);
    gEngine.SetFeedbackLPFCutoff(18000.0f);
    gEngine.SetFeedbackHPFCutoff(60.0f);
    gEngine.SetEchoDelayTime(0.5f);
    gEngine.SetEchoDelayFeedback(1.05f); // deliberately over unity
    gEngine.SetEchoDelaySendAmount(0.8f);
    gEngine.SetReverbMix(0.5f);
    gEngine.SetReverbFeedback(0.9f);
    gEngine.SetOutputLevel(0.5f);

    double  sumL = 0.0, sumR = 0.0;
    float   peak = 0.0f;
    int     bad  = 0;
    // Samples outside ±1.0. The platform's AudioDriver::toWire() hard-clamps
    // there, so every one of these is a clipped sample at the DAC — which
    // sounds like crackle, not like the engine's own SoftClip.
    long clipped = 0;
    for(int i = 0; i < kFrames; ++i)
    {
        float outL = 0.0f, outR = 0.0f;
        // Impulse at the start, silence after — the engine has to sustain
        // itself on its own feedback from there.
        gEngine.Process(i == 0 ? 1.0f : 0.0f, outL, outR);
#if AUDREY_OUTPUT_SOFTCLIP
        // Mirror what renderAudio() does at the module's output edge, so the
        // clip count below reflects what actually reaches the DAC.
        outL = daisysp::SoftClip(outL);
        outR = daisysp::SoftClip(outR);
#endif

        if(!std::isfinite(outL) || !std::isfinite(outR))
        {
            if(bad == 0)
                std::printf("FAIL: non-finite output at frame %d\n", i);
            ++bad;
            continue;
        }
        sumL += outL;
        sumR += outR;
        const float a = std::fmax(std::fabs(outL), std::fabs(outR));
        if(a > peak)
            peak = a;
        if(std::fabs(outL) > 1.0f)
            ++clipped;
        if(std::fabs(outR) > 1.0f)
            ++clipped;
    }

    const double dcL = sumL / kFrames;
    const double dcR = sumR / kFrames;
    std::printf("%d s @ %.0f Hz: peak %.4f  DC L %+.6f  R %+.6f  non-finite %d\n",
                kSeconds,
                kSampleRate,
                peak,
                dcL,
                dcR,
                bad);
    std::printf("clipped at the DAC: %ld of %d samples (%.2f%%)\n",
                clipped,
                kFrames * 2,
                100.0 * (double)clipped / (double)(kFrames * 2));

    // Thresholds are sanity checks, not voicing judgements — the engine is
    // meant to be loud and self-sustaining here.  What must not happen is NaN,
    // silence, or a DC offset walking away.
    if(bad != 0)
    {
        std::printf("FAIL: %d non-finite samples\n", bad);
        return 1;
    }
    if(peak <= 0.0f)
    {
        std::printf("FAIL: silent — engine produced nothing\n");
        return 1;
    }
    if(std::fabs(dcL) > 0.05 || std::fabs(dcR) > 0.05)
    {
        std::printf("FAIL: DC offset drifted\n");
        return 1;
    }

    std::printf("OK\n");
    return 0;
}
