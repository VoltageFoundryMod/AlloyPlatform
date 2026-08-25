#pragma once

#include <stdint.h>

#include "VoiceMode.h"
#include "dsp/ChorusEngine.h"
#include "dsp/CurveEngine.h"
#include "dsp/DattorroReverb.h"
#include "dsp/DelayEngine.h"
#include "dsp/DriftEngine.h"
#include "dsp/FilterEngine.h"
#include "dsp/FxChain.h"
#include "dsp/OTALadder.h"
#include "dsp/ReverbEngine.h"
#include "dsp/Saturate.h"
#include "dsp/SVFFilter.h"
#include "dsp/ShapeOsc.h"
#include "dsp/SpaceEngine.h"
#include "params.h" // PolySlot, EnvelopeType, FilterType, FilterMode, ChorusMode

// ---------------------------------------------------------------------------
// AlloyFlux's internal signal convention.
//
// int32 with ±kSignalFullScale as nominal full scale — chosen long before the
// platform existed and voiced by ear against it, so it stays.  It is a *module*
// convention, not a platform one: the platform's audio boundary is float ±1.0
// and Rack's is volts, and the conversion happens once, at the module's own
// edge, in a single multiply per sample.  Nothing outside this module should
// ever see a ±32512 integer.
// ---------------------------------------------------------------------------
static constexpr int32_t kSignalFullScale = 32512;
static constexpr float   kSignalToFloat   = 1.0f / (float)kSignalFullScale;
static constexpr float   kFloatToSignal   = (float)kSignalFullScale;

// ---------------------------------------------------------------------------
// FM IN — one jack, two bands (M77).
//
// The jack has to serve two jobs that want opposite treatment. Vibrato, sirens
// and envelope sweeps want *exponential* modulation of the fundamental, at
// control rate, because every voice mode spaces its voices from the fundamental
// by ratio and only a multiplier moves a chord without detuning it. Timbral FM
// wants *linear* deviation at audio rate, because that is what puts sidebands
// either side of the carrier instead of warbling the pitch.
//
// Rather than make that a mode the player has to choose, the jack is split by
// frequency and both halves run at once:
//
//        FM IN (volts, sampled every audio frame via setFmInSample)
//                            │
//              ┌─────────────┴─────────────┐
//        2-pole LP @ kFmInSplitHz      x − LP  (residual)
//              │                            │
//        exp2f(V · oct/V) at control rate   Q16 phase offset, per sample
//              │                            │
//        _sGlidedFreq — every mode          nextPM() on every main voice
//        inherits it by ratio               — same Hz deviation on each
//
// Below the split the behaviour is exactly what it always was; above it the
// jack is a true linear FM input. A DC offset therefore reads as pure pitch
// shift with no PM, which is the right answer for an offset.
//
// The low-pass is not only a splitter: it is the **anti-alias filter the pitch
// path never had**. control() decimates 48 kHz to 128 Hz, so before M77 any
// content above ~64 Hz folded straight into the pitch as inharmonic garbage.
// Two poles at kFmInSplitHz put ~-20 dB at the 64 Hz fold point and ~-40 dB an
// octave and a half up, which is what makes an audio-rate cable safe to patch.
//
// Both depths are scaled by SynthParams::fmAmount — the FM AMOUNT knob,
// SHIFT+ROOT on hardware. At 1.0 the pitch path is exactly the pre-M77 law.
//
// Applied in control(), after glide — NOT in fillSynthParams() where the jack
// is read. Upstream it would be erased twice over: a held MIDI note overwrites
// baseFreq wholesale on both platforms, and the VCV scale quantizer rounds it
// to the nearest semitone.
// ---------------------------------------------------------------------------

/// Octaves of pitch deviation per volt on the slow half of FM IN, at FM AMOUNT
/// = 1.0. 0.2 oct/V puts a ±5 V LFO at ±1 octave and the jack's own ±8 V range
/// at ±1.6. Deliberately *not* 1.0: at V/Oct scaling this would be a second
/// pitch input rather than a modulation one.
static constexpr float kFmInOctPerVolt = 0.2f;

/// Crossover between the two halves, Hz. Has to clear the fastest thing anyone
/// would call vibrato (~10 Hz) and still sit far enough under the 64 Hz fold
/// point for two poles to do real work. A fast envelope's attack transient does
/// cross it and arrives as a short burst of PM — which is a percussive FM ping,
/// not a defect.
static constexpr float kFmInSplitHz = 20.0f;

/// Peak phase deviation of the audio-rate half, radians, at FM AMOUNT = 1.0 and
/// the jack driven to its full ±8 V. Six radians is roughly index 6 — past the
/// point where the spectrum is more sideband than carrier, so the knob reaches
/// genuinely violent before it runs out.
static constexpr float kFmInMaxRad = 6.0f;

/// Full-scale swing of the FM IN input stage, volts. Mirrors CvRange::kFmMaxV
/// in platform/include/io/HardwareIO.h — this header is engine-level and does
/// not include the platform's IO map, so the number is restated rather than
/// reached for. The two must not drift; the jack's hotter ±8 V range is what
/// lets it take a modular-level audio signal without clipping the front end.
static constexpr float kFmInFullScaleV = 8.0f;

/// Volts on the jack -> Q16 phase-accumulator units, at FM AMOUNT = 1.0.
/// Same derivation as kFmMaxScale (the internal FM index) but denominated in
/// volts instead of in the ±32512 signal convention.
static constexpr float kFmInPmScale = kFmInMaxRad * (2048.0f * 65536.0f)
                                      / (2.0f * 3.14159265f * kFmInFullScaleV);

// ---------------------------------------------------------------------------
// CLOUD's oscillator pool (M78) — polyphony without giving up the drone.
//
// Before M78 CLOUD spent all seven oscillators on one supersaw stack at one
// pitch. Making it polyphonic cannot mean seven saws per note — four notes
// would be twenty-eight oscillators against a budget that measures out at
// roughly thirteen — so the stack has to narrow as notes are added.
//
// The trap is doing that the obvious way. A *fixed* width (say three saws a
// note) makes a single held note thinner than the drone, which is the one thing
// the mode must not lose. A *naive* dynamic width narrows notes that are
// already sounding, which is the same retroactive-gain mistake POLY's
// normalisation comment warns about, one level worse: it changes timbre, not
// just loudness, under a player's fingers.
//
// So: a pool, not a per-note allocation. Width is whatever divides:
//
//     pool 12   1 note → 7 saws   2 → 5   3 → 3   4 → 3
//
// One note keeps the full seven-saw stack — identical to the drone — and the
// stack narrows only when there is genuinely something to share with. Every
// level change an oscillator makes on the way in or out is ramped at control
// rate (kCloudLvlRamp), and no oscillator is ever re-tasked to a new note until
// it has actually reached silence, so neither the narrowing nor the reuse is
// audible as a step.
//
// Widths are odd on purpose. kCloudOffset's centre voice is index 3, and COLOR
// is a centre-against-sides balance — an even-width subset has no centre and
// COLOR stops meaning anything in the one mode where it is a headline control.
// ---------------------------------------------------------------------------

// kCloudOscMax, kCloudMaxNotes and kCloudPoolDefault are in VoiceMode.h — the
// console clamps against them and should not have to include this header.
// SynthParams::cloudPool / cloudMaxNotes trim them at runtime.

