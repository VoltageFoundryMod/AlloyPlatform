/**
 * SynthEngine.cpp — AlloyFlux core DSP engine (M37b extraction).
 *
 * Extracted from src/main.cpp so the same synthesis code can be shared
 * between the hardware firmware (Raspberry Pi Pico 2) and the
 * future VCV Rack plugin (M37c+).
 *
 * This file also provides the canonical definitions of the global pointers
 * declared extern in params.h so that commands.cpp and usb_midi.cpp continue
 * to compile without modification.
 */

#include "SynthEngine.h"
#include "dsp/SpaceEngine.h"
#include <math.h>
#include <string.h> // memset

// ---------------------------------------------------------------------------
// SynthEngine constructor — pre-wires the reverb pointer so the object is
// usable before init() runs.
// ---------------------------------------------------------------------------
SynthEngine::SynthEngine()
{ reverb = &_dattorroReverb; }

// ---------------------------------------------------------------------------
// Global pointer/array definitions that params.h declares extern.
//
// These point into whichever SynthEngine instance called init() — the firmware
// keeps one in main.cpp, VCV keeps one per Module — so they are "the running
// engine's", not any particular object's.  Set in init().
// ---------------------------------------------------------------------------
FilterEngine   *gFilterInst = nullptr;
EnvelopeEngine *gCurveEng   = nullptr;

// POLY voice allocator — written by usb_midi.cpp, read by SynthEngine::control().
PolySlot sPolySlots[6] = {
    {440.0f, 1.0f, 255},
    {440.0f, 1.0f, 255},
    {440.0f, 1.0f, 255},
    {440.0f, 1.0f, 255},
    {440.0f, 1.0f, 255},
    {440.0f, 1.0f, 255},
};
uint8_t sPolyRR = 0;

// Per-voice envelope pointers for POLY mode — written by SynthEngine::init(),
// read by usb_midi.cpp (setGate) and by SynthEngine::audio() (next()).
EnvelopeEngine *sPolyEnvs[6] = {};

// ---------------------------------------------------------------------------
// SynthEngine::init()
// ---------------------------------------------------------------------------
void SynthEngine::init(uint32_t audioRate, uint32_t controlRate)
{
    _audioRate   = audioRate;
    _controlRate = controlRate;

    // Generate wavetables first — oscillators must be pointing at valid data
    // before any setFreq() / setShape() calls.
    _generateWavetables();

    // Assign table pointers to all oscillators.  Seven of them — CLOUD's
    // supersaw needs the seventh; POLY uses only the first six.
    for(int i = 0; i < 7; i++)
    {
        _voices[i].setTables(
            _sineTable, _triTable, _sawTable, _squareTable, _narrowPulseTable);
        _subVoices[i].setTables(
            _sineTable, _triTable, _sawTable, _squareTable, _narrowPulseTable);
        // Update sample rate. The template argument is only a default; this is
        // what actually sets the rate on both platforms.
        _voices[i].setSampleRate(audioRate);
        _subVoices[i].setSampleRate(audioRate);
        // Fixed square shape for sub oscillators.
        _subVoices[i].setShape(0.75f);
    }

    // Poly envelopes — six, one per POLY slot.
    for(int i = 0; i < 6; i++)
    {
        _polyEnvArr[i].setSampleRate(audioRate);
        polyEnvs[i]  = &_polyEnvArr[i];
        sPolyEnvs[i] = &_polyEnvArr[i];
    }

    // Envelope engines
    _arEnv.setSampleRate(audioRate);
    _adsrEnv.setSampleRate(audioRate);
    curveEng  = &_arEnv;
    gCurveEng = &_arEnv;

    _delay.setSampleRate(audioRate);

    // Chorus — must run before the audio ISR starts.
    _chorus.init(audioRate);

    // Filter
    _filterType     = FilterType::SVF;
    _prevFilterType = FilterType::SVF;
    filterInst      = &_svfFilter;
    gFilterInst     = &_svfFilter;

    // Reverb / delay
    _dattorroReverb.setSampleRate((float)audioRate);
    _dattorroReverb.reset();
    reverb = &_dattorroReverb;

    // Initial voice frequencies
    for(int i = 0; i < 7; i++)
    {
        _voices[i].setFreq(440.0f);
        _subVoices[i].setFreq(440.0f * 0.5f);
    }

    // Pre-warm powf() so the first CHORD/PAIR updateControl() call avoids a
    // cold math-table flash-cache miss.
    volatile float _pw = powf(2.0f, 7.0f / 12.0f);
    (void)_pw;
}

// ---------------------------------------------------------------------------
// SynthEngine::setSampleRate()  — M37c: called on VCV sampleRate change.
// ---------------------------------------------------------------------------
void SynthEngine::setSampleRate(uint32_t audioRate)
{
    _audioRate = audioRate;
    for(int i = 0; i < 7; i++)
    {
        _voices[i].setSampleRate(audioRate);
        _subVoices[i].setSampleRate(audioRate);
    }
    for(int i = 0; i < 6; i++)
        _polyEnvArr[i].setSampleRate(audioRate);
    _arEnv.setSampleRate(audioRate);
    _adsrEnv.setSampleRate(audioRate);
    _chorus.setSampleRate(audioRate);
    // Delay converts ms → samples, so it needs the rate too; control() re-issues
    // setParams() every tick, which re-derives the length from the new rate.
    _delay.setSampleRate(audioRate);
    // Only feeds the reverb's LFO phase advance; its delay lines are fixed
    // sample counts and do not rescale.
    _dattorroReverb.setSampleRate((float)audioRate);
    // Wavetables are sample-rate independent, and filter coefficients are
    // recomputed from _audioRate on every control tick.
}

// ---------------------------------------------------------------------------
// SynthEngine::_cloudDetuneCurve()  — CLOUD supersaw detune law
//
// Szabo's fitted 11th-order polynomial for the JP-8000's DETUNE knob. The
// shape is the point: almost flat for the first third of travel — where the
// useful chorusing lives, and where a linear knob gives you nothing usable —
// then opening out steeply toward a spread of roughly ±11%, about 1.8
// semitones, at full CW.
//
// Evaluated with Horner. Runs at control rate and only when RELATION moves,
// so the order is free; the clamps matter more than the cost, because the
// polynomial diverges hard outside 0–1.
// ---------------------------------------------------------------------------
float SynthEngine::_cloudDetuneCurve(float x)
{
    if(x < 0.0f)
        x = 0.0f;
    else if(x > 1.0f)
        x = 1.0f;

    float y = 10028.7312891634f;
    y       = y * x - 50818.8652045924f;
    y       = y * x + 111363.4808729368f;
    y       = y * x - 138150.6761080548f;
    y       = y * x + 106649.6679158292f;
    y       = y * x - 53046.9642751875f;
    y       = y * x + 17019.9518580080f;
    y       = y * x - 3425.0836591318f;
    y       = y * x + 404.2703938388f;
    y       = y * x - 24.1878824391f;
    y       = y * x + 0.6717417634f;
    y       = y * x + 0.0030115596f;

    // The fit overshoots slightly at both ends; a negative detune would
    // mirror the stack and a runaway one would detune it into noise.
    if(y < 0.0f)
        y = 0.0f;
    else if(y > 1.0f)
        y = 1.0f;
    return y;
}

