// Filter response checks — OTALadder and SVFFilter.
//
// Written after the OTA ladder shipped with its zero-delay-feedback predictor
// missing the (1-G) per-stage factor.  A TPT one-pole is y = G*x + (1-G)*z, so
// four chained give y4 = (1-G)*(G^3*z0 + G^2*z1 + G*z2 + z3); the code had the
// bracket without the factor.  Because G = g/(1+g) climbs with cutoff, the
// feedback was overestimated by 1/(1-G) — nothing at 100 Hz, 1.6x at 8 kHz —
// and the resonance knob stopped meaning one thing:
//
//   res = 0.80, resonant peak     100 Hz  -0.5 dB      4 kHz  +14.4 dB
//
// which put the filter past self-oscillation over the top half of its cutoff
// range.  There it limits rather than resonates, so its gain runs backwards in
// input level (+31 dB at 0.006 FS, -10 dB at 0.92 FS), and opening the cutoff
// on a held note took 6.7 dB *out* of the passband instead of leaving it flat.
//
// None of that is visible in a spot check of one cutoff, which is why it
// survived: every individual setting sounds like a filter.  What it violates is
// a property *across* settings.  So that is what is asserted here — the shape
// of the response has to be independent of where the cutoff sits, and (in the
// linear region, below where tanh engages) of how loud the input is.
//
// The SVF checks are cheaper and were passing already; they are here to keep
// the Cytomic tap set from drifting during a later edit to the shared header.
//
//   make flux-filter

#include "dsp/OTALadder.h"
#include "dsp/SVFFilter.h"
#include <cmath>
#include <cstdio>

static const float  kSR = 48000.0f;
static const double kPi = 3.14159265358979323846;

static int failures = 0;

static void check(bool ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok)
        failures++;
}

static double db(double x) { return 20.0 * log10(x < 1e-12 ? 1e-12 : x); }

// Steady-state gain at one frequency, by correlating the output against the
// drive.  Warm-up has to outlast the ringing of a high-Q setting, so it is
// generous rather than tuned.
static double gainAt(FilterEngine *f, double freq, double ampFS)
{
    f->reset();
    const int    warm = 40000, meas = 48000;
    const double w = 2.0 * kPi * freq / kSR;
    const double a = ampFS * 32512.0;
    for(int n = 0; n < warm; n++)
    {
        int32_t o1, o2;
        int32_t in = (int32_t)lrint(a * sin(w * n));
        f->process(in, in, &o1, &o2);
    }
    double re = 0, im = 0;
    for(int n = 0; n < meas; n++)
    {
        int32_t o1, o2;
        int32_t in = (int32_t)lrint(a * sin(w * (n + warm)));
        f->process(in, in, &o1, &o2);
        re += o1 * cos(w * n);
        im += o1 * sin(w * n);
    }
    return 2.0 * sqrt(re * re + im * im) / meas / a;
}

static double spread(const double *v, int n)
{
    double lo = v[0], hi = v[0];
    for(int i = 1; i < n; i++)
    {
        if(v[i] < lo)
            lo = v[i];
        if(v[i] > hi)
            hi = v[i];
    }
    return hi - lo;
}

static const double kCutoffs[] = {100.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0};
static const int    kNCutoffs  = (int)(sizeof(kCutoffs) / sizeof(kCutoffs[0]));

