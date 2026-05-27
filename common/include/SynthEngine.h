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
//   1. Create global instance: SynthEngine gSynthEngine;
//   2. Call gSynthEngine.init(audioRate, controlRate) once at startup.
//   3. Each control tick: gSynthEngine.control(params, polySlots, output)
//   4. Each audio sample: gSynthEngine.audio(...)
//
// Audio signal range is kept as int32_t ±32512 throughout to match the
// existing Mozzi pipeline.  Float conversion happens only at the ISR boundary.
//
// For VCV Rack (M37c+): call setSampleRate() when the host rate changes.
// ---------------------------------------------------------------------------
class SynthEngine
{
  public:
    // -----------------------------------------------------------------------
    // Constructor — pre-wires the reverb pointer so Core 1's setup1() can
    // safely call reverb->reset() before init() is called on Core 0.
    // _dattorroReverb is a concrete member so its address is fixed at
    // object-construction time, even before init() runs.
    // -----------------------------------------------------------------------
    SynthEngine();

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /**
     * One-time init — generate wavetables, seed all engines.
     * Must be called before startMozzi() so delay buffers are warm.
     * @param audioRate   samples/sec (firmware: MOZZI_AUDIO_RATE = 32768)
     * @param controlRate control ticks/sec (firmware: MOZZI_CONTROL_RATE = 128)
     */
    void init(uint32_t audioRate = 32768u, uint32_t controlRate = 128u);

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
     * revMix     — wet-return gain (0.0 = dry only, 1.0 = full wet).
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
    uint32_t _audioRate   = 32768u;
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
    // Oscillators — 4 main voices + 4 sub voices.
    // -----------------------------------------------------------------------
    ShapeOsc<32768u> _voices[6];
    ShapeOsc<32768u> _subVoices[6];

    // -----------------------------------------------------------------------
    // Envelopes
    // -----------------------------------------------------------------------
    AREnvelope<32768u>   _arEnv;
    ADSREnvelope<32768u> _adsrEnv;
    EnvelopeType         _envType     = EnvelopeType::AR;
    EnvelopeType         _prevEnvType = EnvelopeType::AR;

    // POLY mode — 6 independent per-voice envelopes.
    AREnvelope<32768u> _polyEnvArr[6];

    // -----------------------------------------------------------------------
    // Effect engines
    // -----------------------------------------------------------------------
    DriftEngine<6u>      _drift;
    ChorusEngine<32768u> _chorus;

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
    int16_t _panL[6]      = {256, 0, 0, 0, 0, 0};
    int16_t _panR[6]      = {0, 256, 0, 0, 0, 0};

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

    // CLOUD
    float _cachedRelCloud      = -99.0f;
    float _cachedBaseCloud     = -1.0f;
    float _cachedFreqsCloud[4] = {440.0f, 440.0f, 440.0f, 440.0f};

    // STRING
    float _cachedRelStr      = -99.0f;
    float _cachedBaseStr     = -1.0f;
    float _cachedFreqsStr[4] = {440.0f, 440.0f, 440.0f, 440.0f};

    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    // FM phase scale: kFmMaxScale * modSample gives a Q16 phase offset that
    // corresponds to ±3 radians of phase modulation at max COLOR.
    static constexpr float kFmMaxScale
        = 3.0f * (2048.0f * 65536.0f) / (2.0f * 3.14159265f * 32512.0f);
};

// Global instance — defined in SynthEngine.cpp; used by main.cpp, commands.cpp,
// usb_midi.cpp (via the sPolySlots / gCurveEng / gFilterInst globals that are
// now defined in SynthEngine.cpp and point into this instance).
extern SynthEngine gSynthEngine;