// ---------------------------------------------------------------------------
// SynthEngine::_cloudRandomisePhases()
//
// Randomising the seven phases is not decoration — it is load-bearing twice
// over. Musically it is why no two supersaw stabs sound quite alike, which is
// half of what people recognise in the JP-8000. Structurally it is what keeps
// seven near-identical oscillators from starting in lockstep and summing
// coherently into a peak three times the intended level, which at low
// RELATION they would otherwise hold indefinitely — nothing pulls them apart
// once their phase increments match.
//
// Called on note attack and on entry to CLOUD. The second is what covers
// drone, where there is no attack to hang it on.
// ---------------------------------------------------------------------------
void SynthEngine::_cloudRandomisePhases()
{
    for(int i = 0; i < 7; i++)
    {
        _voices[i].setPhase(_rng());
        _subVoices[i].setPhase(_rng());
    }
}

// ---------------------------------------------------------------------------
// SynthEngine::control()
// ---------------------------------------------------------------------------
void SynthEngine::control(const SynthParams  &p,
                          PolySlot            polySlots[6],
                          SynthControlOutput &out)
{
    // ------------------------------------------------------------------
    // One-pole smoothing — eliminates zipper noise on parameter changes
    // ------------------------------------------------------------------
    float smoothAlpha = 0.3f;
    _sShape += (p.shape - _sShape) * smoothAlpha;
    _sFatness += (p.fatness - _sFatness) * smoothAlpha;
    _sMotion += (p.motion - _sMotion) * 0.15f; // slower smoothing for drift
    _sCurve += (p.curve - _sCurve) * smoothAlpha;
    _sCurveTime += (p.curveTime - _sCurveTime) * smoothAlpha;
    _sVolume += (p.volume - _sVolume) * smoothAlpha;
    _sMidiVel += (p.midiVelocity - _sMidiVel) * smoothAlpha;
    _sSpace += (p.space - _sSpace) * smoothAlpha;
    _sRelation += (p.relation - _sRelation) * smoothAlpha;
    _sColor += (p.color - _sColor) * 0.2f;

    // ------------------------------------------------------------------
    // Envelope update — AR or ADSR
    // ------------------------------------------------------------------
    if(p.envelopeType == EnvelopeType::AR)
    {
        static_cast<AREnvelope<48000u> *>(curveEng)->setCurve(_sCurve,
                                                              _sCurveTime);
    }
    else
    {
        // In ADSR mode, CURVE is a global time scale for A, D, R (sustain is
        // amplitude, not time, so it is unaffected).
        //   curve 0.0 → ×0.25  (tight / percussive)
        //   curve 0.5 → ×1.0   (knob values unchanged)
        //   curve 1.0 → ×4.0   (slow / pad-like)
        const float tScale = powf(4.0f, 2.0f * _sCurve - 1.0f);
        static_cast<ADSREnvelope<48000u> *>(curveEng)->setADSR(
            p.adsrAttack * tScale,
            p.adsrDecay * tScale,
            p.adsrSustain,
            p.adsrRelease * tScale,
            p.adsrLoop);
    }

    // ------------------------------------------------------------------
    // Gate edge detection → envelope + phase reset
    // ------------------------------------------------------------------
    {
        const bool curGate = p.gateHigh;
        if(p.voiceMode != VoiceMode::POLY && curGate != _prevGate)
        {
            curveEng->setGate(curGate);
            if(curGate && curveEng->level() < 0.01f)
            {
                // CLOUD wants the opposite of a known phase — see
                // _cloudRandomisePhases().
                if(p.voiceMode == VoiceMode::CLOUD)
                    _cloudRandomisePhases();
                else
                    for(int i = 0; i < 4; i++)
                    {
                        _voices[i].resetPhase();
                        _subVoices[i].resetPhase();
                    }
            }
            _prevGate = curGate;
        }
    }

    // ------------------------------------------------------------------
    // FM depth pre-computation (PAIR + CASCADE)
    // COLOR drives depth; tanhf soft-clips; volatile write for ISR
    // ------------------------------------------------------------------
    if(p.voiceMode == VoiceMode::PAIR || p.voiceMode == VoiceMode::CASCADE)
    {
        _sFmDepth = tanhf(_sColor * 3.0f * 0.7f) * kFmMaxScale;
    }
    else
    {
        _sFmDepth = 0.0f;
    }

    // ------------------------------------------------------------------
    // Drift engine
    // ------------------------------------------------------------------
    _drift.setSpeed(p.driftSpeed);
    _drift.update(_sMotion);

    // ------------------------------------------------------------------
    // Portamento / glide
    // ------------------------------------------------------------------
    if(p.glideEnabled && p.glideTime > 0.001f)
    {
        const float alpha
            = 1.0f - expf(-1.0f / (p.glideTime * (float)_controlRate));
        _sGlideBase += (p.baseFreq - _sGlideBase) * alpha;
    }
    else
    {
        _sGlideBase = p.baseFreq;
    }

    // ------------------------------------------------------------------
    // FM IN — external pitch FM, exponential, kFmInOctPerVolt per volt.
    //
    // Applied here rather than in fillSynthParams() for two reasons, both of
    // which would silently swallow the modulation upstream: a held MIDI note
    // overwrites p.baseFreq wholesale (main.cpp / AlloyFlux.cpp), and the VCV
    // scale quantizer rounds it to the nearest semitone. Downstream of both,
    // FM survives being played from the keyboard and is not quantized away.
    //
    // A multiplier, not an offset — every voice mode spaces its voices from
    // _sGlidedFreq by ratio, so scaling the fundamental moves a chord without
    // detuning it. POLY is the exception and applies _fmInMul to its own slot
    // frequencies below; the other modes inherit it here.
    //
    // ⚠ Written to _sGlidedFreq, *never* back into _sGlideBase. _sGlideBase is
    // the portamento filter's own state, and feeding a modulated value into it
    // would make the next tick glide away from a pitch the player never asked
    // for — FM would leak into the portamento and smear it.
    //
    // Guarded on the unpatched value: exp2f is ~90 µs on the RP2350, and an
    // unpatched jack should not pay for it every control tick.
    _fmInMul     = (p.fmIn != 0.0f) ? exp2f(p.fmIn * kFmInOctPerVolt) : 1.0f;
    _sGlidedFreq = _sGlideBase * _fmInMul;
    if(_sGlidedFreq < 20.0f)
        _sGlidedFreq = 20.0f;

    float voiceFreqs[7] = {_sGlidedFreq,
                           _sGlidedFreq,
                           _sGlidedFreq,
                           _sGlidedFreq,
                           _sGlidedFreq,
                           _sGlidedFreq,
                           _sGlidedFreq};

    // ------------------------------------------------------------------
    // Voice mode — frequency assignment + pan
    // ------------------------------------------------------------------
    _voiceMode             = p.voiceMode;
    const bool modeChanged = (_voiceMode != _prevVoiceMode);
    _prevVoiceMode         = _voiceMode;

    switch(p.voiceMode)
    {
        case VoiceMode::PAIR:
        default:
        {
            const float semitones = roundf(_sRelation);
            if(semitones != _cachedRelPair || modeChanged)
            {
                _cachedRatioPair = powf(2.0f, semitones / 12.0f);
                _cachedRelPair   = semitones;
            }
            voiceFreqs[0] = _sGlidedFreq + _drift.offset(0);
            if(voiceFreqs[0] < 20.0f)
                voiceFreqs[0] = 20.0f;
            voiceFreqs[1] = _sGlidedFreq * _cachedRatioPair + _drift.offset(1);
            if(voiceFreqs[1] < 20.0f)
                voiceFreqs[1] = 20.0f;
            _voices[0].setFreq(voiceFreqs[0]);
            _voices[1].setFreq(voiceFreqs[1]);
            _voices[0].setShape(_sShape);
            _voices[1].setShape(_sShape);
            {
                const float sm = (p.subOctave == 2) ? 0.25f : 0.5f;
                _subVoices[0].setFreq(voiceFreqs[0] * sm);
                _subVoices[1].setFreq(voiceFreqs[1] * sm);
            }
            _activeVoices = 2;
            _fmPairs      = 1; // PAIR sums its modulator; one pair only
            _panL[0]      = 256;
            _panL[1]      = 0;
            _panL[2]      = 0;
            _panL[3]      = 0;
            _panR[0]      = 0;
            _panR[1]      = 256;
            _panR[2]      = 0;
            _panR[3]      = 0;
            break;
        }
        case VoiceMode::CHORD:
        {
            static const int8_t kChordTable[11][4] = {
                {0, 0, 0, 0},    //  0  Unison
                {0, 7, 12, 19},  //  1  Power
                {0, 3, 7, 12},   //  2  Minor
                {0, 4, 7, 12},   //  3  Major
                {0, 2, 7, 12},   //  4  Sus2
                {0, 5, 7, 12},   //  5  Sus4
                {0, 4, 7, 11},   //  6  Major 7
                {0, 3, 7, 10},   //  7  Minor 7
                {0, 4, 7, 10},   //  8  Dominant 7
                {0, 3, 6, 9},    //  9  Diminished
                {0, 12, 24, 36}, // 10  Octaves
            };
            // COLOR — voicing, in octaves. RELATION says *which* chord; this
            // says how it is spaced, lifting the upper voices away from the
            // root to open a close triad out across the register.
            //
            // Quantised to four positions rather than swept continuously, and
            // in octaves rather than semitones, because both alternatives put
            // the chord out of tune for most of the knob's travel — a smooth
            // sweep would glide the upper voices through every microtone on
            // the way. Discrete octave steps are in tune at every position,
            // which is the whole point of being in CHORD.
            //
            // This replaces a fourth flavour of detune beating, which is what
            // COLOR did here and what RELATION already does better.
            // Six positions, each lifting one more voice by one more octave.
            // Monotonic in total spread — 0, 12, 24, 36, 48, 60 semitones
            // added — so the knob opens the chord steadily rather than
            // jumping about, and no single voice is lifted more than two
            // octaves at any position.
            static const int8_t kVoicing[6][4] = {
                {0, 0, 0, 0},    // 0  close  — as voiced in the chord table
                {0, 0, 0, 12},   // 1  lift   — top voice up an octave
                {0, 0, 12, 12},  // 2  open   — top two up
                {0, 0, 12, 24},  // 3  stack  — top up two octaves
                {0, 12, 12, 24}, // 4  spread — everything above the root up
                {0, 12, 24, 24}, // 5  wide
            };
            const int chordIdx = (_sRelation / 24.0f * 10.0f + 0.5f < 10.5f)
                                     ? (int)(_sRelation / 24.0f * 10.0f + 0.5f)
                                     : 10;
            int       voicingIdx = (int)(_sColor * 5.0f + 0.5f);
            if(voicingIdx < 0)
                voicingIdx = 0;
            else if(voicingIdx > 5)
                voicingIdx = 5;

            if(chordIdx != _cachedChordIdx || voicingIdx != _cachedVoicingChord
               || fabsf(_sGlidedFreq - _cachedChordBase) > 0.01f || modeChanged)
            {
                for(int i = 0; i < 4; i++)
                    _cachedFreqsChord[i]
                        = _sGlidedFreq
                          * powf(2.0f,
                                 (float)(kChordTable[chordIdx][i]
                                         + kVoicing[voicingIdx][i])
                                     / 12.0f);
                _cachedChordIdx     = chordIdx;
                _cachedVoicingChord = voicingIdx;
                _cachedChordBase    = _sGlidedFreq;
            }
            // Octaves + wide voicing reaches five octaves above the root, and
            // the wavetables are band-limited for a 440 Hz fundamental — far
            // enough up they alias instead of getting brighter. Ceiling the
            // voice rather than letting the top of the chord turn to grit.
            const float chordMaxF = (float)_audioRate * 0.22f;
            for(int i = 0; i < 4; i++)
            {
                voiceFreqs[i] = _cachedFreqsChord[i] + _drift.offset(i);
                if(voiceFreqs[i] < 20.0f)
                    voiceFreqs[i] = 20.0f;
                else if(voiceFreqs[i] > chordMaxF)
                    voiceFreqs[i] = chordMaxF;
                _voices[i].setFreq(voiceFreqs[i]);
                _voices[i].setShape(_sShape);
                const float sm = (p.subOctave == 2) ? 0.25f : 0.5f;
                _subVoices[i].setFreq(voiceFreqs[i] * sm);
            }
            _activeVoices = 4;
            _panL[0]      = 128;
            _panL[1]      = 90;
            _panL[2]      = 38;
            _panL[3]      = 0;
            _panR[0]      = 0;
            _panR[1]      = 38;
            _panR[2]      = 90;
            _panR[3]      = 128;
            break;
        }
        case VoiceMode::CLOUD:
        {
            // ----------------------------------------------------------------
            // Supersaw. RELATION is DETUNE, COLOR is MIX — the JP-8000's own
            // two controls, landed on the two knobs that carry mode identity
            // here. They are genuinely orthogonal, which is the point: COLOR
            // in the other ensemble modes is a second detune spread, and next
            // to RELATION it has very little to say. Here it sets *balance*,
            // sweeping from a single clean voice to the full seven-wide stack
            // without moving a single frequency.
            // ----------------------------------------------------------------
            if(modeChanged)
                _cloudRandomisePhases(); // covers drone: no attack to hang on

            // DETUNE — cached against RELATION alone. Proportional, so the
            // stack stays equally wide wherever it is played.
            if(fabsf(_sRelation - _cachedRelCloud) > 0.01f || modeChanged)
            {
                const float d = _cloudDetuneCurve(_sRelation / 24.0f);
                for(int i = 0; i < 7; i++)
                    _cloudDetuneMul[i] = 1.0f + d * kCloudOffset[i];
                _cachedRelCloud = _sRelation;
            }

            // MIX — centre against sides, on Szabo's two fitted curves. At
            // full CCW the centre voice carries the sound almost alone; at
            // full CW the six sides dominate it.
            if(fabsf(_sColor - _cachedColorCloud) > 0.002f || modeChanged)
            {
                float mix = _sColor;
                if(mix < 0.0f)
                    mix = 0.0f;
                else if(mix > 1.0f)
                    mix = 1.0f;
                const float centre = -0.55366f * mix + 0.99785f;
                const float side
                    = -0.73764f * mix * mix + 1.2841f * mix + 0.044372f;

                // Normalise in *quadrature*, not by the plain sum: the seven
                // voices are detuned and so add incoherently, and normalising
                // by the arithmetic sum would make COLOR read as a volume cut
                // rather than as the density control it is.
                float sumSq = centre * centre + 6.0f * side * side;
                if(sumSq < 1e-6f)
                    sumSq = 1e-6f;
                const float norm = 1.0f / sqrtf(sumSq);

                for(int i = 0; i < 7; i++)
                    _cloudLevel[i] = (i == 3 ? centre : side) * norm;
                _cachedColorCloud = _sColor;
            }

            // Voices. MOTION is dialled well back from the old CLOUD (×1.5 →
            // ×0.4): the detune already supplies the width, and drift on top
            // of it only smears the beating that makes a supersaw legible.
            // Movement is STRING's job now — that split is what finally tells
            // the two modes apart.
            const float smCloud = (p.subOctave == 2) ? 0.25f : 0.5f;
            for(int i = 0; i < 7; i++)
            {
                voiceFreqs[i] = _sGlidedFreq * _cloudDetuneMul[i]
                                + _drift.offset(i) * 0.4f;
                if(voiceFreqs[i] < 20.0f)
                    voiceFreqs[i] = 20.0f;
                _voices[i].setFreq(voiceFreqs[i]);
                _voices[i].setShape(_sShape);

                // Stereo: flat-to-sharp maps left-to-right, so the detune
                // reads as width rather than as mistuning — the same law POLY
                // uses. Constant-sum, matching the ensemble modes.
                const float w   = kCloudPanScale * _cloudLevel[i];
                const float p01 = (float)i * (1.0f / 6.0f);
                _panL[i]        = (int16_t)(w * (1.0f - p01));
                _panR[i]        = (int16_t)(w * p01);
            }

            // One sub, on the centre voice, rather than seven. The JP-8000 has
            // none at all; seven would be seven more oscillators for a band
            // the stack already fills. FATNESS still does something useful.
            _subVoices[3].setFreq(_sGlidedFreq * smCloud < 20.0f
                                      ? 20.0f
                                      : _sGlidedFreq * smCloud);

            // Track the HPF to the played note — see _cloudHpA.
            {
                const float fc = _sGlidedFreq;
                const float w0 = 6.2831853f * fc / (float)_audioRate;
                _cloudHpA      = 1.0f / (1.0f + w0);
            }

            _activeVoices = 7;
            break;
        }
        case VoiceMode::CASCADE:
        {
            static constexpr float kCascadeRatios[]
                = {1.0f, 1.333f, 1.5f, 2.0f, 2.5f, 3.0f};
            const int zoneIdx
                = (_sRelation / 4.0f < 5.0f) ? (int)(_sRelation / 4.0f) : 5;
            const float ratio = kCascadeRatios[zoneIdx];
            // Two carrier/modulator pairs, panned apart. FM sums only the
            // carrier — the modulator is never heard directly — so a single
            // pair is one mono source and no pan weight can widen it. A second
            // pair is what gives CASCADE a stereo image at all.
            //
            // The pairs are detuned a fixed ±1.4 cents against each other, on
            // top of whatever drift MOTION adds. Without that they would be
            // sample-identical at MOTION = 0 and collapse straight back to
            // mono. The detune is applied to the *pair*, so the FM ratio inside
            // each one stays exact — that ratio is what keeps CASCADE harmonic.
            static constexpr float kPairDetuneA = 0.9992f; // −1.4 cents
            static constexpr float kPairDetuneB = 1.0008f; // +1.4 cents
            const float            baseA        = _sGlidedFreq * kPairDetuneA;
            const float            baseB        = _sGlidedFreq * kPairDetuneB;

            voiceFreqs[0] = baseA + _drift.offset(0);
            if(voiceFreqs[0] < 20.0f)
                voiceFreqs[0] = 20.0f;
            voiceFreqs[1] = baseA * ratio + _drift.offset(1);
            if(voiceFreqs[1] < 20.0f)
                voiceFreqs[1] = 20.0f;
            voiceFreqs[2] = baseB + _drift.offset(2);
            if(voiceFreqs[2] < 20.0f)
                voiceFreqs[2] = 20.0f;
            voiceFreqs[3] = baseB * ratio + _drift.offset(3);
            if(voiceFreqs[3] < 20.0f)
                voiceFreqs[3] = 20.0f;

            {
                const float sm = (p.subOctave == 2) ? 0.25f : 0.5f;
                for(int i = 0; i < 4; i++)
                {
                    _voices[i].setFreq(voiceFreqs[i]);
                    _voices[i].setShape(_sShape);
                    _subVoices[i].setFreq(voiceFreqs[i] * sm);
                }
            }
            _activeVoices = 4;
            // Carriers soft-panned opposite; L+R per pair = 256, so the summed
            // level matches the single centred carrier this replaced.
            // Modulators stay silent — they are heard only through the phase
            // modulation they apply.
            _panL[0] = 192;
            _panR[0] = 64;
            _panL[1] = 0;
            _panR[1] = 0;
            _panL[2] = 64;
            _panR[2] = 192;
            _panL[3] = 0;
            _panR[3] = 0;
            // Set last: this is the flag the audio core reads to start
            // rendering pair B, so its frequencies and pans must already be in
            // place. The reverse switch is safe for the same reason — the pans
            // it would read are zero.
            _fmPairs = 2;
            break;
        }
        case VoiceMode::STRING:
        {
            if(fabsf(_sRelation - _cachedRelStr) > 0.05f
               || fabsf(_sGlidedFreq - _cachedBaseStr) > 0.01f || modeChanged)
            {
                const float spreadCents = (_sRelation / 24.0f) * 30.0f;
                const float offCents[4] = {-spreadCents * 0.5f,
                                           -spreadCents * (1.0f / 6.0f),
                                           spreadCents * (1.0f / 6.0f),
                                           spreadCents * 0.5f};
                for(int i = 0; i < 4; i++)
                    _cachedFreqsStr[i]
                        = _sGlidedFreq * powf(2.0f, offCents[i] / 1200.0f);
                _cachedRelStr  = _sRelation;
                _cachedBaseStr = _sGlidedFreq;
            }
            // COLOR — timbre spread across the four voices, not a second
            // detune. See _shapeSpread(). RELATION owns pitch width here and
            // MOTION owns the movement; this is the third axis, and the one
            // that lets a string section be lush without being wider.
            static constexpr float kColorOff[4]
                = {-0.5f, -1.0f / 6.0f, 1.0f / 6.0f, 0.5f};
            const float shapeSpread = _sColor * 0.5f;
            for(int i = 0; i < 4; i++)
            {
                voiceFreqs[i] = _cachedFreqsStr[i] + _drift.offset(i) * 3.0f;
                if(voiceFreqs[i] < 20.0f)
                    voiceFreqs[i] = 20.0f;
                _voices[i].setFreq(voiceFreqs[i]);
                _voices[i].setShape(
                    _shapeSpread(_sShape, shapeSpread, kColorOff[i]));
                const float sm = (p.subOctave == 2) ? 0.25f : 0.5f;
                _subVoices[i].setFreq(voiceFreqs[i] * sm);
            }
            _activeVoices = 4;
            _panL[0]      = 128;
            _panL[1]      = 90;
            _panL[2]      = 38;
            _panL[3]      = 0;
            _panR[0]      = 0;
            _panR[1]      = 38;
            _panR[2]      = 90;
            _panR[3]      = 128;
            break;
        }
        case VoiceMode::POLY:
        {
            const float            subMult = (p.subOctave == 2) ? 0.25f : 0.5f;
            static constexpr float kColorOff[6]
                = {-0.5f, -0.3f, -0.1f, 0.1f, 0.3f, 0.5f};
            // COLOR — timbre spread per slot, matching STRING. It was a fixed
            // Hz offset, which in POLY is the worst place for one: the slots
            // hold different notes, so the same offset is a shimmer up top and
            // a sour interval down low.
            const float shapeSpread = _sColor * 0.5f;
            _activeVoices           = 6;

            // MOTION scales with how much is being held. A single note stays
            // steady enough to play a line on; a full chord breathes. Drift on
            // a lone voice is just tuning instability — it only reads as life
            // when there is something for it to beat against.
            uint8_t polyHeld = 0;
            for(int i = 0; i < 6; i++)
                if(polySlots[i].midiNote != kPolySlotFree)
                    polyHeld++;
            const float polyDrift
                = 0.35f
                  + 0.65f
                        * (polyHeld > 1 ? (float)(polyHeld - 1) * (1.0f / 5.0f)
                                        : 0.0f);

            // RELATION — detune spread across the slots, ±15 cents at full CW,
            // matching STRING's range. In *cents*, not Hz: poly notes span the
            // keyboard, and a fixed Hz offset would be an inaudible nudge in
            // the top octave and a sour interval in the bottom one. COLOR keeps
            // its absolute-Hz spread, so the two layer the way they do in the
            // ensemble modes — proportional detune underneath, constant-rate
            // beating on top.
            //
            // The spread runs flat-to-sharp in the same order the pan table
            // runs left-to-right, so the detune reads as width rather than as
            // mistuning.
            if(fabsf(_sRelation - _cachedRelPoly) > 0.05f || modeChanged)
            {
                const float spreadCents = (_sRelation / 24.0f) * 30.0f;
                for(int i = 0; i < 6; i++)
                    _polyDetuneMul[i]
                        = powf(2.0f, spreadCents * kColorOff[i] / 1200.0f);
                _cachedRelPoly = _sRelation;
            }
            // Fixed normalisation: scale by 1/sqrt(6) — equal-power headroom for
            // the maximum voice count.  A DYNAMIC sounding-based scale is tempting
            // for loudness stability but causes retroactive gain changes on existing
            // notes every time a new note starts, which sounds like notes "dying".
            // Fixed scale means each voice always contributes the same amount;
            // the user's master volume knob compensates for the −7.8 dB headroom.
            //
            // Slots sit at fixed stereo positions so a held chord opens out
            // across the field instead of stacking in the centre. The law is
            // **constant power** — cos/sin of an angle sweeping 5°..85°, so
            // L² + R² is identical for every slot. That matters here and not in
            // the ensemble modes: allocation is round-robin, so the same note
            // lands in a different slot each time it is played, and the
            // constant-sum law those modes use would make it audibly louder at
            // the edges than in the middle. Precomputed — no trig at runtime.
            //
            // The arc stops just short of hard L/R on purpose. A single note
            // played on its own still reaches both channels instead of coming
            // out of one speaker, while a full chord spans nearly the whole
            // field. SPACE is a mid/side stage downstream and this is what
            // gives it something to act on: SPACE 0 collapses the spread back
            // to mono, 1 leaves it as voiced, 2 throws it wider than the arc.
            static constexpr float wScale = 256.0f / 2.449f; // 2.449 ≈ sqrt(6)
            // cos/sin(5° + i·16°) × sqrt(2) — the sqrt(2) puts a centred slot
            // on exactly wScale, matching the level this replaced.
            static constexpr float kPanL[6]
                = {1.4088f, 1.3203f, 1.1294f, 0.8511f, 0.5068f, 0.1233f};
            static constexpr float kPanR[6]
                = {0.1233f, 0.5068f, 0.8511f, 1.1294f, 1.3203f, 1.4088f};
            for(int i = 0; i < 6; i++)
            {
                // _fmInMul, not _sGlidedFreq: POLY takes its pitch per slot
                // from the allocator and never passes through the glide stage
                // that carries FM IN for every other mode.
                float f = polySlots[i].freq * _polyDetuneMul[i] * _fmInMul
                          + _drift.offset(i) * polyDrift;
                if(f < 20.0f)
                    f = 20.0f;
                _voices[i].setFreq(f);
                _voices[i].setShape(
                    _shapeSpread(_sShape, shapeSpread, kColorOff[i]));
                _polyEnvArr[i].setCurve(_sCurve, p.curveTime);
                _subVoices[i].setFreq((f * subMult < 20.0f) ? 20.0f
                                                            : f * subMult);
                _subVoices[i].setShape(0.75f);
                const float w = wScale * polySlots[i].velocity;
                _panL[i]      = (int16_t)(w * kPanL[i]);
                _panR[i]      = (int16_t)(w * kPanR[i]);
            }
            break;
        }
        case VoiceMode::PLASMA:
        {
            // RELATION — C:M ratio, continuous from ÷2 to ×8. Continuous and
            // not zoned on purpose: CASCADE already offers six ratios chosen
            // to stay harmonic, and the whole reason to have this mode as well
            // is to be able to sit *between* them. The clangorous, beating,
            // faintly wrong ratios are the ones worth reaching for here.
            if(fabsf(_sRelation - _cachedRelPlasma) > 0.01f || modeChanged)
            {
                _plasmaRatio     = 0.5f * powf(2.0f, _sRelation / 24.0f * 4.0f);
                _cachedRelPlasma = _sRelation;
            }

            // COLOR → cross-mod depth, MOTION → self-feedback. Both soft-
            // clipped rather than linear, so the knob keeps resolution in the
            // range where the system is still musical and compresses the top
            // end where it is already fully unstable.
            _plasmaCross
                = tanhf(_sColor * 2.0f) * kPlasmaCrossRad * kPlasmaPhaseScale;
            _plasmaFb
                = tanhf(_sMotion * 1.6f) * kPlasmaFbRad * kPlasmaPhaseScale;

            // Two cells, detuned ±1.4 cents against each other so they do not
            // start out sample-identical, then left to diverge on their own.
            static constexpr float kCellA = 0.9992f;
            static constexpr float kCellB = 1.0008f;

            float fM0 = _sGlidedFreq * kCellA + _drift.offset(0);
            float fM1 = _sGlidedFreq * kCellB + _drift.offset(2);
            if(fM0 < 20.0f)
                fM0 = 20.0f;
            if(fM1 < 20.0f)
                fM1 = 20.0f;
            // C is ceilinged separately: at ×8 ratio on a high root it would
            // otherwise run past the band limit of the wavetables.
            const float maxC = (float)_audioRate * 0.22f;
            float       fC0  = fM0 * _plasmaRatio + _drift.offset(1);
            float       fC1  = fM1 * _plasmaRatio + _drift.offset(3);
            if(fC0 < 20.0f)
                fC0 = 20.0f;
            else if(fC0 > maxC)
                fC0 = maxC;
            if(fC1 < 20.0f)
                fC1 = 20.0f;
            else if(fC1 > maxC)
                fC1 = maxC;

            _voices[0].setFreq(fM0);
            _voices[1].setFreq(fC0);
            _voices[2].setFreq(fM1);
            _voices[3].setFreq(fC1);
            // SHAPE still morphs both operators. A sine pair is the classic
            // reading, but feeding the loop a saw or a pulse is where it gets
            // genuinely violent, and there is no reason to withhold that.
            for(int i = 0; i < 4; i++)
                _voices[i].setShape(_sShape);

            // One sub, centred, on the root — the ring-mod product is thin on
            // its own and this is what gives it a floor to stand on.
            const float smPlasma = (p.subOctave == 2) ? 0.25f : 0.5f;
            _subVoices[0].setFreq(fM0 * smPlasma < 20.0f ? 20.0f
                                                         : fM0 * smPlasma);

            _activeVoices = 4;
            _panL[0]      = 192;
            _panR[0]      = 64;
            _panL[1]      = 64;
            _panR[1]      = 192;
            break;
        }
    } // end switch

    // Mode transition cleanup
    if(modeChanged)
    {
        for(int i = 0; i < 6; i++)
        {
            _polyEnvArr[i].reset();
            sPolySlots[i].midiNote = kPolySlotFree;
        }
        sPolyRR = 0;
        // CLOUD's HPF holds the last sample it saw; entering the mode with a
        // stale one in the filter is a click.
        _cloudHpXL = _cloudHpYL = 0.0f;
        _cloudHpXR = _cloudHpYR = 0.0f;
        // PLASMA's feedback taps likewise. A coupled system started from a
        // stale state can ring before it settles, which on entry sounds like
        // a fault rather than like the mode.
        for(int i = 0; i < 2; i++)
        {
            _plasmaPrevM[i] = _plasmaPrevC[i] = 0;
            _plasmaAvgM[i] = _plasmaAvgC[i] = 0;
        }
    }

    // ------------------------------------------------------------------
    // Sub weight — per mode, so FATNESS means the same amount of sub
    // wherever the knob is turned.
    //
    // It used to be one global figure, which quietly made the knob mean
    // different things: PAIR puts two sub oscillators in the mix, the
    // ensemble modes four, POLY six. Same knob position, three times the
    // sub energy, and the four-voice modes muddied first.
    //
    // The scales below normalise against PAIR, which is the default and so
    // the voicing everyone learns first. Modes whose voices sit at
    // *different* pitches add incoherently and are scaled by √(2/n);
    // STRING's four sit at near-unison and do add coherently, so it takes
    // the full 2/n. CLOUD is scaled up, not down — the supersaw runs a
    // single sub on its centre voice where PAIR has two.
    // ------------------------------------------------------------------
    {
        static const float kSubScale[7] = {
            1.00f, // PAIR    — 2 subs, an interval apart
            1.35f, // CLOUD   — 1 sub, on the centre voice only
            0.70f, // CHORD   — 4 subs on chord tones, incoherent
            1.00f, // CASCADE — 2 audible subs; the modulators' are muted
            0.50f, // STRING  — 4 subs at near-unison, coherent
            0.60f, // POLY    — 6 subs, each on its own note
            1.35f, // PLASMA  — 1 sub, and the ring mod above it is thin
        };
        const int mi = (int)p.voiceMode;
        _sSubWf
            = _sFatness * 0.5f * ((mi >= 0 && mi < 7) ? kSubScale[mi] : 1.0f);
    }

    // Chorus depth — STRING keeps a 0.3 minimum
    chorusDepth = (p.voiceMode == VoiceMode::STRING)
                      ? (_sMotion > 0.3f ? _sMotion : 0.3f)
                      : _sMotion;
    _chorusMode = p.chorusMode;

    // ------------------------------------------------------------------
    // Filter type switch
    // ------------------------------------------------------------------
    if(p.filterType != _prevFilterType)
    {
        filterInst->reset();
        filterInst      = (p.filterType == FilterType::SVF)
                              ? static_cast<FilterEngine *>(&_svfFilter)
                              : static_cast<FilterEngine *>(&_otaLadder);
        gFilterInst     = filterInst;
        _filterType     = p.filterType;
        _prevFilterType = p.filterType;
    }
    {
        _sFilterCutoff += (p.filterCutoff - _sFilterCutoff) * smoothAlpha;
        _sFilterRes += (p.filterRes - _sFilterRes) * smoothAlpha;
        filterInst->setParams(
            _sFilterCutoff, _sFilterRes, p.filterMode, (float)_audioRate);
    }

    // ------------------------------------------------------------------
    // Envelope type switch
    // ------------------------------------------------------------------
    if(p.envelopeType != _prevEnvType)
    {
        curveEng->reset();
        curveEng     = (p.envelopeType == EnvelopeType::AR)
                           ? static_cast<EnvelopeEngine *>(&_arEnv)
                           : static_cast<EnvelopeEngine *>(&_adsrEnv);
        gCurveEng    = curveEng;
        _envType     = p.envelopeType;
        _prevEnvType = p.envelopeType;
    }

    // ------------------------------------------------------------------
    // Fx ordering
    // ------------------------------------------------------------------
    _fxOrder = p.fxOrder;

    // ------------------------------------------------------------------
    // Delay params
    // ------------------------------------------------------------------
    _delay.setParams(p.delayTime, p.delayFeedback, p.delayMix);

    // ------------------------------------------------------------------
    // Reverb params
    // ------------------------------------------------------------------
    // Deadbands rather than exact compares: revSize and revDamping arrive from
    // an ADC, whose last bit dithers, and an exact test would reconfigure the
    // plate on every one of the 128 ticks a second even with the knob at rest.
    // The thresholds sit below what is audible on either control.
    if(fabsf(p.revSize - _prevRevSize) > 0.0025f
       || fabsf(p.revDamping - _prevRevDamping) > 0.0025f)
    {
        reverb->setParams(p.revSize, p.revDamping);
        _prevRevSize    = p.revSize;
        _prevRevDamping = p.revDamping;
    }
    if(fabsf(p.revModSpeed - _prevRevModSpeed) > 0.01f
       || fabsf(p.revModDepth - _prevRevModDepth) > 0.01f)
    {
        reverb->setModulation(p.revModSpeed, p.revModDepth);
        _prevRevModSpeed = p.revModSpeed;
        _prevRevModDepth = p.revModDepth;
    }
    if(p.revFrozen != _prevRevFrozen)
    {
        reverb->freeze(p.revFrozen);
        _prevRevFrozen = p.revFrozen;
    }

    // ------------------------------------------------------------------
    // Fill output snapshot for Core 1 bookkeeping
    // ------------------------------------------------------------------
    out.freq1   = voiceFreqs[0];
    out.freq2   = voiceFreqs[1];
    out.shape   = _sShape;
    out.fatness = _sFatness;
    out.motion  = _sMotion;
    out.curve   = _sCurve;
    out.volume  = _sVolume;
}