int main()
{
    printf("OTALadder — resonant peak must not depend on cutoff\n");
    {
        const double resSet[2] = {0.5, 0.8};
        for(int r = 0; r < 2; r++)
        {
            double pk[kNCutoffs];
            printf("  res=%.2f:", resSet[r]);
            for(int i = 0; i < kNCutoffs; i++)
            {
                OTALadder l;
                l.setParams(
                    (float)kCutoffs[i], (float)resSet[r], FilterMode::LP4, kSR);
                pk[i] = db(gainAt(&l, kCutoffs[i], 0.05));
                printf(" %.0fHz:%.1fdB", kCutoffs[i], pk[i]);
            }
            const double s = spread(pk, kNCutoffs);
            printf("   spread %.2f dB\n", s);
            // Was 6.0 dB at res 0.5 and 14.9 dB at res 0.8 before the fix.
            check(s < 1.0, "gain at cutoff is the same at every cutoff");
        }
    }

    printf("\nOTALadder — response must not depend on input level while linear\n");
    {
        // The piecewise tanh is exactly x for |x| <= 0.75, so everything below
        // that has to be strictly linear.  Past self-oscillation it is not: the
        // output pins near constant amplitude and gain falls as drive rises.
        //
        // Ceiling is 0.06 FS, not something louder: at res 0.8 the feedback
        // term k*sigma is ~3.6x the output, so 0.2 FS in already reaches the
        // knee legitimately (measures 1.1 dB down) and the check would be
        // testing the saturator rather than the predictor.  0.06 is well clear,
        // and still spanned 17 dB on the broken predictor.
        double       g[4];
        const double amps[4] = {0.002, 0.006, 0.02, 0.06};
        printf("  fc=4000 res=0.80:");
        for(int i = 0; i < 4; i++)
        {
            OTALadder l;
            l.setParams(4000.0f, 0.8f, FilterMode::LP4, kSR);
            g[i] = db(gainAt(&l, 4000.0, amps[i]));
            printf(" %.3fFS:%.1fdB", amps[i], g[i]);
        }
        const double s = spread(g, 4);
        printf("   spread %.2f dB\n", s);
        // Was 30.8 dB before the fix.
        check(s < 1.0, "gain is level-independent below the tanh knee");
    }

    printf("\nOTALadder — opening the cutoff must not drain the passband\n");
    {
        double p[kNCutoffs];
        printf("  200 Hz tone, res=0.80:");
        for(int i = 0; i < kNCutoffs; i++)
        {
            OTALadder l;
            l.setParams((float)kCutoffs[i], 0.8f, FilterMode::LP4, kSR);
            p[i] = db(gainAt(&l, 200.0, 0.05));
            printf(" fc%.0f:%.1fdB", kCutoffs[i], p[i]);
        }
        printf("\n");
        // Only cutoffs at or above the tone: below it the tone is stopband and
        // is meant to be attenuated.
        double above[kNCutoffs];
        int    na = 0;
        for(int i = 0; i < kNCutoffs; i++)
            if(kCutoffs[i] >= 400.0)
                above[na++] = p[i];
        const double s = spread(above, na);
        printf("  passband spread above the tone: %.2f dB\n", s);
        check(s < 1.5, "passband holds flat as the cutoff opens");
    }

    printf("\nOTALadder — 4-pole rolloff and a monotonic resonance taper\n");
    {
        // Measured 2k->4k on a loud drive.  An octave higher the response is
        // 70 dB down, which at this integer scale is under 1 LSB — the probe
        // reads the quantization floor rather than the filter.  res = 0 keeps
        // every internal node below the tanh knee even at 0.5 FS.
        OTALadder l;
        l.setParams(1000.0f, 0.0f, FilterMode::LP4, kSR);
        const double slope
            = db(gainAt(&l, 4000.0, 0.5)) - db(gainAt(&l, 2000.0, 0.5));
        printf("  2k->4k slope %.1f dB/oct\n", slope);
        check(slope < -20.0 && slope > -27.0, "rolloff is 4-pole (-24 dB/oct)");

        const double resSet[5] = {0.0, 0.25, 0.5, 0.75, 0.9};
        double       prev      = -1e9;
        bool         mono      = true;
        printf("  taper at fc=1000:");
        for(int i = 0; i < 5; i++)
        {
            OTALadder t;
            t.setParams(1000.0f, (float)resSet[i], FilterMode::LP4, kSR);
            const double g = db(gainAt(&t, 1000.0, 0.05));
            printf(" %.2f:%.1fdB", resSet[i], g);
            if(g <= prev + 0.5)
                mono = false;
            prev = g;
        }
        printf("\n");
        check(mono, "resonance rises monotonically with the knob");
    }

    printf("\nOTALadder — res 1.0 self-oscillates, on pitch, at every cutoff\n");
    {
        const double oscFc[3] = {200.0, 1000.0, 4000.0};
        for(int i = 0; i < 3; i++)
        {
            const double fc = oscFc[i];
            OTALadder    l;
            l.setParams((float)fc, 1.0f, FilterMode::LP4, kSR);
            int32_t a, b;
            l.process(20000, 20000, &a, &b); // kick once, then leave it alone
            double  pk   = 0;
            int     zc   = 0;
            int32_t prev = 0;
            for(int n = 0; n < 144000; n++)
            {
                l.process(0, 0, &a, &b);
                if(n < 96000)
                    continue;
                if(fabs((double)a) > pk)
                    pk = fabs((double)a);
                if(prev <= 0 && a > 0)
                    zc++;
                prev = a;
            }
            const double amp  = pk / 32512.0;
            const double freq = zc * kSR / 48000.0;
            printf("  fc=%6.0f  amp %.3f FS  pitch %.0f Hz\n", fc, amp, freq);
            check(amp > 0.1, "oscillation sustains rather than dying out");
            check(amp < 0.95, "it stays a sine instead of squaring on the rail");
            check(fabs(freq - fc) < fc * 0.05, "it oscillates at the cutoff");
        }
    }

    printf("\nOTALadder — makeup gain must not cost headroom\n");
    {
        // _makeup lifts the resonant peak along with the passband, and the peak
        // is already the loudest thing in the signal, so the makeup depth is
        // bounded by clipping rather than by taste.  At exponent 0.35 a loud saw
        // starts clamping (4.7% of samples at res 1.0) and at 0.5 the
        // self-oscillation squares off against the rail.  0.25 clips nowhere.
        // This is the check that stops someone raising it for a bit more level.
        const double resSet[3] = {0.5, 0.9, 1.0};
        for(int i = 0; i < 3; i++)
        {
            OTALadder l;
            l.setParams(2000.0f, (float)resSet[i], FilterMode::LP4, kSR);
            double ph = 0;
            const double inc = 110.0 / kSR;
            int    clipped = 0, counted = 0;
            double pk = 0;
            for(int n = 0; n < 80000; n++)
            {
                ph += inc;
                if(ph >= 1.0)
                    ph -= 1.0;
                const int32_t in = (int32_t)lrint((2.0 * ph - 1.0) * 0.5 * 32512.0);
                int32_t       o1, o2;
                l.process(in, in, &o1, &o2);
                if(n < 20000)
                    continue;
                counted++;
                if(fabs((double)o1) > pk)
                    pk = fabs((double)o1);
                if(o1 >= 32512 || o1 <= -32512)
                    clipped++;
            }
            const double pct = 100.0 * clipped / counted;
            printf("  res=%.2f  110 Hz saw at 0.5 FS -> peak %.3f FS, "
                   "clamp hits %.2f%%\n",
                   resSet[i],
                   pk / 32512.0,
                   pct);
            check(pct < 0.05, "a loud saw does not reach the clamp");
        }
    }

    printf("\nOTALadder — passband must match SVFFilter at res 0\n");
    {
        // The 0.5 drive scale in _chan() is tanh headroom, not a level change,
        // and shipped uncompensated: the ladder ran 6.02 dB under the SVF, so
        // switching filter type dropped the patch audibly.  kMakeup undoes it.
        //
        // Only asserted at res = 0, where the (1+k)^0.25 term is 1 and the two
        // filters have to agree exactly.  Above it the ladder's own feedback
        // pulls DC gain to 1/(1+k) — 15.8 dB down at res 1.0, of which _makeup
        // gives back a quarter in dB.  That residual thinning is real ladder
        // behaviour and is deliberately left in place.
        SVFFilter s;
        s.setParams(2000.0f, 0.0f, FilterMode::LP, kSR);
        OTALadder l;
        l.setParams(2000.0f, 0.0f, FilterMode::LP4, kSR);
        const double gs  = db(gainAt(&s, 20.0, 0.05));
        const double gl  = db(gainAt(&l, 20.0, 0.05));
        const double gap = fabs(gs - gl);
        printf("  SVF %.2f dB, ladder %.2f dB, gap %.2f dB\n", gs, gl, gap);
        check(gap < 0.2, "the two filter types are level-matched at res 0");
    }

    printf("\nSVFFilter — Cytomic tap set\n");
    {
        SVFFilter s;
        s.setParams(1000.0f, 0.0f, FilterMode::LP, kSR);
        const double lpDC = db(gainAt(&s, 10.0, 0.05));
        const double lpFc = db(gainAt(&s, 1000.0, 0.05));
        s.setParams(1000.0f, 0.0f, FilterMode::HP, kSR);
        const double hpNy = db(gainAt(&s, 20000.0, 0.05));
        s.setParams(1000.0f, 0.0f, FilterMode::NOTCH, kSR);
        const double nFc = db(gainAt(&s, 1000.0, 0.05));
        printf("  LP DC %.2f dB, LP at fc %.2f dB, HP at 20k %.2f dB, "
               "notch null %.0f dB\n",
               lpDC,
               lpFc,
               hpNy,
               nFc);
        check(fabs(lpDC) < 0.1, "LP passes DC at unity");
        check(fabs(hpNy) < 0.1, "HP passes Nyquist at unity");
        check(fabs(lpFc + 6.02) < 0.3, "res=0 gives k=2, so -6 dB at cutoff");
        check(nFc < -60.0, "notch is a real null");

        // Small drive: at res 0.9 the SVF peaks ~14 dB and hard-clips at the
        // rail on anything louder — see the clamp in SVFFilter::_chan().
        double pk[kNCutoffs];
        for(int i = 0; i < kNCutoffs; i++)
        {
            SVFFilter t;
            t.setParams((float)kCutoffs[i], 0.9f, FilterMode::LP, kSR);
            pk[i] = db(gainAt(&t, kCutoffs[i], 0.002));
        }
        printf("  res=0.9 peak spread across cutoff: %.2f dB\n",
               spread(pk, kNCutoffs));
        check(spread(pk, kNCutoffs) < 0.5,
              "SVF resonance is cutoff-independent too");
    }

    printf("\n%s (%d failure%s)\n",
           failures ? "FAILURES" : "all checks passed",
           failures,
           failures == 1 ? "" : "s");
    return failures;
}
