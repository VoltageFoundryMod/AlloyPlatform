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
// SynthParams — snapshot of all goal parameters for one control cycle.
//
// The hardware shim in main.cpp populates this struct from the gXxx globals
// each updateControl() tick and passes it to SynthEngine::control().
// The future VCV Rack module will populate it directly from widget state.
// ---------------------------------------------------------------------------
struct SynthParams
{
    float        baseFreq    = 440.0f;
    float        shape       = 0.0f;
    float        fatness     = 0.4f;
    uint8_t      subOctave   = 1; // 1 = one octave below, 2 = two octaves below
    float        motion      = 0.0f;
    float        driftSpeed  = 0.04f;
    VoiceMode    voiceMode   = VoiceMode::PAIR;
    float        relation    = 0.0f;
    float        curve       = 0.5f;
    float        curveTime   = 1.0f;
    bool         gateHigh    = false;
    bool         gatePatched = false;
    float        volume      = 1.0f;
    float        midiVelocity  = 1.0f;
    float        glideTime     = 0.0f;
    bool         glideEnabled  = false;
    ChorusMode   chorusMode    = ChorusMode::I_II;
    float        space         = 1.0f;
    float        color         = 0.0f;
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
     * (M37c) Runtime sample-rate change — re-initialises all rate-dependent
     * engines without clearing note/sequence state.
     */
    void setSampleRate(uint32_t audioRate);

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

    // -----------------------------------------------------------------------
    // Oscillators — 7 main voices + 7 sub voices.
    //
    // Seven, not six, because of CLOUD: the JP-8000 supersaw is a seven-saw
    // stack and the count is not arbitrary — the detune offsets below are
    // fitted to that many. POLY still uses only the first six.
    // -----------------------------------------------------------------------
    ShapeOsc<48000u> _voices[7];
    ShapeOsc<48000u> _subVoices[7];

    // -----------------------------------------------------------------------
    // Envelopes
    // -----------------------------------------------------------------------
    AREnvelope<48000u>   _arEnv;
    ADSREnvelope<48000u> _adsrEnv;
    EnvelopeType         _envType     = EnvelopeType::AR;
    EnvelopeType         _prevEnvType = EnvelopeType::AR;

    // POLY mode — 6 independent per-voice envelopes.
    AREnvelope<48000u> _polyEnvArr[6];

    // -----------------------------------------------------------------------
    // Effect engines
    // -----------------------------------------------------------------------
    DriftEngine<7u>      _drift;
    ChorusEngine<48000u> _chorus;

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

    // Portamento smoother.
    float _sGlidedFreq = 440.0f;

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
    uint8_t _fmPairs = 1u;
    int16_t _panL[7] = {256, 0, 0, 0, 0, 0, 0};
    int16_t _panR[7] = {0, 256, 0, 0, 0, 0, 0};

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
    float _cachedChordBase     = -1.0f;
    float _cachedFreqsChord[4] = {440.0f, 440.0f, 440.0f, 440.0f};

    // CLOUD — supersaw. Unlike the other ensemble modes nothing here needs a
    // powf(): detune is a *proportional* offset, so the per-voice value is a
    // plain multiplier against the root and the cache holds the multipliers
    // rather than absolute frequencies. RELATION moving costs seven multiplies.
    float _cachedRelCloud    = -99.0f;
    float _cloudDetuneMul[7] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    // Per-voice level, already normalised. Recomputed when COLOR moves.
    float _cachedColorCloud = -99.0f;
    float _cloudLevel[7]    = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};

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
     * seven decorrelated voices are added in quadrature — see _cloudLevel's
     * normalisation. Held a little under the exact match for headroom: the
     * supersaw is peakier than its RMS suggests.
     */
    static constexpr float kCloudPanScale = 240.0f;

    /** Detune curve: RELATION 0–1 → detune fraction. Szabo's fitted 11th-order
     *  polynomial. Control rate only, so the order costs nothing. */
    static float _cloudDetuneCurve(float x);

    /** Re-randomise all seven supersaw phases (note attack, mode entry). */
    void _cloudRandomisePhases();

    /** Cheap LCG — phase randomisation only, never in the audio path. */
    uint32_t        _rngState = 0x9E3779B9u;
    inline uint32_t _rng()
    {
        _rngState = _rngState * 1664525u + 1013904223u;
        return _rngState;
    }

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