/// Pseudo-slot the drone stack is planned under, so drone and notes share one
/// allocator and one set of level ramps — which is what makes the crossfade
/// between them fall out for free instead of needing its own code path.
static constexpr uint8_t kCloudDroneSlot = kCloudMaxNotes;

/// Oscillator belongs to nothing and is free to claim.
static constexpr uint8_t kCloudNoOwner = 255;

// Two ramps, because the two things that change level here are not the same
// size and cannot afford the same treatment.
//
// A saw joining or leaving a *live* stack is a fraction of that stack, and a
// control-rate ramp on it measures clean. The whole drone stopping is the
// entire output, and the same control-rate ramp on that is plainly audible —
// measured at 7.7x the steady-state sample step on the host harness, a click,
// because a one-pole moving 20% of its remaining distance every 7.8 ms is a
// staircase when what it is scaling is the whole signal.
//
// So the drone gets a per-sample ramp and the individual saws keep the
// control-rate one. That costs one multiply-add a sample rather than one per
// oscillator, because the drone is a single stack and its gain folds into the
// same per-stack multiply its envelope would use.

/// Ticks a saw takes to fade fully in or out of a live stack. Linear, not a
/// one-pole, and that matters for more than smoothness: a one-pole only
/// approaches zero, so "has this oscillator gone quiet enough to re-task?"
/// would take ~28 ticks to answer yes. A linear ramp reaches zero exactly, on
/// the tick you can predict, which is what bounds how long a stack waits to
/// widen back out.
/// Long, because the gate only moves at control rate and its per-tick step is
/// therefore a discontinuity. Measured against the steady-state sample step,
/// narrowing a stack cost 10x at 6 ticks, 2.7x at 24 and 1.9x at 48.
///
/// The cost of the length is that a *third* note waits for its last saw, since
/// the first two have to fade theirs out before it can have them — up to this
/// many ticks of filling in. One note to two never waits: the pool has spares.
///
/// If a tick is ever audible when notes are added, the real fix is to advance
/// the gate per sample rather than per tick, the way _cloudDroneGain already
/// does. That costs a multiply per *ramping* oscillator per sample — nothing in
/// steady state, if the render list is split into ramping and settled runs.
static constexpr uint8_t kCloudGateTicks = 48; // ≈ 375 ms at 128 Hz
static constexpr float   kCloudGateStep  = 1.0f / (float)kCloudGateTicks;

/// Time constant for the drone's own fade, seconds. Long enough not to click,
/// short enough that the drone is out of the way by the time a note has
/// attacked.
static constexpr float kCloudDroneFadeS = 0.04f;

/// Below this a gate counts as closed and its oscillator may be re-tasked.
static constexpr float kCloudSilent = 0.0008f;

/// Above this a poly slot counts as still sounding when a note re-presses it,
/// and polyRetrigger() rises out of the tail instead of zeroing it. Matches the
/// level at which updateControl() reaps a releasing slot, so a slot the
/// allocator considers finished is one this considers silent.
static constexpr float kPolyRetrigSilent = 0.001f;

/// Where the note-tracking high-pass sits, as a fraction of the played note.
///
/// It used to sit *on* the note, which is the JP-8000's own arrangement and
/// keeps a seven-saw stack from going muddy in the low register. The catch is
/// that a one-pole high-pass at the fundamental takes 3 dB off the fundamental
/// itself — and on a sawtooth that is the largest partial there is. Measured,
/// it was costing CLOUD **2.3 dB of RMS**, which is most of why the mode read
/// quieter than every other one on a scope while peaking in exactly the same
/// place.
///
/// Half the note frequency keeps the tracking and still thins everything below
/// the fundamental, where the stack actually piles up, but only takes ~1 dB off
/// the fundamental instead of 3.
///
/// Worth +1.4 dB of raw RMS with the peak unchanged — but that figure flatters
/// it, and the honest one is smaller. Lower crest means *more* samples sit near
/// the peak, so more of them cross the saturator's knee: drone distortion went
/// 0.36% → 1.72% at an unchanged level. Held at **equal distortion**, which is
/// the only fair comparison, the recovered fundamental is worth about
/// **+0.85 dB** (RMS 8860 at 0.36% before, 9767 at 0.43% after). Real, free,
/// and a third of what the bypass measurement suggested.
///
/// ⚠ This is a deliberate departure from the reference, which tracks on the
/// note. Raise it back to 1.0 for the authentic voicing if the low register
/// ever sounds thick.
static constexpr float kCloudHpTrack = 0.5f;

/// Floor under the side voices that RELATION lifts, at COLOR fully closed.
///
/// Szabo's MIX curve puts the six side voices **27 dB** under the centre at
/// COLOR 0 — faithful to the JP-8000, and the reason COLOR 0 gives one clean
/// saw. The side effect is that RELATION, which only detunes those six, does
/// nothing at all there. On a panel where RELATION is the largest knob, a
/// player who has not yet found COLOR sweeps the biggest control on the module
/// and hears silence from it. That is a bad first ten minutes.
///
/// So the side level gets a floor that RELATION raises. Two things keep it from
/// being a hack:
///
///  - It is driven by the *detune amount* — the output of _cloudDetuneCurve,
///    not the raw knob. The voices come up exactly as they spread apart, so it
///    reads as detune becoming audible rather than as a volume change. Tying it
///    to the knob instead would lift their level through the curve's flat first
///    third, where there is no detune yet to hear.
///  - It fades out as COLOR opens ((1 - mix)), so anywhere above the bottom of
///    COLOR's travel the fitted curves govern exactly as before.
///
/// COLOR 0 with RELATION 0 is therefore still one clean saw, unchanged. COLOR 0
/// with RELATION up is a subtly chorused one — which is what sweeping that knob
/// ought to do. 0.22 lands the sides ~13 dB under the centre at full RELATION:
/// clearly audible, still obviously centre-dominant.
static constexpr float kCloudSideFloor = 0.26f;

