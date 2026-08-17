// Audrey echo A/B — the two DSP risks M63f has been carrying, measured.
//
//   make audrey-ab
//
// M63f decimated the echo to 12 kHz and stored it as int16 to make it fit an
// RP2350. Both were argued for on paper and neither was ever verified. The
// milestone says to check them with a swept sine and a spectrum analyser; this
// does the same thing with numbers instead of eyes, which is reproducible and
// can gate a future change.
//
// Two questions, one per test:
//
//   A. Does the 24 dB/oct anti-alias filter actually catch the fold-down?
//      The echo send is tapped post-reverb and is full-bandwidth, so at /4
//      everything above 6 kHz folds back into the audible band — and 11 kHz
//      lands at 1 kHz, right where the echo's own 800 Hz bandpass passes it
//      through untouched. Measured by feeding a pure tone above Nyquist and
//      reading the energy at the frequency it folds to.
//
//      Note there are *two* artifacts here and only one of them is aliasing.
//      Going down, content above the decimated Nyquist folds — that is what the
//      AA filter is for. Coming back up, linear interpolation leaves
//      reconstruction images at k*fd +/- f, which no input filter can touch;
//      an input at 3 kHz images at 9 kHz whether or not it ever aliased. Both
//      are reported, because the second one turned out to dominate and it would
//      be easy to misread it as a failure of the first.
//
//   B. Does int16 quantisation noise regenerate when feedback exceeds unity?
//      The echo deliberately allows feedback past 1.0 for saturated swells,
//      which means anything the quantiser injects is recirculated and amplified
//      instead of decaying. Measured two ways: the residual floor left after a
//      decaying echo should have died, and whether silence in stays silence out
//      when the loop is over unity.
//
// The EchoDelay is exercised directly rather than through the Engine, so the
// string, the reverb and the feedback loop cannot colour the answer.
//
// Comparing configurations means rebuilding — these are compile-time switches
// by design. `make audrey-ab-sweep` runs the full matrix; see the Makefile.

// For AUDREY_ECHO_MAX_S. Only the macro is wanted, but taking it from the
// engine header rather than restating it is the point: the buffer under test
// has to be the size the firmware actually builds. Nothing here instantiates
// Engine, so no engine sources need linking.
#include "FeedbackSynthEngine.h"

#include "EchoDelay.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using infrasonic::EchoDelay;

static constexpr float  kSampleRate = 48000.0f;
// -std=c++14 is strict ANSI, which does not define M_PI.
static constexpr double kPi = 3.14159265358979323846;

// The echo under test, sized as the firmware sizes it. Static, not stack: at
// AUDREY_ECHO_DECIMATION=1 with float storage this is 768 KB.
static EchoDelay<(size_t)(48000 * AUDREY_ECHO_MAX_S)> gEcho;