// ---------------------------------------------------------------------------
// SynthEngine::polyRetrigger()
// ---------------------------------------------------------------------------
void SynthEngine::polyRetrigger(uint8_t slot, float freq, float subMult)
{
    if(slot >= 6)
        return;
    // 1. Hard-silence the envelope so the attack always starts from 0.
    _polyEnvArr[slot].reset();
    // 2. Reset oscillator phase — new note starts at a known waveform position.
    _voices[slot].resetPhase();
    _subVoices[slot].resetPhase();
    // 3. Apply the new frequency immediately (normally deferred to next control
    //    tick), so the attack samples are at the correct pitch from the start.
    _voices[slot].setFreq(freq);
    const float subFreq = freq * subMult;
    _subVoices[slot].setFreq(subFreq < 20.0f ? 20.0f : subFreq);
    // 4. Arm the attack.
    _polyEnvArr[slot].setGate(true);
}

// ---------------------------------------------------------------------------
// SynthEngine::audio()
// ---------------------------------------------------------------------------
void SynthEngine::audio(int32_t  revWetL,
                        int32_t  revWetR,
                        float    revMix,
                        bool     revEnabled,
                        int32_t *finalL,
                        int32_t *finalR,
                        int32_t *dryForRevL,
                        int32_t *dryForRevR)
{
    const bool isPolyMode   = (_voiceMode == VoiceMode::POLY);
    const bool isCloudMode  = (_voiceMode == VoiceMode::CLOUD);
    const bool isPlasmaMode = (_voiceMode == VoiceMode::PLASMA);
    const bool isFmMode
        = (_voiceMode == VoiceMode::CASCADE || _voiceMode == VoiceMode::PAIR);

    // ------------------------------------------------------------------
    // Oscillator mix — sum sActiveVoices into L/R via pan weights.
    // FM modes: voice[1] PM-modulates voice[0].
    // POLY mode: per-voice envelopes applied inside the loop.
    // ------------------------------------------------------------------
    int32_t left = 0, right = 0;
    if(isFmMode)
    {
        const int16_t modSample = _voices[1].next();
        const int32_t sub1      = _subVoices[1].next();
        const int32_t pmOffset  = (int32_t)((float)modSample * _sFmDepth);
        const int32_t carrier   = _voices[0].nextPM(pmOffset);
        const int32_t sub0      = _subVoices[0].next();
        const int32_t m0 = (int32_t)((float)carrier + (float)sub0 * _sSubWf);
        const int32_t m1 = (int32_t)((float)modSample + (float)sub1 * _sSubWf);
        left             = ((m0 * _panL[0]) + (m1 * _panL[1])) >> 8;
        right            = ((m0 * _panR[0]) + (m1 * _panR[1])) >> 8;
        if(_fmPairs == 2)
        {
            // CASCADE's second pair — voices 2/3, detuned against 0/1 and
            // panned opposite. Only the carrier is summed, exactly as above.
            const int16_t modSampleB = _voices[3].next();
            const int32_t sub3       = _subVoices[3].next();
            const int32_t pmOffsetB  = (int32_t)((float)modSampleB * _sFmDepth);
            const int32_t carrierB   = _voices[2].nextPM(pmOffsetB);
            const int32_t sub2       = _subVoices[2].next();
            const int32_t m2
                = (int32_t)((float)carrierB + (float)sub2 * _sSubWf);
            const int32_t m3
                = (int32_t)((float)modSampleB + (float)sub3 * _sSubWf);
            left += ((m2 * _panL[2]) + (m3 * _panL[3])) >> 8;
            right += ((m2 * _panR[2]) + (m3 * _panR[3])) >> 8;
        }
    }
    else if(isPlasmaMode)
    {
        // Two coupled cells. Each operator is phase-modulated by the *other's*
        // last sample plus a damped tap of its own — read the taps before
        // either is advanced, so both operators see the same instant and the
        // loop stays symmetric.
        const float cross = _plasmaCross;
        const float fb    = _plasmaFb;

        for(int cell = 0; cell < 2; cell++)
        {
            const int vM = cell * 2;     // 0, 2
            const int vC = cell * 2 + 1; // 1, 3

            const int32_t offM = (int32_t)((float)_plasmaPrevC[cell] * cross
                                           + (float)_plasmaAvgM[cell] * fb);
            const int32_t offC = (int32_t)((float)_plasmaPrevM[cell] * cross
                                           + (float)_plasmaAvgC[cell] * fb);

            const int16_t m = _voices[vM].nextPM(offM);
            const int16_t c = _voices[vC].nextPM(offC);

            _plasmaAvgM[cell]
                = (int16_t)(((int32_t)m + _plasmaPrevM[cell]) >> 1);
            _plasmaAvgC[cell]
                = (int16_t)(((int32_t)c + _plasmaPrevC[cell]) >> 1);
            _plasmaPrevM[cell] = m;
            _plasmaPrevC[cell] = c;

            // Ring mod: C² · M. Squaring C frequency-doubles it and leaves a
            // DC term, so the product carries both M itself and M rung at
            // twice C — the metallic half of the sound. Both shifts stay
            // inside int32: 32512² is 1.06e9 against a 2.15e9 ceiling.
            //
            // The two >>15 shifts land the product at full scale on their own
            // — peak |c| = |m| = 32512 gives 32002 out — so there is no
            // makeup gain here, and adding one clips. Measured RMS runs 1.5
            // to 4.5 dB under a plain oscillator depending on ratio and
            // depth, which is the right amount quieter for what it is.
            const int32_t c2  = ((int32_t)c * (int32_t)c) >> 15;
            const int32_t out = (c2 * (int32_t)m) >> 15;

            left += (out * _panL[cell]) >> 8;
            right += (out * _panR[cell]) >> 8;
        }

        if(_sSubWf > 0.001f)
        {
            const int32_t sub = _subVoices[0].next();
            const int32_t sm  = (int32_t)((float)sub * _sSubWf);
            left += sm >> 1; // centred
            right += sm >> 1;
        }
    }
    else if(isCloudMode)
    {
        // Supersaw: seven mains, and exactly one sub — the centre voice's.
        // Skipping the other six is what keeps a seven-oscillator mode
        // cheaper than POLY's six-plus-six.
        for(uint8_t i = 0; i < 7; i++)
        {
            const int32_t s = _voices[i].next();
            left += (s * _panL[i]) >> 8;
            right += (s * _panR[i]) >> 8;
        }
        if(_sSubWf > 0.001f)
        {
            const int32_t sub = _subVoices[3].next();
            const int32_t sm  = (int32_t)((float)sub * _sSubWf);
            left += sm >> 1; // centred
            right += sm >> 1;
        }

        // Note-tracking one-pole HPF. Applied here, before the limiter, so it
        // takes the low pile-up out *before* anything has a chance to clip on
        // it rather than after.
        const float a  = _cloudHpA;
        const float xl = (float)left, xr = (float)right;
        _cloudHpYL = a * (_cloudHpYL + xl - _cloudHpXL);
        _cloudHpYR = a * (_cloudHpYR + xr - _cloudHpXR);
        _cloudHpXL = xl;
        _cloudHpXR = xr;
        left       = (int32_t)_cloudHpYL;
        right      = (int32_t)_cloudHpYR;
    }
    else if(isPolyMode)
    {
        // Main oscillator is ALWAYS advanced to keep its phase accumulator live —
        // skipping next() freezes the phase at DC and causes silent/corrupt output
        // when a new note starts.  Sub-voice, multiply, and pan accumulation are
        // still skipped for silent voices (env < threshold) to save CPU.
        const bool hasSub = _sSubWf > 0.001f;
        for(uint8_t i = 0; i < 6; i++)
        {
            const float env
                = _polyEnvArr[i].next(); // direct call, no virtual dispatch
            const int32_t s = _voices[i].next(); // always advance phase
            if(env < 0.001f)
                continue; // skip expensive work only
            int32_t m;
            if(hasSub)
            {
                const int32_t sub = _subVoices[i].next();
                m = (int32_t)(((float)s + (float)sub * _sSubWf) * env);
            }
            else
            {
                m = (int32_t)((float)s * env);
            }
            left += (m * _panL[i]) >> 8;
            right += (m * _panR[i]) >> 8;
        }
    }
    else
    {
        // Non-FM non-POLY: pull sub-voice work out of the inner loop.
        if(_sSubWf > 0.001f)
        {
            for(uint8_t i = 0; i < _activeVoices; i++)
            {
                const int32_t s   = _voices[i].next();
                const int32_t sub = _subVoices[i].next();
                const int32_t m   = (int32_t)((float)s + (float)sub * _sSubWf);
                left += (m * _panL[i]) >> 8;
                right += (m * _panR[i]) >> 8;
            }
        }
        else
        {
            for(uint8_t i = 0; i < _activeVoices; i++)
            {
                const int32_t s = _voices[i].next();
                left += (s * _panL[i]) >> 8;
                right += (s * _panR[i]) >> 8;
            }
        }
    }

    // Transparent safety limit before the VCA.
    // The previous always-on Padé soft clip distorted even nominal single-voice
    // sine output, injecting harmonics into otherwise clean tones. Limit only on
    // true overflow so sub-clipping signals remain fully linear.
    if(left > 32767)
        left = 32767;
    else if(left < -32767)
        left = -32767;
    if(right > 32767)
        right = 32767;
    else if(right < -32767)
        right = -32767;

    // ------------------------------------------------------------------
    // VCA — envelope × volume × velocity, with de-click on downward moves
    // ------------------------------------------------------------------
    const float envLevel
        = (!isPolyMode && gGatePatched) ? curveEng->next() : 1.0f;
    const float gainTarget = _sVolume * _sMidiVel * envLevel;
    if(gainTarget < _sGainSmooth)
        _sGainSmooth += (gainTarget - _sGainSmooth) * 0.2f;
    else
        _sGainSmooth = gainTarget;
    left  = (int32_t)((float)left * _sGainSmooth);
    right = (int32_t)((float)right * _sGainSmooth);

    // ------------------------------------------------------------------
    // Post-effects chain — two configurable positions
    // ------------------------------------------------------------------

    // [FILTER — PRE-CHORUS]
    if(!_fxOrder.filterPostChorus)
        filterInst->process(left, right, &left, &right);

    // Chorus
    {
        int32_t outL, outR;
        _chorus.process(left, right, chorusDepth, _chorusMode, &outL, &outR);
        left  = outL;
        right = outR;
    }

    // [FILTER — POST-CHORUS]
    if(_fxOrder.filterPostChorus)
        filterInst->process(left, right, &left, &right);

    // [DELAY — PRE-REVERB]
    if(!_fxOrder.delayPostReverb)
        _delay.process(left, right, &left, &right);

    // Export pre-reverb signal for Core 1
    *dryForRevL = left;
    *dryForRevR = right;

    // Mix reverb wet return
    if(revEnabled)
    {
        left += (int32_t)((float)revWetL * revMix);
        right += (int32_t)((float)revWetR * revMix);
        // Hard-limit to prevent int16 wrapping crackle when passed to from16Bit.
        // The previous Padé soft-clip caused ~6-7% continuous non-linear distortion
        // at typical reverb tail levels (s≈0.4-0.6), adding harmonics to the tail
        // and producing audible shimmer. A hard-limit is fully transparent at all
        // levels up to ±32767 and only activates at simultaneous dry+wet peaks.
        if(left > 32767)
            left = 32767;
        else if(left < -32767)
            left = -32767;
        if(right > 32767)
            right = 32767;
        else if(right < -32767)
            right = -32767;
    }

    // [DELAY — POST-REVERB]
    if(_fxOrder.delayPostReverb)
        _delay.process(left, right, &left, &right);

    // SPACE — stereo width
    if(_sSpace < 0.995f || _sSpace > 1.005f)
    {
        int32_t spL, spR;
        SpaceEngine::process(left, right, _sSpace, &spL, &spR);
        left  = spL;
        right = spR;
    }

    *finalL = left;
    *finalR = right;
}