/// Level of one CLOUD stack — drone and every held note alike.
///
/// **Fixed, and deliberately not divided by the polyphony.** The obvious law is
/// 1/sqrt(maxNotes), so that the maximum chord lands exactly at full scale. It
/// is also wrong, and audibly so: peak grows as sqrt(N) under it, which means
/// designing for four notes makes *one* note a quarter of the level, and the
/// common case — one or two notes — comes out weak. That is the complaint that
/// prompted this.
///
/// Roland's polysynths do not do that. A JUNO or a JP-8000 runs each voice at
/// its own fixed level, lets the sum grow with the chord, and lets the output
/// stage round off what comes out the top. A chord being louder than one note
/// is correct — it is what a chord sounds like — and the peaks are handled at
/// the end rather than pre-empted at every voice.
///
/// So this is a per-stack level, the same for one note as for four, and
/// softSaturate() catches the sum.
///
/// **Above unity, and it has to be**, because CLOUD's problem is crest factor
/// rather than gain. Seven near-coincident detuned voices spend most of their
/// time near zero and spike when they briefly align, where PAIR's two sit close
/// to their peak most of the cycle. Measured on the same patch, every mode
/// peaks at 85–87% of full scale — but CLOUD's *RMS* came out 8.5 dB under
/// PAIR's, and RMS is what a listener calls loudness (and what a scope shows as
/// the dense band: ~±1.7 V against ±4.4 V on the jacks).
///
/// The quadrature normalisation is not wrong — holding summed power constant is
/// the right law for adding decorrelated voices — it just says nothing about
/// loudness once the crest factor changes underneath it. So the level is set
/// above unity and softSaturate() rounds the peaks, which is exactly what every
/// hardware supersaw does and the reason the saturator exists at all.
///
/// Chosen against **measured distortion**, not against the fraction of samples
/// the curve touches. Those are not the same thing and the difference matters:
/// a sample just past the knee is barely altered, because the slope there is 1.
/// The number below is error energy against a bit-identical unsaturated render
/// of the same stream, which is the only honest measure. SHAPE at saw:
///
///     level   vs PAIR   drone    1 note   2 notes   4 notes
///     0.75     -8.5 dB   0.000%   0.07%     1.56%     4.52%
///     0.90     -6.9 dB   0.364%   1.44%     2.95%     5.96%
///     1.05     -5.6 dB   1.452%   3.00%     4.24%     7.21%
///     1.25     -4.3 dB   3.115%   4.46%     5.39%     8.33%
///
/// **The gap to the mono modes is crest factor, exactly.** Measured on the same
/// patch, CLOUD peaks at 87% of full scale and so does PAIR — identical. What
/// differs is the crest: 2.93 against 1.56, because seven near-coincident
/// voices spend most of the cycle near zero and spike only when they briefly
/// align. That ratio predicts -5.5 dB and -5.4 dB is what was measured, so
/// there is nothing else going on and nothing being held in reserve. In
/// particular the drone is *not* attenuated to leave room for notes: it already
/// sits at the peak ceiling, which is why boosting it in drone mode alone buys
/// nothing that saturation does not also cost.
///
/// So the only lever is the saturator, and the exchange rate is:
///
///     level   vs PAIR   drone distortion
///     0.90     -5.4 dB   0.43%
///     1.10     -3.8 dB   2.10%
///     1.30     -2.5 dB   3.49%
///
/// 1.10 lands CLOUD on CHORD's level (-3.8) and near STRING's (-3.5), which is
/// the target: in family with the other ensemble modes rather than the odd one
/// out on a scope.
///
/// This reverses an earlier, more conservative reading of the same numbers, and
/// the reason is what the distortion lands *on*. The always-on Padé clip that
/// this module removed twice put its 6–7% on clean single-oscillator sines and
/// on reverb tails, where added harmonics have nothing to hide behind. Two
/// percent on seven detuned sawtooths — already dense, bright and beating — is
/// a different proposition, and masking is the whole difference. Judging a
/// distortion figure without asking what signal carries it was the mistake.
static constexpr float kCloudStackLevel = 1.10f;

// ---------------------------------------------------------------------------
// SynthParams — snapshot of all goal parameters for one control cycle.
//
// The hardware shim in main.cpp populates this struct from the gXxx globals
// each updateControl() tick and passes it to SynthEngine::control().
// The future VCV Rack module will populate it directly from widget state.
// ---------------------------------------------------------------------------
struct SynthParams
{
    float      baseFreq     = 440.0f;
    float      shape        = 0.0f;
    float      fatness      = 0.4f;
    uint8_t    subOctave    = 1; // 1 = one octave below, 2 = two octaves below
    float      motion       = 0.0f;
    float      driftSpeed   = 0.04f;
    VoiceMode  voiceMode    = VoiceMode::PAIR;
    float      relation     = 0.0f;
    float      curve        = 0.5f;
    float      curveTime    = 1.0f;
    bool       gateHigh     = false;
    bool       gatePatched  = false;
    float      volume       = 1.0f;
    float      midiVelocity = 1.0f;
    float      glideTime    = 0.0f;
    bool       glideEnabled = false;
    ChorusMode chorusMode   = ChorusMode::I_II;
    float      space        = 1.0f;
    float      color        = 0.0f;
    // FM IN jack, volts. Only a fallback path reads this — a platform that
    // calls setFmInSample() every audio frame (both of them do) supplies the
    // jack at audio rate instead and control() ignores this field. Kept so a
    // host that renders control ticks without an audio loop — a test harness,
    // a headless parameter sweep — still gets the slow half of the jack.
    float fmIn = 0.0f;
    // FM AMOUNT, 0–1: scales *both* halves of FM IN. 1.0 is full depth on each
    // — kFmInOctPerVolt on the pitch path, kFmInMaxRad on the PM path.
    // SHIFT+ROOT on hardware; context-menu slider in VCV.
    float fmAmount = 1.0f;
    // CLOUD's oscillator pool (M78) — how many supersaw oscillators the mode
    // may sound at once, and across how many held notes. Width per note falls
    // out of the two; see _cloudWidthFor(). Runtime rather than compile-time so
    // the ceiling can be walked up against `slow-blk` on real hardware instead
    // of guessed at — `cloud pool` / `cloud notes` on the console.
    uint8_t      cloudPool     = kCloudPoolDefault;
    uint8_t      cloudMaxNotes = kCloudMaxNotes;
    EnvelopeType envelopeType  = EnvelopeType::AR;
    float        adsrAttack    = 0.05f;
    float        adsrDecay     = 0.10f;
    float        adsrSustain   = 0.8f;
    float        adsrRelease   = 0.30f;
    bool         adsrLoop      = false;
    FilterType   filterType    = FilterType::SVF;
    float        filterCutoff  = 983.2f; // kDefaultFilterCutoff
    float        filterRes     = 0.0f;
    FilterMode   filterMode    = FilterMode::OFF;
    FxOrder      fxOrder       = {false, false};
    float        revMix        = 0.35f;
    bool         revEnabled    = false;
    float        revSize       = 0.5f;
    float        revDamping    = 0.5f;
    float        revModSpeed   = 1.0f;
    float        revModDepth   = 1.0f;
    bool         revFrozen     = false;
    float        delayTime     = 100.0f;
    float        delayFeedback = 0.5f;
    float        delayMix      = 0.0f;
};

// Snapshot of the smoothed DSP params published to the Core 1 / VCV side
// after each control tick (replaces the gDsp mutex block in main.cpp).
struct SynthControlOutput
{
    float freq1   = 440.0f;
    float freq2   = 440.0f;
    float shape   = 0.0f;
    float fatness = 0.4f;
    float motion  = 0.0f;
    float curve   = 0.5f;
    float volume  = 0.8f;
};

// ---------------------------------------------------------------------------
// SynthEngine — owns all synthesis DSP state.
//
// Lifecycle:
//   1. Create an instance — the platform owns it, there is no singleton.
//   2. Call init(audioRate, controlRate) once at startup.
//   3. Each control tick: control(params, polySlots, output)
//   4. Each audio sample: audio(...)
//
// Audio signal range is kept as int32_t ±32512 throughout; float conversion
// happens only at the output boundary.
//
// Call setSampleRate() whenever the host or driver rate changes.
// ---------------------------------------------------------------------------
class SynthEngine
{
  public:
    // -----------------------------------------------------------------------
    // Constructor — pre-wires the reverb pointer so the object is usable
    // before init() runs.  _dattorroReverb is a concrete member, so its
    // address is fixed at object-construction time.
    // -----------------------------------------------------------------------
    SynthEngine();

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /**
     * One-time init — generate wavetables, seed all engines.
     * Must be called before the audio driver starts so delay buffers are warm.
     * @param audioRate   samples/sec (firmware: kAudioRate = 48000)
     * @param controlRate control ticks/sec (firmware: kControlRate = 128)
     */
    void init(uint32_t audioRate = 48000u, uint32_t controlRate = 128u);

