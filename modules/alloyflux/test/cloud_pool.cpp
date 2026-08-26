// CLOUD oscillator-pool checks (M78).
//
// The pool is the part of the mode whose correctness the audio output will not
// show you. An oscillator handed to two stacks at once, a stack left a saw
// short, a width that oversubscribes the pool — every one of those sounds like
// a slightly different supersaw rather than like a bug, and would sit in the
// firmware indefinitely. So the plan is read back through
// SynthEngine::cloudPoolState() and asserted on directly.
//
// The two properties the mode was built around, and which are easy to lose in
// a later refactor:
//
//   1. A single held note still gets the full seven-saw stack. If it does not,
//      playing one note sounds thinner than the drone and the mode has lost
//      the thing that makes it CLOUD.
//   2. Nothing steps. The drone stopping, a stack narrowing as a note joins,
//      an oscillator being re-tasked — all of it has to be a fade. This is
//      checked as a max sample-to-sample delta against the steady-state one,
//      which is what caught the drone fading at control rate (7.7x steady, an
//      audible click) during development.
//
//   make flux-cloud

#include "SynthEngine.h"
#include <cmath>
#include <cstdio>

// Globals params.h declares extern that the engine reads.
volatile bool gGatePatched = false;
volatile bool gGateHigh    = false;

static SynthEngine eng;
static PolySlot    slots[6];
static int         failures = 0;

static void check(bool ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok)
        failures++;
}

static void allNotesOff()
{
    for(int i = 0; i < 6; i++)
    {
        slots[i].freq     = 440.0f;
        slots[i].velocity = 1.0f;
        slots[i].midiNote = kPolySlotFree;
        if(eng.polyEnvs[i])
        {
            eng.polyEnvs[i]->setGate(false);
            eng.polyEnvs[i]->reset();
        }
    }
}

static void noteOn(uint8_t slot, float freq, uint8_t note, float vel = 1.0f)
{
    slots[slot].freq     = freq;
    slots[slot].velocity = vel;
    slots[slot].midiNote = note;
    eng.polyRetrigger(slot, freq, 0.5f);
}

static SynthParams mkParams(bool gatePatched, uint8_t pool, uint8_t notes)
{
    SynthParams p;
    p.voiceMode     = VoiceMode::CLOUD;
    p.baseFreq      = 220.0f;
    p.relation      = 12.0f;
    p.color         = 0.5f;
    p.fatness       = 0.0f; // sub off: isolate the main stacks
    p.motion        = 0.0f; // drift off: frequencies stay predictable
    p.gatePatched   = gatePatched;
    p.cloudPool     = pool;
    p.cloudMaxNotes = notes;
    return p;
}

// Render `ticks` control periods. RMS is averaged over the whole span — a
// detuned supersaw beats by several Hz, so a short window swings ±35% and says
// nothing about level. maxStep is the click detector.
static void
render(const SynthParams &p, int ticks, float *rmsOut, float *maxStepOut)
{
    SynthControlOutput co;
    double             sum     = 0.0;
    long               n       = 0;
    float              maxStep = 0.0f;
    static float       prevL   = 0.0f;
    for(int t = 0; t < ticks; t++)
    {
        eng.control(p, slots, co);
        for(int s = 0; s < 375; s++) // 48000 / 128
        {
            int32_t l, r, dl, dr;
            eng.audio(0, 0, 0.0f, false, &l, &r, &dl, &dr);
            sum += (double)l * l + (double)r * r;
            n += 2;
            const float step = fabsf((float)l - prevL);
            if(step > maxStep)
                maxStep = step;
            prevL = (float)l;
        }
    }
    if(rmsOut)
        *rmsOut = (float)sqrt(sum / (double)n);
    if(maxStepOut)
        *maxStepOut = maxStep;
}