// ---------------------------------------------------------------------------
// Minimal iterative radix-2 FFT. Real input, in-place complex output.
// A dependency-free ~40 lines beats linking a library into a test that runs
// eight transforms.
// ---------------------------------------------------------------------------
static void fft(std::vector<float> &re, std::vector<float> &im)
{
    const size_t n = re.size();
    for(size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for(; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if(i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for(size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double)len;
        const float  wr  = (float)std::cos(ang);
        const float  wi  = (float)std::sin(ang);
        for(size_t i = 0; i < n; i += len)
        {
            float cr = 1.0f, ci = 0.0f;
            for(size_t k = 0; k < len / 2; ++k)
            {
                const float ur = re[i + k], ui = im[i + k];
                const float vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                const float vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k]               = ur + vr;
                im[i + k]               = ui + vi;
                re[i + k + len / 2]     = ur - vr;
                im[i + k + len / 2]     = ui - vi;
                const float nr = cr * wr - ci * wi;
                ci             = cr * wi + ci * wr;
                cr             = nr;
            }
        }
    }
}

static float dbfs(float lin)
{ return lin <= 1e-12f ? -240.0f : 20.0f * std::log10(lin); }

/// Reset the echo to a clean, known state for one measurement.
static void reset(float delay_s, float feedback)
{
    gEcho.Init(kSampleRate);
    gEcho.SetLagTime(0.0f);
    gEcho.SetDelayTime(delay_s, true);
    gEcho.SetFeedback(feedback);
}

/// Priming stimulus: a 10 ms burst of 500 Hz, not a single-sample impulse.
///
/// This matters and it cost a confusing result to find out. The decimator only
/// samples the input every kDecim-th frame, so a one-sample impulse is simply
/// never seen — three times out of four at /4. With the anti-alias filter on,
/// the filter smears the impulse across enough samples that some of it gets
/// through, so the impulse "works"; with the filter off it vanishes entirely
/// and the echo reads as silent. That is a property of the test stimulus, not
/// of the echo, and comparing builds requires a stimulus that survives both.
static constexpr int kBurstFrames = (int)(kSampleRate * 0.010f);

static float burst(int n)
{
    if(n >= kBurstFrames)
        return 0.0f;
    const float w = 2.0f * (float)kPi * 500.0f / kSampleRate;
    // Raised-cosine envelope so the burst has no click of its own.
    const float env
        = 0.5f * (1.0f - std::cos(2.0f * (float)kPi * (float)n / (float)kBurstFrames));
    return env * std::sin(w * (float)n);
}

// ---------------------------------------------------------------------------
// Test A — alias rejection.
//
// Feed a unit sine at `f_in`, let the delay line settle, then capture a block
// and look at what came out. Anything at a frequency other than f_in is a
// product of the decimator, because a delay line is otherwise linear and
// time-invariant.
//
// The number that matters is the level at the *fold-down* frequency, which for
// a decimated rate `fd` is |f_in - round(f_in/fd)*fd| — the image of the input
// reflected about the decimated Nyquist.
// ---------------------------------------------------------------------------
static void testAlias()
{
    constexpr size_t kN       = 16384; // ~0.34 s at 48 kHz
    const float      decimHz  = kSampleRate / (float)AUDREY_ECHO_DECIMATION;
    const float      nyquist  = decimHz * 0.5f;

    std::printf("\n=== A. Alias rejection ===\n");
    std::printf("  decimated rate %.0f Hz, Nyquist %.0f Hz, anti-alias %s\n",
                decimHz,
                nyquist,
                AUDREY_ECHO_ANTIALIAS ? "ON" : "OFF");

    if(AUDREY_ECHO_DECIMATION == 1)
    {
        std::printf("  decimation is /1 — no fold-down is possible. "
                    "This build is the reference.\n");
    }

    std::printf("\n  %8s %8s %9s %9s %8s %9s %9s\n",
                "input",
                "wanted",
                "folds to",
                "at fold",
                "worst",
                "worst at",
                "vs wanted");
    std::printf("  %8s %8s %9s %9s %8s %9s %9s\n",
                "Hz",
                "out dB",
                "Hz",
                "dB",
                "spur dB",
                "Hz",
                "dB");
    std::printf("  %8s %8s %9s %9s %8s %9s %9s\n",
                "--------",
                "--------",
                "---------",
                "---------",
                "--------",
                "---------",
                "---------");

    // Probes chosen to land the fold-down in different places: 7 k folds to
    // 5 k (still high, masked), 11 k to 1 k (the bad one — the echo bandpass
    // is at 800 Hz and passes it), 13 k to 1 k as well from the other side.
    static const float kProbes[] = {
        3000.f, 5000.f, 7000.f, 9000.f, 11000.f, 13000.f, 15000.f, 17000.f};

    float worstOverall   = -240.0f;
    float worstOverallHz = 0.0f;

    for(float fin : kProbes)
    {
        // Feedback 0: one pass through the decimator and back, no loop, so the
        // measurement is of the resampler alone.
        reset(0.02f, 0.0f);

        // Settle: run one delay length plus filter transients before capturing.
        const int kWarm = (int)(kSampleRate * 0.25f);
        const float w   = 2.0f * (float)kPi * fin / kSampleRate;
        int         n   = 0;
        for(; n < kWarm; ++n)
            (void)gEcho.Process(std::sin(w * (float)n));

        std::vector<float> re(kN, 0.0f), im(kN, 0.0f);
        for(size_t i = 0; i < kN; ++i, ++n)
            re[i] = gEcho.Process(std::sin(w * (float)n));

        // Hann window — the probes are not bin-centred and leakage would swamp
        // a -80 dB alias product.
        double wsum = 0.0;
        for(size_t i = 0; i < kN; ++i)
        {
            const float win
                = 0.5f
                  * (1.0f
                     - std::cos(2.0f * (float)kPi * (float)i / (float)(kN - 1)));
            re[i] *= win;
            wsum += win;
        }
        fft(re, im);

        const float binHz = kSampleRate / (float)kN;
        const float norm  = 2.0f / (float)wsum;

        // Predicted fold-down: reflect f_in about the decimated Nyquist.
        const float k       = std::round(fin / decimHz);
        const float foldHz  = std::fabs(fin - k * decimHz);

        auto level = [&](float hz) {
            const int b = (int)std::round(hz / binHz);
            if(b <= 0 || b >= (int)kN / 2)
                return -240.0f;
            // Sum three bins — a windowed tone spreads across its neighbours.
            float acc = 0.0f;
            for(int d = -1; d <= 1; ++d)
                acc = std::fmax(acc, std::hypot(re[b + d], im[b + d]) * norm);
            return dbfs(acc);
        };

        // Worst spur anywhere in the audible band, excluding the input tone.
        float spur = -240.0f, spurHz = 0.0f;
        for(int b = 1; b < (int)kN / 2; ++b)
        {
            const float hz = (float)b * binHz;
            if(hz > 20000.0f)
                break;
            if(std::fabs(hz - fin) < 60.0f) // the input itself
                continue;
            const float l = dbfs(std::hypot(re[b], im[b]) * norm);
            if(l > spur)
            {
                spur   = l;
                spurHz = hz;
            }
        }

        // The echo's own output at the input frequency. Everything else has to
        // be judged against *this*, not against the input: the loop's 800 Hz
        // bandpass attenuates the wanted signal too, so a spur that is 40 dB
        // below the input may be only 5 dB below what you actually hear.
        const float wanted = level(fin);

        const bool folds = fin > nyquist && AUDREY_ECHO_DECIMATION > 1;
        char       foldCol[16], foldLvl[16];
        if(folds)
        {
            std::snprintf(foldCol, sizeof(foldCol), "%.0f", foldHz);
            std::snprintf(foldLvl, sizeof(foldLvl), "%.1f", level(foldHz));
        }
        else
        {
            std::snprintf(foldCol, sizeof(foldCol), "-");
            std::snprintf(foldLvl, sizeof(foldLvl), "-");
        }

        std::printf("  %8.0f %8.1f %9s %9s %8.1f %9.0f %9.1f\n",
                    fin,
                    wanted,
                    foldCol,
                    foldLvl,
                    spur,
                    spurHz,
                    spur - wanted);

        if(spur - wanted > worstOverall)
        {
            worstOverall   = spur - wanted;
            worstOverallHz = spurHz;
        }
    }

    std::printf("\n  worst spur relative to the wanted output: "
                "%.1f dB at %.0f Hz\n",
                worstOverall,
                worstOverallHz);
    std::printf("  (input tones are 0 dBFS; 'vs wanted' is the column that "
                "decides audibility)\n");
}

// ---------------------------------------------------------------------------
// Test B — quantiser noise.
//
// B1  Residual floor. Feedback 0.5, one impulse, run long enough that a float
//     echo would be inaudible, then measure what is left. Float storage decays
//     toward zero; int16 storage cannot go below its own quantisation step, so
//     whatever sits here is the price of the format.
//
// B2  Regeneration. Feedback 1.05, **silence in**, primed with a single
//     impulse. Over unity, anything in the loop grows. The question is whether
//     the quantiser keeps injecting new energy — reported as RMS per second, so
//     a floor that climbs is visible as a trend rather than a single number.
// ---------------------------------------------------------------------------
static void testQuantNoise()
{
    std::printf("\n=== B. Quantiser noise ===\n");
    std::printf("  storage %s, noise shaping %s\n",
                AUDREY_ECHO_Q15 ? "int16" : "float",
                AUDREY_ECHO_NOISE_SHAPE ? "on" : "off");

    // --- B1: residual floor after a decaying echo -------------------------
    {
        reset(0.25f, 0.5f);
        const int kTotal = (int)(kSampleRate * 12.0f);
        const int kTail  = (int)(kSampleRate * 1.0f);

        double tailSq = 0.0;
        float  tailPk = 0.0f;
        for(int i = 0; i < kTotal; ++i)
        {
            const float out = gEcho.Process(burst(i));
            if(i >= kTotal - kTail)
            {
                tailSq += (double)out * out;
                tailPk = std::fmax(tailPk, std::fabs(out));
            }
        }
        const float rms = (float)std::sqrt(tailSq / kTail);
        std::printf("\n  B1 residual after 12 s at feedback 0.5 "
                    "(0.5^48 is -14 dB per repeat, so this should be nothing):\n");
        std::printf("     RMS %8.1f dBFS   peak %8.1f dBFS\n",
                    dbfs(rms),
                    dbfs(tailPk));
    }

    // --- B2: does the quantiser inject energy of its own? -----------------
    //
    // The decisive test, and it needs *no* stimulus at all. Feedback is over
    // unity, so anything the quantiser adds is amplified 1.05x per repeat
    // forever. If the output is still exactly zero after 20 s, the quantiser
    // contributed nothing — which is the whole question. Priming with an
    // impulse (B3) cannot answer this, because then the loop is amplifying the
    // impulse and the quantiser's share is unobservable.
    {
        reset(0.25f, 1.05f);
        const int kSecs = 20;

        double sq = 0.0;
        float  pk = 0.0f;
        for(int i = 0; i < (int)(kSampleRate * kSecs); ++i)
        {
            const float out = gEcho.Process(0.0f); // pure silence in
            sq += (double)out * out;
            pk = std::fmax(pk, std::fabs(out));
        }
        const float rms = (float)std::sqrt(sq / (kSampleRate * kSecs));
        std::printf("\n  B2 silence in, no stimulus at all, feedback 1.05 "
                    "for %d s:\n",
                    kSecs);
        std::printf("     RMS %8.1f dBFS   peak %8.1f dBFS\n",
                    dbfs(rms),
                    dbfs(pk));
        std::printf("     Anything above the noise floor here is the "
                    "quantiser bootstrapping itself.\n");
    }

    // --- B3: growth rate once there IS something in the loop --------------
    //
    // Not a pass/fail — this is the instrument's intended behaviour, measured
    // so the two can be told apart. 1.05 per 0.25 s repeat is +1.7 dB/s until
    // SoftClip takes over.
    {
        reset(0.25f, 1.05f);
        const int kSecs = 20;

        std::printf("\n  B3 primed with a 10 ms burst, feedback 1.05 — the "
                    "intended swell, for reference:\n");
        std::printf("     %6s %12s %12s\n", "sec", "RMS dBFS", "peak dBFS");

        int n = 0;
        for(int s = 0; s < kSecs; ++s)
        {
            double sq = 0.0;
            float  pk = 0.0f;
            for(int i = 0; i < (int)kSampleRate; ++i, ++n)
            {
                const float out = gEcho.Process(burst(n));
                sq += (double)out * out;
                pk = std::fmax(pk, std::fabs(out));
            }
            if(s % 4 == 0 || s == kSecs - 1)
                std::printf("     %6d %12.1f %12.1f\n",
                            s + 1,
                            dbfs((float)std::sqrt(sq / kSampleRate)),
                            dbfs(pk));
        }
        std::printf("     (SoftClip inside the loop bounds this — a settled "
                    "level is the loop saturating, not a fault)\n");
    }

    // --- B4: signal-to-noise with the loop nearly at unity ----------------
    //
    // B1 and B2 both return an exact zero for float *and* int16 storage, which
    // is a real result but not a discriminating one: with nothing in the buffer
    // there is nothing to quantise. The number that separates the two formats
    // is the noise floor *while a signal is circulating*, with feedback high
    // enough that the quantiser's error is recirculated many times.
    //
    // Feedback 0.95, just under unity, so the loop converges to a steady state
    // instead of running away. Input is deliberately quiet (-40 dBFS): the
    // quantiser step is fixed, so the worse case for SNR is a small signal.
    {
        reset(0.25f, 0.95f);

        constexpr size_t kN     = 16384;
        constexpr float  kToneHz = 500.0f; // inside the loop's 800 Hz bandpass
        constexpr float  kAmp    = 0.01f;  // -40 dBFS

        const float w = 2.0f * (float)kPi * kToneHz / kSampleRate;
        int         n = 0;

        // Settle: ~60 repeats at 0.25 s.
        for(; n < (int)(kSampleRate * 15.0f); ++n)
            (void)gEcho.Process(kAmp * std::sin(w * (float)n));

        std::vector<float> re(kN, 0.0f), im(kN, 0.0f);
        for(size_t i = 0; i < kN; ++i, ++n)
            re[i] = gEcho.Process(kAmp * std::sin(w * (float)n));

        double wsum = 0.0;
        for(size_t i = 0; i < kN; ++i)
        {
            const float win
                = 0.5f
                  * (1.0f
                     - std::cos(2.0f * (float)kPi * (float)i / (float)(kN - 1)));
            re[i] *= win;
            wsum += win;
        }
        fft(re, im);

        const float binHz = kSampleRate / (float)kN;
        const float norm  = 2.0f / (float)wsum;

        // Split the spectrum into "the tone and its harmonics" and "everything
        // else". Everything else is what the quantiser costs.
        double tonePow = 0.0, noisePow = 0.0;
        float  tonePk = 0.0f;
        for(int b = 1; b < (int)kN / 2; ++b)
        {
            const float hz  = (float)b * binHz;
            const float mag = std::hypot(re[b], im[b]) * norm;
            const float p   = mag * mag;

            // SoftClip sits inside the loop, so the tone arrives with a long
            // harmonic series. That is intended distortion, not quantiser
            // noise — count it separately or the two formats look identical
            // because harmonic energy swamps both.
            bool isTone = false;
            for(int h = 1; h <= 20; ++h)
                if(std::fabs(hz - kToneHz * (float)h) < 3.0f * binHz)
                    isTone = true;

            if(isTone)
            {
                tonePow += p;
                if(std::fabs(hz - kToneHz) < 3.0f * binHz)
                    tonePk = std::fmax(tonePk, mag);
            }
            else
                noisePow += p;
        }

        std::printf("\n  B4 -40 dBFS 500 Hz tone, feedback 0.95, at steady "
                    "state:\n");
        std::printf("     tone out %8.1f dBFS   noise+spurs %8.1f dBFS   "
                    "SNR %6.1f dB\n",
                    dbfs(tonePk),
                    dbfs((float)std::sqrt(noisePow)),
                    dbfs(tonePk) - dbfs((float)std::sqrt(noisePow)));
        std::printf("     This is the number that separates int16 from float — "
                    "compare across builds.\n");
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    std::printf("Audrey echo A/B\n");
    std::printf("  build: echo %d s, decimation /%d, %s storage, "
                "shaping %s, anti-alias %s\n",
                (int)AUDREY_ECHO_MAX_S,
                (int)AUDREY_ECHO_DECIMATION,
                AUDREY_ECHO_Q15 ? "int16" : "float",
                AUDREY_ECHO_NOISE_SHAPE ? "on" : "off",
                AUDREY_ECHO_ANTIALIAS ? "on" : "off");
    std::printf("  storage: %zu B (%.1f KiB) per channel\n",
                sizeof(gEcho),
                sizeof(gEcho) / 1024.0);

    testAlias();
    testQuantNoise();

    std::printf("\ndone\n");
    return 0;
}