    /**
     * Control-rate update (128 Hz on hardware).
     * Runs one-pole smoothing, voice frequency computation, and effect
     * coefficient updates.  Must NOT be called from the audio ISR.
     * @param p          goal-value snapshot for this tick
     * @param polySlots  6-element POLY voice allocation array (written by MIDI)
     * @param out        filled with smoothed params for Core 1 bookkeeping
     */
    void control(const SynthParams  &p,
                 PolySlot            polySlots[6],
                 SynthControlOutput &out);

    /**
     * Audio-rate update: one stereo sample.
     *
     * revWetL/R  — Core 1's reverb-wet return from the previous frame
     *              (±32512 int32; pass 0 when reverb is disabled).
     * revMix     — wet-return gain, added on top of an unattenuated dry
     *              (0.0 = dry only, 1.0 = dry + full wet). This is a send
     *              level, not a dry/wet crossfade — the dry path never
     *              drops. AlloyCoil's same-named control *does* crossfade;
     *              the two are deliberately different and labelled apart
     *              ("Wet" here, "Dry/Wet" there).
     * revEnabled — when false the reverb mix path is skipped entirely.
     * finalL/R   — fully-processed stereo output (±32512 int32).
     * dryForRevL/R — pre-reverb signal for Core 1 to process next frame.
     */
    void audio(int32_t  revWetL,
               int32_t  revWetR,
               float    revMix,
               bool     revEnabled,
               int32_t *finalL,
               int32_t *finalR,
               int32_t *dryForRevL,
               int32_t *dryForRevR);

    /**
     * (M77) Audio-rate FM IN feed — call once per audio frame, before audio().
     *
     * `volts` is the raw jack voltage. This is the *only* thing on the module
     * sampled at audio rate rather than at 128 Hz, and it is why FM IN sits on
     * GP27, a direct ADC pin deliberately kept off the analogue mux.
     *
     * Splits the signal into the two bands described at kFmInSplitHz: the slow
     * half is published for control() to turn into a pitch multiplier, the fast
     * half becomes the Q16 phase offset audio() adds to every main voice.
     *
     * Inline and float-light on purpose — two multiply-adds and one conversion,
     * inside the same time-critical section as renderAudio().
     *
     * Cross-core, on hardware: this runs on Core 1 and control() reads
     * _fmInSlowV from Core 0. Both published values are volatile for that
     * reason, and each is a single word — a torn read is not possible and a
     * one-tick-stale one is inaudible on a 20 Hz band.
     *
     * A platform that never calls this loses nothing but the audio-rate half;
     * control() falls back to SynthParams::fmIn and behaves exactly as it did
     * before M77.
     */
    inline void setFmInSample(float volts)
    {
        _fmInFed      = true;
        const float a = _fmInLpA;
        _fmInLp1 += (volts - _fmInLp1) * a;
        _fmInLp2 += (_fmInLp1 - _fmInLp2) * a;

        // Decimate by *averaging*, not by sampling. The two poles above are a
        // 12 dB/octave slope, and just past the control rate's 64 Hz Nyquist
        // that is not yet enough — measured on the host harness, a full-scale
        // 110 Hz modulator still folded ~77 cents of warble into the pitch.
        //
        // A box average over exactly one control period is the missing piece:
        // its nulls land on multiples of the control rate, it adds another
        // ~16 dB where the poles are weakest, and — the reason it is worth an
        // accumulator — it is flat to within 0.01% across the vibrato band, so
        // unlike a third pole it costs the slow half nothing.
        //
        // Published on this side's own count rather than on the control tick,
        // so the two clocks need not agree and no accumulator is shared across
        // cores. Drifting phase between them is meaningless on a 20 Hz band.
        _fmInAcc += _fmInLp2;
        if(++_fmInAccN >= _fmInAccLen)
        {
            _fmInSlowV = _fmInAcc * _fmInAccRecip;
            _fmInAcc   = 0.0f;
            _fmInAccN  = 0;
        }

        // Residual, not a separate high-pass: x − LP costs nothing and is
        // exactly complementary, so the two halves always sum back to the jack.
        // Taken from the un-averaged LP — the averaging exists to protect the
        // decimated path, and subtracting it here would put the box filter's
        // own comb into the audio-rate half.
        _fmInPm = (int32_t)((volts - _fmInLp2) * _fmInPmScale);
    }

    /**
     * (M37c) Runtime sample-rate change — re-initialises all rate-dependent
     * engines without clearing note/sequence state.
     */
    void setSampleRate(uint32_t audioRate);

    /**
     * Snapshot of CLOUD's oscillator pool as control() last left it.
     *
     * The pool's correctness is not something the audio output shows you: an
     * oscillator handed to two stacks at once, or a stack left one saw short,
     * sounds like a slightly different supersaw rather than like a bug. So the
     * plan is readable, and the host harness asserts on it directly.
     *
     * Also what the `cloud` console command reports, which is the reason it is
     * a real accessor rather than test scaffolding: the derived width is the
     * number that tells a player what the pool setting actually bought them.
     */
    struct CloudPoolState
    {
        uint8_t pool    = 0; ///< oscillators in play
        uint8_t width   = 0; ///< saws per stack
        uint8_t notes   = 0; ///< configured max held notes
        bool    droning = true;
        /// Per oscillator: owning slot, kCloudDroneSlot, or kCloudNoOwner.
        uint8_t owner[kCloudOscMax] = {};
        /// Per oscillator: index into kCloudOffset.
        uint8_t offset[kCloudOscMax] = {};
        /// Per oscillator: fade gate, 0–1. 1 is fully in the stack, 0 is free.
        float gate[kCloudOscMax] = {};
        /// Which render buffer audio() is reading, and the saw count each
        /// stack has in each buffer. Exposed so the harness can assert the
        /// property the double-buffering exists for: control() must never
        /// write the buffer audio() is currently reading.
        uint8_t activeList                   = 0;
        uint8_t listN[2][kCloudMaxNotes + 1] = {};
    };
    void cloudPoolState(CloudPoolState &s) const;

    /**
     * Noise-free poly note retrigger — call from the MIDI note-on callback.
     *
     * Three separate click sources are eliminated in one atomic step:
     *  1. Old envelope level: reset to 0 before arming attack — prevents the
     *     abrupt step that occurs when a releasing voice is stolen.
     *  2. Oscillator phase: reset to 0 so the new note starts from a known
     *     waveform position instead of an arbitrary mid-cycle offset.
     *  3. Sub-oscillator phase: same reset for the sub-voice.
     *
     * The new frequency is applied immediately (not deferred to the next
     * control tick), so the first audible samples of the attack are at the
     * correct pitch even when the voice was playing a different note.
     *
     * @param slot      0–5 poly voice slot index
     * @param freq      new voice frequency in Hz
     * @param subMult   sub-oscillator frequency multiplier (0.5 or 0.25)
     */
    void polyRetrigger(uint8_t slot, float freq, float subMult);