// ---------------------------------------------------------------------------
// How much of its level a released note still holds after `ticks` control
// periods, as a fraction of where it was when the gate fell.
//
// A *fraction* rather than an absolute level, so the measurement does not have
// to sit through a slow attack first: the release is one-pole, so level(t) /
// level(0) is exp(-t/T) wherever the attack had got to. That is the whole
// quantity of interest — T is what the envelope controls are supposed to set.
//
// Driven through eng.control()/eng.audio() rather than by poking an envelope
// directly, because the bug this guards against was never in the envelope: it
// was that nothing told CLOUD's envelopes what CURVE was.
// ---------------------------------------------------------------------------
static float releaseFraction(SynthParams p, int settle, int ticks)
{
    gGatePatched = true;
    // Let any envelope-type or voice-mode change land *before* the note
    // starts. Both reset the per-slot envelopes, which would silence a note
    // gated on the tick before and leave nothing to measure.
    render(p, 4, nullptr, nullptr);
    allNotesOff();
    noteOn(0, 220.0f, 60);
    render(p, settle, nullptr, nullptr);

    const float atRelease = eng.polyEnvs[0] ? eng.polyEnvs[0]->level() : 0.0f;
    slots[0].midiNote     = kPolySlotFree;
    if(eng.polyEnvs[0])
        eng.polyEnvs[0]->setGate(false);
    render(p, ticks, nullptr, nullptr);

    if(atRelease <= 0.0f)
        return -1.0f; // never attacked — the caller's check will fail loudly
    return eng.polyEnvs[0]->level() / atRelease;
}

static void checkPlan(const char *label, int expectStacks, int expectWidth)
{
    SynthEngine::CloudPoolState st;
    eng.cloudPoolState(st);

    int  perStack[kCloudMaxNotes + 1] = {};
    int  inPlay                       = 0;
    bool dupe                         = false;
    for(int i = 0; i < kCloudOscMax; i++)
    {
        if(st.owner[i] == kCloudNoOwner)
            continue;
        inPlay++;
        perStack[st.owner[i]]++;
        // The same detune must not be sounding twice in one stack: that is
        // both a wasted oscillator and two saws beating at exactly 0 Hz.
        for(int j = i + 1; j < kCloudOscMax; j++)
            if(st.owner[j] == st.owner[i] && st.offset[j] == st.offset[i])
                dupe = true;
    }
    int stacks = 0;
    for(int s = 0; s <= kCloudMaxNotes; s++)
        if(perStack[s])
            stacks++;

    printf("  %-22s width=%d stacks=%d inPlay=%2d drone=%d  per-stack:",
           label,
           st.width,
           stacks,
           inPlay,
           (int)st.droning);
    for(int s = 0; s <= kCloudMaxNotes; s++)
        if(perStack[s])
            printf(
                " %s=%d", s == kCloudDroneSlot ? "drone" : "note", perStack[s]);
    printf("\n");

    check(!dupe, "no detune assigned twice within a stack");
    check(inPlay <= st.pool, "pool never oversubscribed");
    if(expectWidth > 0)
        check(st.width == expectWidth, "expected saws per note");
    if(expectStacks > 0)
        check(stacks == expectStacks, "expected stack count");
}

