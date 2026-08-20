// Alloy Coil — knob/CC agreement check.
//
//   make coil-params
//
// params.json is the single source of truth for every parameter's range and
// curve, but two consumers are *hand-written mirrors* of it and nothing was
// checking them:
//
//   io/IOBridge.h   what a knob or a CV does, on hardware and in VCV
//   vcv/AlloyCoil.cpp  the knob boot positions (now derived, see defaultPos())
//
// The generated consumers — param_manifest.generated.h and the web
// configurator's paramMapAlloyCoil.ts — cannot drift, because `make params`
// rewrites them. IOBridge can, and did: M63i changed fbbody and echotime from
// a log curve to square-law, and every place that number had been worked out by
// hand had to be found by eye.
//
// So this walks each knob across its travel and asserts that IOBridge lands on
// exactly what ParamDescriptor::fromPos() gives for the same position. If they
// agree, a knob, a MIDI CC and the web slider all reach the same value — which
// is the actual property worth protecting.

#include "io/PanelMap.h"
#include "param_manifest.generated.h"
#include "params.h"

#include "io/IOBridge.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// params.h globals — IOBridge writes these.
float gStringPitch   = 40.0f;
float gFeedbackGain  = -30.0f;
float gFeedbackDelay = 0.001f;
float gFeedbackLPF   = 18000.0f;
float gFeedbackHPF   = 250.0f;
float gEchoSend      = 0.0f;
float gEchoTime      = 0.5f;
float gEchoFeedback  = 0.0f;
float gReverbMix     = 0.0f;
float gReverbDecay   = 0.2f;
float gOutputLevel   = 0.5f;
float gExciterLevel  = 1.0f;

/// Every pot at the same position, nothing patched, no button held — so one
/// sweep exercises all twelve knobs at once.
struct FlatIO : IHardwareIO
{
    float pos = 0.0f;
    float readPot(PotId) override { return pos; }
    float readCV(CVId) override { return 0.0f; }
    bool  isPatched(CVId) override { return false; }
    bool  readButton(ButtonId) override { return false; }
    void  writeLight(LightId, float, float, float) override {}
};

int main()
{
    FlatIO io;
    int    failures = 0;

    std::printf("Alloy Coil — IOBridge vs params.json, %d parameters\n\n",
                (int)kParamManifestCount);
    std::printf("  %-10s %-8s %10s %10s\n", "param", "curve", "at 0.5", "worst err");

    for(uint8_t p = 0; p < kParamManifestCount; p++)
    {
        const ParamDescriptor &d = kParamManifest[p];

        float worst    = 0.0f;
        float worstPos = 0.0f;
        float atHalf   = 0.0f;

        for(int k = 0; k <= 64; k++)
        {
            io.pos = (float)k / 64.0f;
            fillCoilParams(io);

            const float want = d.fromPos(io.pos);
            const float got  = *d.target;
            if(std::fabs(io.pos - 0.5f) < 1e-6f)
                atHalf = got;

            // Relative, with an absolute floor for ranges that touch zero.
            const float err = std::fabs(got - want)
                              / (std::fabs(want) > 1e-6f ? std::fabs(want) : 1.0f);
            if(err > worst)
            {
                worst    = err;
                worstPos = io.pos;
            }
        }

        // 1e-5 covers float round-trip through powf(); anything larger is a
        // genuine disagreement between the two curves, not arithmetic noise.
        const bool bad = worst > 1.0e-5f;
        if(bad)
            failures++;

        const char *curve = (d.scale == ParamScale::Log)
                                ? "log"
                                : (d.skew != 1.0f ? "skew" : "linear");
        std::printf("  %-10s %-8s %10.5g %10.2e%s\n",
                    d.name,
                    curve,
                    atHalf,
                    worst,
                    bad ? "   ** MISMATCH" : "");
        if(bad)
            std::printf("      worst at knob %.4f: IOBridge gives %.6g, "
                        "params.json says %.6g\n",
                        worstPos,
                        (io.pos = worstPos, fillCoilParams(io), *d.target),
                        d.fromPos(worstPos));
    }

    // The knob boot positions the VCV module uses. Derived rather than written
    // down, so this asserts the derivation rather than a transcription.
    std::printf("\n  %-10s %12s %12s\n", "param", "default", "knob pos");
    for(uint8_t p = 0; p < kParamManifestCount; p++)
    {
        const ParamDescriptor &d   = kParamManifest[p];
        const float            pos = d.toPos(d.defVal);
        const float            rt  = d.fromPos(pos);
        const bool             bad = std::fabs(rt - d.defVal)
                         > (std::fabs(d.defVal) > 1e-6f
                                ? std::fabs(d.defVal) * 1e-4f
                                : 1e-6f);
        if(bad)
            failures++;
        std::printf("  %-10s %12.5g %12.6f%s\n",
                    d.name,
                    d.defVal,
                    pos,
                    bad ? "   ** does not round-trip" : "");
    }

    if(failures)
    {
        std::printf("\nFAIL: %d parameter(s) disagree with params.json\n", failures);
        return 1;
    }
    std::printf("\nOK — knob, MIDI CC and web slider all agree\n");
    return 0;
}