    // -----------------------------------------------------------------------
    // Public pointers — aliased by hardware-side globals so commands.cpp and
    // usb_midi.cpp continue to work without changes.  Set in init().
    // -----------------------------------------------------------------------
    FilterEngine   *filterInst  = nullptr; // -> _svfFilter or _otaLadder
    EnvelopeEngine *curveEng    = nullptr; // -> _arEnv or _adsrEnv
    EnvelopeEngine *polyEnvs[6] = {};      // -> _polyEnvArr[0..5]
    ReverbEngine   *reverb = nullptr; // pre-set by constructor → always valid

    // Volatile members written by control(), read by audio() ISR — same
    // semantics as the volatile globals they replace in main.cpp.
    volatile float chorusDepth = 0.0f;

  private:
    uint32_t _audioRate   = 48000u;
    uint32_t _controlRate = 128u;

    // -----------------------------------------------------------------------
    // Wavetables — 5 shapes × 2048 samples × int16_t = 20 KB total.
    // Generated at init() time; all 8 oscillators share these arrays.
    // -----------------------------------------------------------------------
    static constexpr uint16_t TABLE_CELLS = 2048u;
    int16_t                   _sineTable[TABLE_CELLS];
    int16_t                   _triTable[TABLE_CELLS];
    int16_t                   _sawTable[TABLE_CELLS];
    int16_t                   _squareTable[TABLE_CELLS];
    int16_t                   _narrowPulseTable[TABLE_CELLS];

    void        _generateWavetables();
    static void _normaliseTable(const float *buf, int16_t *dst, int n);

    /// Recompute the FM IN crossover coefficient for the current _audioRate.
    /// Called from init() and setSampleRate() — nowhere else needs it.
    void _updateFmInSplit();

    // -----------------------------------------------------------------------
    // Oscillators — kCloudOscMax main voices + the same number of subs.
    //
    // Every mode but CLOUD uses at most the first six or seven: the JP-8000
    // supersaw is a seven-saw stack and the detune offsets below are fitted to
    // that many, POLY takes six, the rest fewer. The array is sized for CLOUD's
    // pool (M78), which spreads stacks of those seven offsets across up to
    // kCloudMaxNotes held notes and so needs more oscillators than any single
    // stack does. Only SynthParams::cloudPool of them ever sound at once.
    //
    // The sub array is the same length purely so the two stay index-parallel;
    // CLOUD sounds exactly one sub and the other modes one per voice.
    // -----------------------------------------------------------------------
    ShapeOsc<48000u> _voices[kCloudOscMax];
    ShapeOsc<48000u> _subVoices[kCloudOscMax];

    // -----------------------------------------------------------------------
    // Envelopes
    // -----------------------------------------------------------------------
    AREnvelope<48000u>   _arEnv;
    ADSREnvelope<48000u> _adsrEnv;
    EnvelopeType         _envType     = EnvelopeType::AR;
    EnvelopeType         _prevEnvType = EnvelopeType::AR;

    // POLY and CLOUD — 6 independent per-slot envelopes, in both flavours.
    //
    // Two arrays rather than one array of pointers because audio() calls
    // next() on these six times a sample: a virtual call there is 288k
    // indirect branches a second for something that changes at most when the
    // user picks a different envelope type. _polyEnvIsAdsr selects between
    // them, is read once per frame, and both arms stay direct calls.
    //
    // Only the selected array is ever gated or advanced; the other sits idle
    // at zero. Switching type resets both, so whichever arm audio() picks
    // across the flip is silent either way.
    AREnvelope<48000u>   _polyEnvArr[6];
    ADSREnvelope<48000u> _polyAdsrArr[6];

    /// Which of the two poly arrays is live. Volatile for the same cross-core
    /// reason as _cloudListIdx: control() on Core 0 writes it, audio() on
    /// Core 1 reads it.
    volatile bool _polyEnvIsAdsr = false;

    // -----------------------------------------------------------------------
    // Envelope coefficient cache.
    //
    // setCurve() is two expf() calls and setADSR() is three, at ~90 µs each on
    // the RP2350 — the same cost the exp2f guard in control() exists for. The
    // previous code called setCurve() unguarded on all six poly slots every
    // tick (12 expf ≈ 1.1 ms of a 7.8 ms control period, for coefficients that
    // were identical across the six and usually unchanged from last tick).
    // Now one envelope is computed when a driving value actually moves and the
    // rest copy it. Same idiom as _cachedRelPlasma and friends.
    // -----------------------------------------------------------------------
    float _envCacheCurve = -1.0f; // _sCurve at last recompute
    float _envCacheTime  = -1.0f; // _sCurveTime at last recompute
    float _envCacheA = -1.0f, _envCacheD = -1.0f;
    float _envCacheS = -1.0f, _envCacheR = -1.0f;
    bool  _envCacheLoop = false;
    /// Whether the last recompute targeted the per-slot envelopes. Part of the
    /// cache key: entering CLOUD/POLY with the knobs untouched must still tune
    /// the slots. See _updateEnvelopes().
    bool _envCachePerSlot = false;
    /// ADSR's CURVE-driven time scale, powf-derived — cached against _sCurve.
    float _envTScale      = 1.0f;
    float _envCacheTScale = -1.0f;

    /// Retune the mono and (when the mode uses them) per-slot envelopes from
    /// the current params. One guard, one recompute, applied to everything
    /// live — see the note on the cache above, and _updateEnvelopes() in the
    /// .cpp for why it is not per-mode.
    void _updateEnvelopes(const SynthParams &p);

    /// Current level of poly slot `s`, whichever flavour is selected.
    float _polySlotLevel(uint8_t s) const
    {
        return _polyEnvIsAdsr ? _polyAdsrArr[s].level() : _polyEnvArr[s].level();
    }

    // -----------------------------------------------------------------------
    // Effect engines
    // -----------------------------------------------------------------------
    DriftEngine<kCloudOscMax> _drift;
    ChorusEngine<48000u>      _chorus;

    SVFFilter  _svfFilter;
    OTALadder  _otaLadder;
    FilterType _filterType     = FilterType::SVF;
    FilterType _prevFilterType = FilterType::SVF;

    DattorroReverb _dattorroReverb;

    DelayEngine _delay;

    // -----------------------------------------------------------------------
    // Smoothed control-rate state (one-pole LPF outputs)
    // -----------------------------------------------------------------------
    float _sShape     = 0.0f;
    float _sFatness   = 0.4f;
    float _sMotion    = 0.0f;
    float _sCurve     = 0.5f;
    float _sCurveTime = 1.0f;
    float _sVolume    = 0.8f;
    float _sMidiVel   = 1.0f;
    float _sSpace     = 1.0f;
    float _sRelation  = 0.0f;
    float _sColor     = 0.0f;

    // Derived control-rate values read by audio() ISR.
    float          _sSubWf = 0.2f; // fatness -> sub weight (0..0.5)
    volatile float _sFmDepth
        = 0.0f; // FM phase scale (volatile: control writes, ISR reads)

    // Portamento smoother state — the played pitch, glided, with no modulation
    // on it. Kept separate from _sGlidedFreq so FM IN cannot leak back into the
    // filter and drag the portamento off the note.
    float _sGlideBase = 440.0f;