int main()
{
    eng.init(48000u, 128u);
    float rms, step;

    // ---- the width ladder, and that it always fits ----------------------
    printf("\n== Width ladder ==\n");
    struct
    {
        uint8_t pool;
        int     want[4];
    } ladder[] = {
        {12, {7, 5, 3, 3}}, // default
        {15, {7, 7, 5, 3}}, // the "push it" setting
        {10, {7, 5, 3, 1}}, // degrades to a single saw rather than overrunning
    };
    for(auto &L : ladder)
    {
        printf("  pool %2d: ", L.pool);
        bool ok = true;
        for(int n = 1; n <= 4; n++)
        {
            uint8_t w = 7;
            while(w > 1 && (uint16_t)w * (uint16_t)n > (uint16_t)L.pool)
                w -= 2;
            printf("%d ", w);
            if(w != L.want[n - 1] || (uint16_t)w * n > L.pool)
                ok = false;
        }
        printf("\n");
        check(ok, "ladder matches and fits the pool");
    }

    // ---- drone ----------------------------------------------------------
    printf("\n== Drone: no gate, no notes ==\n");
    gGatePatched = false;
    allNotesOff();
    SynthParams drone = mkParams(false, 12, 4);
    render(drone, 40, nullptr, nullptr);
    checkPlan("drone", 1, 7);
    float droneRms;
    render(drone, 256, &droneRms, nullptr);
    printf("  drone RMS = %.0f\n", droneRms);
    check(droneRms > 500.0f, "drone sounds with no gate and no notes");

    // ---- one note keeps the full stack ----------------------------------
    printf("\n== One held note ==\n");
    gGatePatched = true;
    allNotesOff();
    noteOn(0, 220.0f, 60);
    SynthParams p1 = mkParams(true, 12, 4);
    render(p1, 60, nullptr, nullptr);
    checkPlan("1 note", 1, 7);
    float oneRms;
    render(p1, 256, &oneRms, nullptr);
    // One stack is one stack: the drone and a single held note run at the same
    // level (kCloudStackLevel), so playing a note does not sound like the
    // module dropped in volume. Chords get louder from there — that is what a
    // chord does — and the output saturator catches the sum.
    printf("  one-note RMS = %.0f (%.3f x drone)\n", oneRms, oneRms / droneRms);
    check(fabsf(oneRms / droneRms - 1.0f) < 0.15f,
          "one held note plays at the drone's level");

    // ---- narrowing ------------------------------------------------------
    printf("\n== Adding notes ==\n");
    noteOn(1, 277.2f, 64);
    render(p1, 60, nullptr, nullptr);
    checkPlan("2 notes", 2, 5);

    noteOn(2, 329.6f, 67);
    render(p1, 60, nullptr, nullptr);
    checkPlan("3 notes", 3, 3);

    noteOn(3, 415.3f, 71);
    render(p1, 60, nullptr, nullptr);
    checkPlan("4 notes", 4, 3);

    // A fifth note must not be planned: cloudMaxNotes is the ceiling, and a
    // fifth stack would take the pool past what it can render.
    noteOn(4, 493.9f, 74);
    render(p1, 60, nullptr, nullptr);
    checkPlan("5th note offered", 4, 3);

    // ---- widening back --------------------------------------------------
    printf("\n== Releasing back to one note ==\n");
    // Explicitly short envelopes, because how fast the pool widens back out is
    // a function of CURVE: a released stack keeps its oscillators for as long
    // as it is still audible (see _cloudSounding), and at the default CURVE a
    // release tail runs for about three seconds. This check used to pass
    // without saying so, back when CLOUD ignored CURVE entirely and every
    // release was AREnvelope's default ~21 ms whatever the knob said.
    SynthParams rel = mkParams(true, 12, 4);
    rel.curveTime   = 0.05f;
    render(rel, 60, nullptr, nullptr); // let the short release smooth in first
    for(int i = 1; i < 6; i++)
    {
        slots[i].midiNote = kPolySlotFree;
        if(eng.polyEnvs[i])
            eng.polyEnvs[i]->setGate(false);
    }
    render(rel, 200, nullptr, nullptr);
    checkPlan("back to 1 note", 1, 7);

    // ---- and back to the drone, at its original level -------------------
    printf("\n== Return to drone ==\n");
    gGatePatched = false;
    allNotesOff();
    SynthParams back = mkParams(false, 12, 4);
    render(back, 60, nullptr, nullptr);
    checkPlan("drone again", 1, 7);
    float backRms;
    render(back, 256, &backRms, nullptr);
    printf("  drone RMS = %.0f (was %.0f, ratio %.3f)\n",
           backRms,
           droneRms,
           backRms / droneRms);
    check(fabsf(backRms / droneRms - 1.0f) < 0.12f,
          "drone returns at the level it left at");

    // ---- nothing steps --------------------------------------------------
    printf("\n== Transitions are fades, not steps ==\n");
    // Slow envelopes so a note attack cannot be mistaken for a pool click.
    SynthParams t         = mkParams(true, 12, 4);
    t.curve               = 0.95f;
    t.curveTime           = 6.0f;
    SynthParams slowDrone = back;
    slowDrone.curve       = 0.95f;
    slowDrone.curveTime   = 6.0f;

    // Steady-state reference. Taken over a long span on purpose: a detuned
    // supersaw beats, so the worst sample step in any short window depends on
    // where in the beat you looked, and an unstable reference makes every
    // comparison below meaningless.
    gGatePatched = false;
    allNotesOff();
    render(slowDrone, 40, &rms, &step);
    render(slowDrone, 128, &rms, &step);
    printf("  steady-state max sample step = %.0f (RMS %.0f)\n", step, rms);
    const float steady = step;

    // Velocity 0 mutes the incoming stack, so this measures the drone fading
    // out and nothing else. A note attack cannot hide in this number.
    gGatePatched = true;
    noteOn(0, 220.0f, 60, 0.0f);
    render(t, 40, &rms, &step);
    printf("  drone -> note, drone fade only = %.0f (%.1fx steady)\n",
           step,
           step / steady);
    check(step < steady * 2.0f, "drone fades out without a step");

    gGatePatched = false;
    allNotesOff();
    render(slowDrone, 60, &rms, &step);
    gGatePatched = true;
    noteOn(0, 220.0f, 60);
    render(t, 40, &rms, &step);
    printf("  drone -> first note        = %.0f (%.1fx steady)\n",
           step,
           step / steady);
    check(step < steady * 2.0f, "entering the first note does not step");

    noteOn(1, 277.2f, 64);
    render(t, 40, &rms, &step);
    printf("  1 -> 2 notes (7 -> 5 saws) = %.0f (%.1fx steady)\n",
           step,
           step / steady);
    check(step < steady * 2.0f, "narrowing the stack does not step");

    // ---- headroom --------------------------------------------------------
    //
    // Two things are checked here, and they pull against each other.
    //
    // Nothing may ever reach the rail: a hard-clipped supersaw fed into the
    // delay's feedback path recirculates and is heard as ringing, which is what
    // was reported from hardware at three notes up.
    //
    // But the saturator must also stay out of the way of simple playing. The
    // always-on Padé clip this module used to carry was removed twice for
    // distorting clean sines nowhere near the ceiling, and a curve that shapes
    // a sustained drone would be the same mistake. So the drone and a single
    // note have to sit *under the knee*, where the path is provably linear.
    printf("\n== Headroom: saw, FATNESS 0.7, delay 0.5 ==\n");
    {
        SynthParams h   = mkParams(true, 16, 4);
        h.shape         = 0.5f; // saw
        h.fatness       = 0.7f;
        h.delayMix      = 0.5f;
        h.delayFeedback = 0.5f;
        h.delayTime     = 300.0f;

        static const float freqs[4] = {220.0f, 277.2f, 329.6f, 415.3f};

        auto measure = [&](const char *label, int notes, float maxShaped)
        {
            gGatePatched = (notes > 0);
            allNotesOff();
            for(int n = 0; n < notes; n++)
                noteOn((uint8_t)n, freqs[n], (uint8_t)(60 + n));
            h.gatePatched = (notes > 0);
            render(h, 90, nullptr, nullptr);

            SynthControlOutput co;
            long               railed = 0, shaped = 0, total = 0;
            float              peak = 0.0f;
            for(int t = 0; t < 128; t++)
            {
                eng.control(h, slots, co);
                for(int s = 0; s < 375; s++)
                {
                    int32_t l, r, dl, dr;
                    eng.audio(0, 0, 0.0f, false, &l, &r, &dl, &dr);
                    total += 2;
                    const float al = fabsf((float)l), ar = fabsf((float)r);
                    if(al > peak)
                        peak = al;
                    if(ar > peak)
                        peak = ar;
                    if(al > kSatKnee)
                        shaped++;
                    if(ar > kSatKnee)
                        shaped++;
                    if(l >= 32767 || l <= -32767)
                        railed++;
                    if(r >= 32767 || r <= -32767)
                        railed++;
                }
            }
            const float pctShaped = 100.0f * (float)shaped / (float)total;
            printf(
                "  %-8s peak %5.0f (%3.0f%% FS)  railed %.2f%%  shaped "
                "%.2f%%\n",
                label,
                peak,
                100.0f * peak / 32767.0f,
                100.0f * (float)railed / (float)total,
                pctShaped);
            check(railed == 0, "never reaches the rail");
            if(maxShaped >= 0.0f)
                check(pctShaped <= maxShaped, "stays inside the linear region");
        };

        // With this much delay on it, even a single note grazes the knee — but
        // that is the saturator acting on the *delay's* output (its mix law
        // reaches 1.25x), not colouring the oscillators. All that is required
        // here is that nothing rails.
        measure("drone", 0, -1.0f);
        measure("1 note", 1, -1.0f);
        measure("2 notes", 2, -1.0f);
        measure("3 notes", 3, -1.0f);
        measure("4 notes", 4, -1.0f);

        // Dry, sustained material must stay inside the *masking budget*.
        //
        // CLOUD deliberately runs into the saturator — it is 5.5 dB down on the
        // mono modes purely through crest factor, and waveshaping the peaks is
        // the only thing that closes any of that. So this cannot assert an
        // untouched signal. What it can assert is that the colouring stays
        // where seven detuned sawtooths hide it.
        //
        // The bound is on samples *touching* the curve, which is a loose proxy:
        // a sample just past the knee is barely altered, since the slope there
        // is exactly 1. Measured against it, kCloudStackLevel 1.10 gives ~1.8%
        // touched and 2.1% actual distortion. 2.5% here corresponds to roughly
        // 1.25–1.30, which is where distortion passes 3.5% and starts being
        // audible as dirt rather than as level — so a careless bump trips this
        // while the deliberate setting passes.
        //
        // ⚠ The number that matters is what the distortion lands *on*. The
        // always-on Padé clip removed twice from this module put 6–7% on clean
        // single-oscillator sines and reverb tails, where nothing masks it.
        // Comparing that figure to this one without asking what carries it is
        // the mistake to avoid.
        h.fatness  = 0.0f;
        h.delayMix = 0.0f;
        measure("drone dry", 0, 2.50f);
        measure("1 note dry", 1, 2.50f);
    }

    // ---- RELATION must do something, wherever COLOR is -------------------
    //
    // RELATION only detunes the six side voices, and Szabo's MIX curve puts
    // those 27 dB under the centre at COLOR 0. Faithful to the JP-8000, and the
    // reason COLOR 0 gives one clean saw — but it also means the largest knob
    // on the panel did nothing at all with COLOR closed. kCloudSideFloor lifts
    // the sides as the detune widens to fix that.
    //
    // Measured as *beat depth*: how much the short-term RMS wobbles across a
    // long window. Detuned voices beat against each other; a single voice is
    // steady. It is the one number that distinguishes "the stack is detuned"
    // from "the stack is loud".
    printf("\n== RELATION is audible at every COLOR ==\n");
    {
        auto beatDepth = [&](float color, float relation)
        {
            SynthParams p = mkParams(false, 16, 4);
            p.color       = color;
            p.relation    = relation;
            p.shape       = 0.5f; // saw
            gGatePatched  = false;
            allNotesOff();
            render(p, 80, nullptr, nullptr); // settle the smoothers

            SynthControlOutput co;
            float              lo = 1e30f, hi = 0.0f, mean = 0.0f;
            const int          kWindows = 256;
            for(int t = 0; t < kWindows; t++)
            {
                eng.control(p, slots, co);
                double sum = 0.0;
                for(int s = 0; s < 375; s++)
                {
                    int32_t l, r, dl, dr;
                    eng.audio(0, 0, 0.0f, false, &l, &r, &dl, &dr);
                    sum += (double)l * l;
                }
                const float rms = (float)sqrt(sum / 375.0);
                if(rms < lo)
                    lo = rms;
                if(rms > hi)
                    hi = rms;
                mean += rms;
            }
            mean /= (float)kWindows;
            return (mean < 1e-3f) ? 0.0f : (hi - lo) / mean;
        };

        const float colors[] = {0.0f, 0.15f, 0.5f, 1.0f};
        for(float c : colors)
        {
            const float flat  = beatDepth(c, 0.0f);
            const float wide  = beatDepth(c, 24.0f);
            const float swing = wide - flat;
            printf("  COLOR %.2f  beat depth %.2f -> %.2f  (swing %.2f)\n",
                   c,
                   flat,
                   wide,
                   swing);
            check(swing > 0.20f, "sweeping RELATION changes the sound");
        }
    }

    // ---- the cross-core invariant ---------------------------------------
    //
    // This is the one the hardware caught and the host build could not. The
    // render lists are read by audio() on Core 1 while control() rebuilds them
    // on Core 0, and the first version rebuilt them *in place* — starting by
    // zeroing the counts. Core 1 therefore saw an empty stack for part of every
    // control tick: constant crackle at ~50% CPU headroom with zero overruns.
    //
    // Sequential host calls can never reproduce that directly, but the
    // structural property is testable: whatever buffer audio() is reading, a
    // control tick must leave it completely untouched.
    printf("\n== control() must not touch the buffer audio() is reading ==\n");
    {
        bool violated = false;
        // Exercise the transitions that actually republish: notes arriving,
        // widths changing, notes leaving, the drone coming back.
        struct
        {
            int  slot;
            bool on;
        } script[] = {
            {0, true},
            {1, true},
            {2, true},
            {3, true},
            {3, false},
            {2, false},
            {1, false},
            {0, false},
        };
        gGatePatched = true;
        allNotesOff();
        static const float freqs[4] = {220.0f, 277.2f, 329.6f, 415.3f};

        for(auto &ev : script)
        {
            if(ev.on)
                noteOn(
                    (uint8_t)ev.slot, freqs[ev.slot], (uint8_t)(60 + ev.slot));
            else
            {
                slots[ev.slot].midiNote = kPolySlotFree;
                if(eng.polyEnvs[ev.slot])
                    eng.polyEnvs[ev.slot]->setGate(false);
            }
            // Several ticks each, so gates finish ramping and oscillators are
            // actually re-tasked rather than merely marked.
            for(int tick = 0; tick < 30; tick++)
            {
                SynthEngine::CloudPoolState before;
                eng.cloudPoolState(before);
                const uint8_t active = before.activeList;

                render(p1, 1, nullptr, nullptr);

                SynthEngine::CloudPoolState after;
                eng.cloudPoolState(after);
                for(int s = 0; s <= kCloudMaxNotes; s++)
                    if(before.listN[active][s] != after.listN[active][s])
                        violated = true;
            }
        }
        // And the same across the drone hand-off in both directions.
        for(int pass = 0; pass < 2; pass++)
        {
            gGatePatched = (pass != 0);
            if(gGatePatched)
                noteOn(0, 220.0f, 60);
            else
                allNotesOff();
            for(int tick = 0; tick < 60; tick++)
            {
                SynthEngine::CloudPoolState before;
                eng.cloudPoolState(before);
                const uint8_t active = before.activeList;
                render(p1, 1, nullptr, nullptr);
                SynthEngine::CloudPoolState after;
                eng.cloudPoolState(after);
                for(int s = 0; s <= kCloudMaxNotes; s++)
                    if(before.listN[active][s] != after.listN[active][s])
                        violated = true;
            }
        }
        check(!violated,
              "the buffer being rendered is never written by control()");
    }

    // ---- the envelope controls actually reach the stacks ----------------
    //
    // CLOUD became polyphonic at M78 by sounding the same per-slot envelopes
    // POLY uses, but the call that tunes them from CURVE sat inside the mode
    // switch's POLY case. So CLOUD gated those envelopes without ever setting
    // them: it ran on AREnvelope's default member initialisers — a fixed
    // ~21 ms attack, full sustain, ~21 ms release — and CURVE did nothing to
    // the mode at any setting. Because the array is shared with POLY, passing
    // through POLY first left CLOUD holding POLY's last CURVE, which made it
    // look like the knob half-worked.
    //
    // Measured rather than asserted structurally: what matters is that the
    // envelope the mode *renders with* follows the controls, and the number
    // below is read back through control()/audio() for that reason. A frozen
    // ~21 ms release is silent long before the window closes, so it reads 0
    // and every one of these checks fails.
    {
        printf("\n== Envelope controls reach POLY and CLOUD ==\n");
        const int kSettle = 64; // 0.5 s
        const int kWindow = 64; // 0.5 s of release to measure across

        struct
        {
            VoiceMode   mode;
            const char *name;
        } modes[] = {{VoiceMode::CLOUD, "CLOUD"}, {VoiceMode::POLY, "POLY"}};

        for(auto &m : modes)
        {
            // --- AR: CURVE sets attack and release together ---------------
            SynthParams ar  = mkParams(true, 12, 4);
            ar.voiceMode    = m.mode;
            ar.envelopeType = EnvelopeType::AR;

            ar.curve         = 0.45f; // shortest CURVE that still sustains
            const float arLo = releaseFraction(ar, kSettle, kWindow);
            ar.curve         = 0.95f;
            const float arHi = releaseFraction(ar, kSettle, kWindow);

            printf("  %-5s AR   curve 0.45 -> %.3f held, 0.95 -> %.3f held\n",
                   m.name,
                   arLo,
                   arHi);
            // A ~21 ms release is gone (0.000) after half a second, so any
            // value in this band proves CURVE was applied at all.
            check(arLo > 0.10f && arLo < 0.60f, "AR: CURVE sets the release");
            // And that it is the knob being followed, not one fixed value.
            check(arHi > arLo * 1.5f, "AR: a longer CURVE rings longer");

            // --- ADSR: RELEASE sets it, independently of CURVE ------------
            SynthParams ad  = mkParams(true, 12, 4);
            ad.voiceMode    = m.mode;
            ad.envelopeType = EnvelopeType::ADSR;
            ad.adsrAttack   = 0.01f;
            ad.adsrDecay    = 0.01f;
            ad.adsrSustain  = 1.0f;

            ad.adsrRelease   = 0.20f;
            const float adLo = releaseFraction(ad, kSettle, kWindow);
            ad.adsrRelease   = 2.00f;
            const float adHi = releaseFraction(ad, kSettle, kWindow);

            printf("  %-5s ADSR rel 0.2s -> %.3f held, 2.0s -> %.3f held\n",
                   m.name,
                   adLo,
                   adHi);
            // exp(-0.5/0.2) = 0.082 and exp(-0.5/2.0) = 0.779 — wide bands,
            // since what is under test is that the values arrive at all.
            check(adLo > 0.02f && adLo < 0.30f, "ADSR: RELEASE sets the tail");
            check(adHi > 0.55f, "ADSR: a long RELEASE holds the note up");
            check(adHi > adLo * 2.0f, "ADSR: the two settings differ");
        }

        // --- arriving from a mono mode with the knobs untouched -----------
        //
        // The per-slot retune is cached against the values that drive it, and
        // a mode change moves none of them: entering CLOUD from PAIR without
        // touching a knob has to recompute anyway, or the slots keep whatever
        // they last held. Same class of bug as the one above, one level down.
        {
            SynthParams mono = mkParams(true, 12, 4);
            mono.voiceMode   = VoiceMode::PAIR;
            mono.curve       = 0.95f;
            render(mono, 80, nullptr, nullptr); // sit in PAIR a while

            SynthParams intoCloud = mono;
            intoCloud.voiceMode   = VoiceMode::CLOUD;
            const float frac = releaseFraction(intoCloud, kSettle, kWindow);
            printf("  PAIR -> CLOUD, knobs untouched: %.3f held\n", frac);
            check(frac > 0.55f, "entering CLOUD tunes the slots from CURVE");
        }
    }

    // ---- re-pressing a note whose tail is still ringing ------------------
    //
    // polyRetrigger() used to zero the envelope and reset the oscillator phase
    // unconditionally. Against a silent slot that is right; against one still
    // ringing, each is a step discontinuity in the output, and the pair of
    // them is an audible click on every re-press of a note that has not
    // finished decaying.
    //
    // POLY could always reach it — its releases have always been long — and it
    // measured 24-31x the steady-state sample step. CLOUD reached it as soon
    // as its envelopes started following CURVE: a tail that was a fixed ~21 ms
    // is now seconds, so the re-press lands while it is still up. Both are
    // measured here against their own steady state, which is the only scale
    // that means anything for a click.
    {
        printf("\n== Re-pressing a ringing note does not step ==\n");
        struct
        {
            VoiceMode   mode;
            const char *name;
        } modes[] = {{VoiceMode::CLOUD, "CLOUD"}, {VoiceMode::POLY, "POLY"}};

        for(auto &m : modes)
        {
            SynthParams p = mkParams(true, 12, 4);
            p.voiceMode   = m.mode;
            p.curve       = 0.95f; // a tail long enough to still be up
            p.fatness     = 0.0f;  // isolate the main stacks

            gGatePatched = true;
            render(p, 4, nullptr, nullptr); // land the mode change
            allNotesOff();
            noteOn(0, 220.0f, 60);
            render(p, 200, nullptr, nullptr); // up to full

            float steady = 0.0f;
            render(p, 40, nullptr, &steady); // steady state, for scale

            slots[0].midiNote = kPolySlotFree;
            if(eng.polyEnvs[0])
                eng.polyEnvs[0]->setGate(false);
            render(p, 24, nullptr, nullptr); // ~190 ms in: tail still well up

            const float lvl = eng.polyEnvs[0] ? eng.polyEnvs[0]->level() : 0.0f;
            noteOn(0, 220.0f, 60); // re-press it
            float step = 0.0f;
            render(p, 2, nullptr, &step);

            const float ratio = steady > 0.0f ? step / steady : 999.0f;
            printf(
                "  %-5s tail at re-press %.3f, step %5.0f vs steady %5.0f"
                "  (%.1fx)\n",
                m.name,
                lvl,
                step,
                steady,
                ratio);
            // The tail has to actually still be up, or the check below passes
            // for the wrong reason — that is exactly how this went unnoticed
            // in CLOUD while its releases were frozen at ~21 ms.
            check(lvl > 0.5f, "the tail is still ringing at the re-press");
            check(ratio < 1.5f, "re-pressing it does not step");
        }
    }

    // ---- a new note starts with its stack, not after it ------------------
    //
    // Two ramps open a CLOUD note and they have to be the same ramp. The
    // envelope is one; the pool's per-oscillator fade gate is the other, and
    // at kCloudGateTicks it runs 375 ms whatever CURVE says. Claiming a new
    // note's saws through that fade put both under a note that only needed
    // one, and they do not compose: with a fast attack the envelope had peaked
    // and begun decaying while the gate was still opening, so the loudest
    // moment of the note landed a fifth of a second after its attack. Heard as
    // the note arriving soft with its attack following separately.
    //
    // A stack that is not sounding yet does not need the fade at all — its
    // envelope is at zero and is what brings it in — which is the exemption
    // the drone already had. The fade stays for what it was measured on: a saw
    // joining a stack that is already audible, checked above.
    {
        printf("\n== A new note starts with its stack, not after it ==\n");
        SynthParams p = mkParams(true, 12, 4);
        p.relation    = 0.0f;  // unison: no supersaw beat to swing the RMS
        p.fatness     = 0.0f;  // no sub: the stacks alone
        p.curve       = 0.10f; // fast attack, so a slow fade cannot hide in it

        gGatePatched = true;
        render(p, 4, nullptr, nullptr);
        allNotesOff();
        render(p, 20, nullptr, nullptr); // silence: gate patched, nothing held
        noteOn(0, 220.0f, 60);

        float rms[24], env[24];
        int   firstTick = -1;
        float firstGate = 0.0f;
        for(int t = 0; t < 24; t++)
        {
            render(p, 1, &rms[t], nullptr);
            env[t] = eng.polyEnvs[0] ? eng.polyEnvs[0]->level() : 0.0f;

            SynthEngine::CloudPoolState st;
            eng.cloudPoolState(st);
            int   n = 0;
            float g = 1.0f;
            for(int i = 0; i < kCloudOscMax; i++)
                if(st.owner[i] == 0u)
                {
                    n++;
                    if(st.gate[i] < g)
                        g = st.gate[i];
                }
            if(n > 0 && firstTick < 0)
            {
                firstTick = t;
                firstGate = g;
            }
        }
        printf("  stack arrives at tick %d, lowest gate on it %.3f\n",
               firstTick,
               firstGate);
        check(firstTick >= 0, "the note is given a stack");
        check(firstGate > 0.99f, "its saws open at full gate, not over 375 ms");

        int rPeak = 0, ePeak = 0;
        for(int t = 1; t < 24; t++)
        {
            if(rms[t] > rms[rPeak])
                rPeak = t;
            if(env[t] > env[ePeak])
                ePeak = t;
        }
        const int lag = rPeak > ePeak ? rPeak - ePeak : ePeak - rPeak;
        printf("  loudest tick %d, envelope peaks at tick %d (lag %d)\n",
               rPeak,
               ePeak,
               lag);
        check(lag <= 3, "the note is loudest on its attack, not long after");
    }

    printf("\n%s (%d failure%s)\n",
           failures ? "FAILURES" : "all checks passed",
           failures,
           failures == 1 ? "" : "s");
    return failures;
}