// ---------------------------------------------------------------------------
// Wavetable generation (extracted verbatim from main.cpp)
// ---------------------------------------------------------------------------
void SynthEngine::_normaliseTable(const float *buf, int16_t *dst, int n)
{
    float peak = 0.0f;
    for(int i = 0; i < n; i++)
        if(fabsf(buf[i]) > peak)
            peak = fabsf(buf[i]);
    if(peak < 1e-6f)
        peak = 1.0f;
    const float scale = 32767.0f / peak;
    for(int i = 0; i < n; i++)
        dst[i] = (int16_t)(buf[i] * scale);
}

void SynthEngine::_generateWavetables()
{
    static float buf[TABLE_CELLS]; // static: avoids 4 KB stack frame
    const int    N    = (int)TABLE_CELLS;
    const int    maxH = ((int)_audioRate / 2) / 440; // 54 @ 48000 Hz

    // Sine
    for(int i = 0; i < N; i++)
    {
        const float phase = 2.0f * 3.14159265f * i / N;
        buf[i]            = sinf(phase);
    }
    _normaliseTable(buf, _sineTable, N);

    // Triangle
    for(int i = 0; i < N; i++)
    {
        const float phase = 2.0f * 3.14159265f * i / N;
        float       val   = 0.0f;
        for(int h = 1; h <= maxH; h += 2)
        {
            const float x     = (float)h * 3.14159265f / (maxH + 1);
            const float sigma = sinf(x) / x;
            const float sign  = (((h - 1) / 2) & 1) ? -1.0f : 1.0f;
            val += sigma * sign * sinf((float)h * phase) / ((float)h * h);
        }
        buf[i] = val;
    }
    _normaliseTable(buf, _triTable, N);

    // Sawtooth
    for(int i = 0; i < N; i++)
    {
        const float phase = 2.0f * 3.14159265f * i / N;
        float       val   = 0.0f;
        for(int h = 1; h <= maxH; h++)
        {
            const float x     = (float)h * 3.14159265f / (maxH + 1);
            const float sigma = sinf(x) / x;
            val += sigma * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    _normaliseTable(buf, _sawTable, N);

    // Square / 50% pulse
    for(int i = 0; i < N; i++)
    {
        const float phase = 2.0f * 3.14159265f * i / N;
        float       val   = 0.0f;
        for(int h = 1; h <= maxH; h += 2)
        {
            const float x     = (float)h * 3.14159265f / (maxH + 1);
            const float sigma = sinf(x) / x;
            val += sigma * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    _normaliseTable(buf, _squareTable, N);

    // Hollow / 25% duty-cycle pulse
    for(int i = 0; i < N; i++)
    {
        const float phase = 2.0f * 3.14159265f * i / N;
        float       val   = 0.0f;
        for(int h = 1; h <= maxH; h++)
        {
            const float x     = (float)h * 3.14159265f / (maxH + 1);
            const float sigma = sinf(x) / x;
            const float duty  = sinf((float)h * 3.14159265f * 0.25f);
            val += sigma * duty * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    _normaliseTable(buf, _narrowPulseTable, N);
}