    // The pitch every voice mode actually builds from: _sGlideBase with FM IN
    // applied. Equal to _sGlideBase whenever the jack is unpatched.
    float _sGlidedFreq = 440.0f;

    // FM IN pitch multiplier for this control tick — exp2f(slow volts * oct/V
    // * amount), or exactly 1.0 when the jack is unpatched. Held as a member so
    // POLY can reach it: it takes its pitch from the allocator and so never
    // passes through _sGlidedFreq, where every other mode picks the modulation
    // up.
    float _fmInMul = 1.0f;

    // -----------------------------------------------------------------------
    // FM IN two-band split (M77) — see the block above kFmInOctPerVolt.
    //
    // The filter state belongs to setFmInSample() and is touched by nothing
    // else; only the two published values cross to Core 0 / to audio().
    // -----------------------------------------------------------------------
    float _fmInLpA = 0.0f; // one-pole coeff for kFmInSplitHz at _audioRate
    float _fmInLp1 = 0.0f; // cascade stage 1
    float _fmInLp2 = 0.0f; // cascade stage 2 — the slow half, volts

    // Anti-alias box average over one control period — see setFmInSample().
    float    _fmInAcc      = 0.0f;
    uint32_t _fmInAccN     = 0;
    uint32_t _fmInAccLen   = 1;    // _audioRate / _controlRate, floor 1
    float    _fmInAccRecip = 1.0f; // 1 / _fmInAccLen

    /// Slow half, published for control(). Volts.
    volatile float _fmInSlowV = 0.0f;
    /// Fast half, published for audio(). Q16 phase-accumulator units, already
    /// scaled by FM AMOUNT.
    volatile int32_t _fmInPm = 0;
    /// Volts -> Q16 for the line above: kFmInPmScale × FM AMOUNT. Written at
    /// control rate, read per sample.
    volatile float _fmInPmScale = 0.0f;
    /// Latched by the first setFmInSample(). Once a platform feeds the jack at
    /// audio rate it does so every frame, so this never needs clearing — it
    /// only decides which source control() believes, and a platform does not
    /// change its mind mid-run.
    volatile bool _fmInFed = false;
    /// Smoothed FM AMOUNT, 0–1.
    float _sFmAmount = 1.0f;

    // Filter smoothing.
    float _sFilterCutoff = 983.2f;
    float _sFilterRes    = 0.0f;

    // Reverb parameter change-detection state.
    float _prevRevSize     = -1.0f;
    float _prevRevDamping  = -1.0f;
    float _prevRevModSpeed = -1.0f;
    float _prevRevModDepth = -1.0f;
    bool  _prevRevFrozen   = false;

    // -----------------------------------------------------------------------
    // Audio-ISR state
    // -----------------------------------------------------------------------
    float _sGainSmooth = 1.0f; // de-click gain smoother (only in audio())

    // Stereo pan weights (set by control(), read by audio()).
    uint8_t _activeVoices = 2u;
    /// FM carrier/modulator pairs the audio path renders: 1 in PAIR, 2 in
    /// CASCADE, where the second pair is what makes the mode stereo at all.
    /// Read only when the mode is an FM one; other modes leave it alone.
    uint8_t _fmPairs            = 1u;
    int16_t _panL[kCloudOscMax] = {256};
    int16_t _panR[kCloudOscMax] = {0, 256};

    // Gate edge detection (control()).
    bool _prevGate = false;

    // Current voice mode — written by control(), read by audio().
    VoiceMode _voiceMode     = VoiceMode::PAIR;
    VoiceMode _prevVoiceMode = VoiceMode::PAIR;

    // Chorus mode, written by control(), read by audio().
    ChorusMode _chorusMode = ChorusMode::I_II;

    // Fx ordering, written by control(), read by audio().
    FxOrder _fxOrder = {false, false};

    // -----------------------------------------------------------------------
    // Per-mode frequency caches (avoids repeated powf() calls)
    // -----------------------------------------------------------------------
    // PAIR
    float _cachedRelPair   = -1.0f;
    float _cachedRatioPair = 1.0f;

    // CHORD
    int   _cachedChordIdx      = -1;
    int   _cachedVoicingChord  = -1;
    float _cachedChordBase     = -1.0f;
    float _cachedFreqsChord[4] = {440.0f, 440.0f, 440.0f, 440.0f};

    // CLOUD — supersaw. Unlike the other ensemble modes nothing here needs a
    // powf(): detune is a *proportional* offset, so the per-voice value is a
    // plain multiplier against the root and the cache holds the multipliers
    // rather than absolute frequencies. RELATION moving costs seven multiplies.
    float _cachedRelCloud    = -99.0f;
    float _cloudDetuneMul[7] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    // COLOR's centre/side balance, before normalisation — the normalisation
    // itself depends on the stack width and so is applied per slot, not here.
    //
    // Cached against RELATION as well as COLOR since M78b: the side level has a
    // floor that the detune amount lifts (kCloudSideFloor), so moving either
    // knob invalidates it.
    float _cachedColorCloud = -99.0f;
    float _cachedRelMix     = -99.0f;
    float _cloudCentreLvl   = 1.0f;
    float _cloudSideLvl     = 0.0f;
    /// Szabo's detune curve output, 0–1. Cached with the detune multipliers and
    /// read by the MIX block below, which is why it is a member rather than a
    /// local.
    float _cloudDetuneAmt = 0.0f;

    // STRING
    float _cachedRelStr      = -99.0f;
    float _cachedBaseStr     = -1.0f;
    float _cachedFreqsStr[4] = {440.0f, 440.0f, 440.0f, 440.0f};

    // POLY — the other modes cache absolute frequencies, but every poly slot
    // carries its own note, so what is cached here is the per-slot detune
    // *ratio*. It depends only on RELATION, which makes the cache cheaper than
    // the others: six powf() calls when the knob moves, one multiply per voice
    // per tick otherwise.
    float _cachedRelPoly    = -99.0f;
    float _polyDetuneMul[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    // -----------------------------------------------------------------------
    // CLOUD — supersaw
    //
    // A JP-8000-style seven-oscillator stack, after Adam Szabo's 2010
    // reverse-engineering ("How to Emulate the Super Saw"). Three parts matter
    // and all three are here: the *irregular* detune offsets, the non-linear
    // curve RELATION drives them through, and the centre/side level balance
    // COLOR sets. An evenly-spaced spread with a linear knob does not sound
    // like this, which is why the odd-looking constants are kept exactly.
    //
    // SHAPE is deliberately left live rather than forced to saw. The offsets
    // and the mix law are about how the stack is tuned and balanced, not about
    // what waveform it is made of, so they hold up across the whole morph —
    // a super-pulse or super-triangle is the same trick on another wave.
    // -----------------------------------------------------------------------

    /** Relative detune offsets, as a fraction of the detune amount. */
    static constexpr float kCloudOffset[7] = {-0.11002313f,
                                              -0.06288439f,
                                              -0.01952356f,
                                              0.00000000f, // centre voice
                                              0.01991221f,
                                              0.06216538f,
                                              0.10745242f};

    /**
     * Pan weight scale. Sized so CLOUD's summed level matches the ensemble
     * modes it replaces (their per-voice 128 over four voices) once the
     * decorrelated voices are added in quadrature — see the `norm` term in the
     * CLOUD case of control(). Held a little under the exact match for
     * headroom: the supersaw is peakier than its RMS suggests.
     */
    static constexpr float kCloudPanScale = 240.0f;

    /** Detune curve: RELATION 0–1 → detune fraction. Szabo's fitted 11th-order
     *  polynomial. Control rate only, so the order costs nothing. */
    static float _cloudDetuneCurve(float x);

    /**
     * SHAPE for one voice of an ensemble spread — COLOR's job in STRING and
     * POLY, where it used to add a second detune on top of RELATION's.
     *
     * Two knobs that both spread pitch are one knob with a vernier: at A440
     * RELATION's ±15 cents is ±3.8 Hz against COLOR's ±25 Hz, so COLOR was
     * simply the coarser detune and there was no reason to reach for it.
     * Spreading *timbre* instead gives it something RELATION cannot do — the
     * voices diverge in harmonic content rather than in pitch, which thickens
     * the ensemble without widening it, and the beating between partials
     * stops being identical on every voice.
     *
     * @param base  the SHAPE knob position
     * @param amt   spread depth (COLOR × 0.5 → up to ±0.25 of the morph)
     * @param off   this voice's position in the spread, −0.5 … +0.5
     */
    static inline float _shapeSpread(float base, float amt, float off)
    {
        float s = base + amt * off;
        // Clamping rather than folding: at the ends of the SHAPE morph the
        // spread simply becomes one-sided, which still reads as divergence.
        if(s < 0.0f)
            s = 0.0f;
        else if(s > 1.0f)
            s = 1.0f;
        return s;
    }

    /**
     * Subsets of kCloudOffset for each odd stack width, indexed (width-1)/2.
     *
     * Every row keeps index 3 — the centre voice — so COLOR's centre-against-
     * sides law survives at any width. The narrower rows take the *outermost*
     * offsets rather than the innermost: three saws at the full ±11% spread
     * still read as a supersaw, where three saws clustered at ±2% read as
     * chorus and lose the mode entirely.
     */
    static constexpr uint8_t kCloudWidthIdx[4][7] = {
        {3, 0, 0, 0, 0, 0, 0}, // width 1 — centre alone
        {0, 3, 6, 0, 0, 0, 0}, // width 3 — outer pair + centre
        {0, 2, 3, 4, 6, 0, 0}, // width 5
        {0, 1, 2, 3, 4, 5, 6}, // width 7 — the full JP-8000 stack
    };

    /**
     * Widest odd stack that `notes` of them will fit in `pool`.
     *
     * The loop steps down by two so the result is always odd (see
     * kCloudWidthIdx) and always fits — there is deliberately no lower clamp
     * beyond 1. A floor of, say, three saws would let a pool be oversubscribed
     * whenever cloudMaxNotes was raised past pool/3, and an oversubscribed pool
     * is an oscillator sounding in two stacks at once. Degrading to a single
     * saw a note is the honest answer to "you asked for more notes than you
     * gave me oscillators".
     */
    static inline uint8_t _cloudWidthFor(uint8_t pool, uint8_t notes)
    {
        if(notes == 0u)
            notes = 1u;
        uint8_t w = 7u;
        while(w > 1u && (uint16_t)w * (uint16_t)notes > (uint16_t)pool)
            w -= 2u;
        return w;
    }

    /** Re-randomise every pool phase (mode entry — see _cloudRandomisePhases
     *  for why a *known* phase is the wrong answer for a supersaw). */
    void _cloudRandomisePhases();

    /** Randomise one oscillator's phase as it is claimed for a new stack.
     *  Only ever called on an oscillator that has already ramped to silence,
     *  so the discontinuity is inaudible. */
    void _cloudClaimOsc(uint8_t osc, uint8_t owner, uint8_t offIdx);

    /** Re-plan the pool for this control tick: retire what no longer belongs,
     *  then claim what is newly wanted. */
    void _cloudPlan(const SynthParams &p, PolySlot polySlots[6]);

    /** Ensure `owner` has an oscillator on detune `offIdx`, claiming a free one
     *  if not. Silently does nothing when the pool is momentarily exhausted —
     *  the need is retried on the next tick. */
    void _cloudClaimIfNeeded(uint8_t owner, uint8_t offIdx, uint8_t pool);

    // --- Pool state -------------------------------------------------------
    // _cloudOscOwner is the single source of truth; the slot lists below are
    // rebuilt from it every tick rather than maintained incrementally, which
    // is sixteen iterations at 128 Hz and removes a whole class of bookkeeping
    // bug for it.
    uint8_t
        _cloudOscOwner[kCloudOscMax]; // slot, kCloudDroneSlot, or kCloudNoOwner
    uint8_t _cloudOscOff[kCloudOscMax]; // which kCloudOffset entry it plays
    /// Fade gate, 0–1, linear at kCloudGateStep a tick. Multiplies the level
    /// COLOR and the width normalisation produce, so the fade is independent
    /// of how loud the saw happens to be — a saw entering a quiet stack takes
    /// exactly as long as one entering a loud stack.
    float _cloudOscGate[kCloudOscMax];

    /// Notes that have arrived but whose stack the planner has not handed out
    /// yet. Set by polyRetrigger(), cleared in control() on the tick the slot
    /// gets its oscillators — which is where its envelope is armed, so that
    /// the envelope and the sound it shapes begin on the same sample. Also
    /// tells _cloudClaimOsc() the stack is silent, so its saws open at full
    /// gate instead of fading in under an envelope that is already moving.
    bool _cloudPendingAttack[kCloudMaxNotes] = {};
    float _cloudOscGateTgt[kCloudOscMax];
    /// Set by _cloudPlan(): this oscillator still belongs in the plan. A false
    /// here is a *retiring* oscillator — it keeps its owner and keeps
    /// rendering under that stack's envelope while its level ramps out, and is
    /// only freed once it reaches silence.
    bool _cloudOscWanted[kCloudOscMax] = {};
    /// Slot is held, or still ringing out a release. Notes come and go between
    /// control ticks, so this is latched once per tick and then used by the
    /// planner, the sub and the tracking HPF alike.
    bool _cloudSounding[kCloudMaxNotes] = {};

    // -----------------------------------------------------------------------
    // Render lists — everything audio() needs to draw a stack, in one place.
    //
    // Grouped by stack so the per-sample envelope is one float multiply per
    // *stack* rather than one per oscillator: four multiplies a sample at full
    // polyphony against twelve, which is most of what makes a twelve-oscillator
    // mode affordable at all.
    //
    // Each entry carries the oscillator *pointer* and its own pan weights
    // rather than an index into _voices/_panL. Two reasons, and both showed up
    // on hardware:
    //
    //  - Cost. An index means the inner loop recomputes `_voices[i]` — a load
    //    and a multiply by sizeof(ShapeOsc) — for every oscillator of every
    //    sample, where POLY's flat loop strength-reduces to a pointer walk.
    //    Contiguous entries make this loop the same shape as POLY's.
    //  - Correctness. It puts the pan weights inside the same published
    //    snapshot as the list, so a stack can never render against another
    //    tick's levels.
    //
    // ⚠ DOUBLE-BUFFERED, and it has to be. control() runs on Core 0 and audio()
    // on Core 1, so a list rebuilt in place is read while it is half-written —
    // and because a rebuild starts by zeroing the counts, the reader does not
    // see a *slightly* wrong stack, it sees an **empty** one. At 128 Hz that is
    // a hole in the output on every control tick: heard on hardware as constant
    // crackle at any polyphony, with ~50% CPU headroom and no overruns, and
    // invisible to the host harness, where the two run sequentially.
    //
    // control() fills the buffer audio() is not reading, then publishes it with
    // a single byte store.
    // -----------------------------------------------------------------------
    struct CloudEntry
    {
        ShapeOsc<48000u> *osc  = nullptr;
        int16_t           panL = 0;
        int16_t           panR = 0;
    };
    CloudEntry       _cloudList[2][kCloudMaxNotes + 1][7] = {};
    uint8_t          _cloudListN[2][kCloudMaxNotes + 1]   = {};
    volatile uint8_t _cloudListIdx                        = 0;

    /// Which stack the single sub belongs to, so it is shaped by that stack's
    /// envelope rather than droning under a chord that has already released.
    /// kCloudNoOwner when nothing is sounding — the sub is skipped entirely.
    /// Volatile for the same cross-core reason as _cloudListIdx: written on
    /// Core 0, read every sample on Core 1.
    volatile uint8_t _cloudSubSlot = kCloudDroneSlot;

    /// The sub's own level, matching the gain of the stack it is tuned under.
    ///
    /// Without this the sub is the loudest thing in the mode as soon as CLOUD
    /// goes polyphonic. kSubScale[CLOUD] is 1.35 — scaled *up*, because the
    /// mode runs one sub where PAIR runs two — and that was calibrated when
    /// CLOUD was a single stack at unity. Once the stacks dropped to
    /// _cloudNoteGain, an unscaled sub became two to three times too loud
    /// against them: FATNESS turned into a different control in poly than in
    /// drone, and a low-frequency signal that big is what actually eats the
    /// headroom a chord needs.
    volatile float _cloudSubGain = 1.0f;

    uint8_t _cloudPool    = kCloudPoolDefault; // clamped copy of cloudPool
    uint8_t _cloudNotes   = kCloudMaxNotes;    // clamped copy of cloudMaxNotes
    uint8_t _cloudWidth   = 7u;                // current per-stack width
    bool    _cloudDroning = true; // no notes: the stack is the drone

    /// The drone stack's own gain, ramped *per sample* in audio() — see the
    /// two-ramps note above kCloudGateTicks. Volatile because control() sets
    /// the target on Core 0 and audio() advances it on Core 1.
    float          _cloudDroneGain = 1.0f;
    volatile float _cloudDroneTgt  = 1.0f;
    float          _cloudDroneRamp = 0.0f; // one-pole coeff for _audioRate
    /// Fixed 1/sqrt(cloudMaxNotes) headroom for the note stacks — fixed, not
    /// scaled by how many are sounding, for the reason POLY's normalisation
    /// comment gives. The drone does not take it (it is one stack by
    /// definition), which is what keeps drone level identical to pre-M78.
    float _cloudNoteGain = 0.5f;

    /** Cheap LCG — phase randomisation only, never in the audio path. */
    uint32_t        _rngState = 0x9E3779B9u;
    inline uint32_t _rng()
    {
        _rngState = _rngState * 1664525u + 1013904223u;
        return _rngState;
    }

    // -----------------------------------------------------------------------
    // PLASMA — cross-modulating pair through a ring mod
    //
    // Two sine operators, M and C, each phase-modulating the *other* from its
    // previous sample, each also modulating itself, and the pair multiplied
    // together as C²·M on the way out.
    //
    // The bidirectional coupling is what separates this from CASCADE. CASCADE
    // is a chain — modulator into carrier, one way, fixed musical ratio, and
    // it stays harmonic by construction. Here the two operators are a loop:
    // each one's output is in the other's input, so past a certain depth the
    // system stops being a pair of oscillators and starts being one coupled
    // system that rings, beats and eventually breaks up. That is the mode.
    //
    // Design owes its topology to Geodesics' Dark Energy (Pierre Collard and
    // Marc Boulé, GPL-3.0), reimplemented here rather than ported: theirs runs
    // each operator at 8× with a CIC decimator, which the RP2350 does not have
    // the budget for alongside the effect chain. Running at rate instead means
    // feedback FM aliases at high MOTION — on a mode whose whole character is
    // instability that reads as grit rather than as a defect, and it is the
    // trade that makes the mode possible at all here.
    //
    // Two cells, detuned against each other and panned apart, for the same
    // reason CASCADE has two pairs: one cell is a mono source and no pan
    // weight can widen it. Coupled systems diverge, so the two decorrelate on
    // their own and the image opens up as the sound gets wilder.
    // -----------------------------------------------------------------------

    /// Per-cell previous outputs — the feedback taps, read next sample.
    int16_t _plasmaPrevM[2] = {0, 0};
    int16_t _plasmaPrevC[2] = {0, 0};
    /// Two-sample averages of the same. Self-feedback FM is unstable on a
    /// single-sample tap and will latch into a screech; averaging the last two
    /// damps that without audibly softening the tone. The DX7 did the same.
    int16_t _plasmaAvgM[2] = {0, 0};
    int16_t _plasmaAvgC[2] = {0, 0};

    /// COLOR → cross-modulation depth, as a Q16 phase scale.
    volatile float _plasmaCross = 0.0f;
    /// MOTION → self-feedback depth, same units.
    volatile float _plasmaFb = 0.0f;

    /// RELATION → C:M frequency ratio. Cached; powf() only when it moves.
    float _cachedRelPlasma = -99.0f;
    float _plasmaRatio     = 1.0f;

    /** Peak phase deviation, in radians, at full COLOR. Deeper than PAIR and
     *  CASCADE's 3 — the coupling needs room to actually destabilise. */
    static constexpr float kPlasmaCrossRad = 4.5f;
    /** Peak self-feedback deviation. Held well below the cross depth: past
     *  about 2 radians a self-modulating sine stops being a tone at all. */
    static constexpr float kPlasmaFbRad = 1.6f;
    /** Radians → the Q16 table-phase units nextPM() expects. */
    static constexpr float kPlasmaPhaseScale
        = (2048.0f * 65536.0f) / (2.0f * 3.14159265f * 32512.0f);

    // One-pole HPF tracking the played note, applied to CLOUD only. The
    // JP-8000 has one and it is what keeps a seven-saw stack from turning to
    // mud in the low register — without it the detuned partials pile up below
    // the fundamental. Coefficient set at control rate, state advanced in
    // audio().
    volatile float _cloudHpA  = 0.0f;
    float          _cloudHpXL = 0.0f, _cloudHpYL = 0.0f;
    float          _cloudHpXR = 0.0f, _cloudHpYR = 0.0f;

    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    // FM phase scale: kFmMaxScale * modSample gives a Q16 phase offset that
    // corresponds to ±3 radians of phase modulation at max COLOR.
    static constexpr float kFmMaxScale
        = 3.0f * (2048.0f * 65536.0f) / (2.0f * 3.14159265f * 32512.0f);
};

// No global instance.  Each platform owns its own: the firmware keeps one file-
// scope object in main.cpp, VCV keeps one per Module (so two AlloyFlux modules
// in a rack do not share DSP state).  Code that needs to start a note without
// knowing about the engine calls polyNoteOn() — see params.h.
