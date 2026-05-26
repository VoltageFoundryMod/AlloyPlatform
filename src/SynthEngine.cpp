/**
 * SynthEngine.cpp — AlloyFlux core DSP engine (M37b extraction).
 *
 * Extracted from src/main.cpp so the same synthesis code can be shared
 * between the hardware firmware (Mozzi / Raspberry Pi Pico 2) and the
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
// SynthEngine constructor — pre-wires the reverb pointer so Core 1's setup1()
// can safely call reverb->reset() before init() runs on Core 0.
// ---------------------------------------------------------------------------
SynthEngine::SynthEngine() {
    reverb = &_dattorroReverb;
}

// ---------------------------------------------------------------------------
// Global instance
// ---------------------------------------------------------------------------
SynthEngine gSynthEngine;

// ---------------------------------------------------------------------------
// Global pointer/array definitions that params.h declares extern.
// These point into gSynthEngine's concrete member objects; set in init().
// ---------------------------------------------------------------------------
FilterEngine *gFilterInst = nullptr;
EnvelopeEngine *gCurveEng = nullptr;

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
void SynthEngine::init(uint32_t audioRate, uint32_t controlRate) {
    _audioRate = audioRate;
    _controlRate = controlRate;

    // Generate wavetables first — oscillators must be pointing at valid data
    // before any setFreq() / setShape() calls.
    _generateWavetables();

    // Assign table pointers to all oscillators.
    for (int i = 0; i < 6; i++) {
        _voices[i].setTables(_sineTable, _triTable, _sawTable,
                             _squareTable, _narrowPulseTable);
        _subVoices[i].setTables(_sineTable, _triTable, _sawTable,
                                _squareTable, _narrowPulseTable);
        // Update sample rate (harmless at default 32768, required for VCV).
        _voices[i].setSampleRate(audioRate);
        _subVoices[i].setSampleRate(audioRate);
        // Fixed square shape for sub oscillators.
        _subVoices[i].setShape(0.75f);

        // Poly envelopes
        _polyEnvArr[i].setSampleRate(audioRate);
        polyEnvs[i] = &_polyEnvArr[i];
        sPolyEnvs[i] = &_polyEnvArr[i];
    }

    // Envelope engines
    _arEnv.setSampleRate(audioRate);
    _adsrEnv.setSampleRate(audioRate);
    curveEng = &_arEnv;
    gCurveEng = &_arEnv;

    // Chorus — must run before the audio ISR starts.
    _chorus.init(audioRate);

    // Filter
    _filterType = FilterType::SVF;
    _prevFilterType = FilterType::SVF;
    filterInst = &_svfFilter;
    gFilterInst = &_svfFilter;

    // Reverb / delay
    reverb = &_dattorroReverb;

    // Initial voice frequencies
    for (int i = 0; i < 6; i++) {
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
void SynthEngine::setSampleRate(uint32_t audioRate) {
    _audioRate = audioRate;
    for (int i = 0; i < 6; i++) {
        _voices[i].setSampleRate(audioRate);
        _subVoices[i].setSampleRate(audioRate);
        _polyEnvArr[i].setSampleRate(audioRate);
    }
    _arEnv.setSampleRate(audioRate);
    _adsrEnv.setSampleRate(audioRate);
    _chorus.setSampleRate(audioRate);
    // Wavetables and filter coefficients are sample-rate independent; no
    // regeneration needed (filter coefficients are recomputed each tick anyway).
}

// ---------------------------------------------------------------------------
// SynthEngine::control()
// ---------------------------------------------------------------------------
void SynthEngine::control(const SynthParams &p, PolySlot polySlots[6],
                          SynthControlOutput &out) {
    // ------------------------------------------------------------------
    // One-pole smoothing — eliminates zipper noise on parameter changes
    // ------------------------------------------------------------------
    _sShape += (p.shape - _sShape) * 0.1f;
    _sFatness += (p.fatness - _sFatness) * 0.1f;
    _sMotion += (p.motion - _sMotion) * 0.05f; // slower: ramps drift/chorus
    _sCurve += (p.curve - _sCurve) * 0.1f;
    _sCurveTime += (p.curveTime - _sCurveTime) * 0.1f;
    _sVolume += (p.volume - _sVolume) * 0.1f;
    _sMidiVel += (p.midiVelocity - _sMidiVel) * 0.1f;
    _sSpace += (p.space - _sSpace) * 0.1f;
    _sRelation += (p.relation - _sRelation) * 0.1f;
    _sColor += (p.color - _sColor) * 0.08f;

    // ------------------------------------------------------------------
    // Envelope update — AR or ADSR
    // ------------------------------------------------------------------
    if (p.envelopeType == EnvelopeType::AR) {
        static_cast<AREnvelope<32768u> *>(curveEng)->setCurve(_sCurve, _sCurveTime);
    } else {
        // In ADSR mode, CURVE is a global time scale for A, D, R (sustain is
        // amplitude, not time, so it is unaffected).
        //   curve 0.0 → ×0.25  (tight / percussive)
        //   curve 0.5 → ×1.0   (knob values unchanged)
        //   curve 1.0 → ×4.0   (slow / pad-like)
        const float tScale = powf(4.0f, 2.0f * _sCurve - 1.0f);
        static_cast<ADSREnvelope<32768u> *>(curveEng)->setADSR(
            p.adsrAttack * tScale, p.adsrDecay * tScale, p.adsrSustain,
            p.adsrRelease * tScale, p.adsrLoop);
    }

    // ------------------------------------------------------------------
    // Gate edge detection → envelope + phase reset
    // ------------------------------------------------------------------
    {
        const bool curGate = p.gateHigh;
        if (p.voiceMode != VoiceMode::POLY && curGate != _prevGate) {
            curveEng->setGate(curGate);
            if (curGate && curveEng->level() < 0.01f) {
                for (int i = 0; i < 4; i++) {
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
    if (p.voiceMode == VoiceMode::PAIR || p.voiceMode == VoiceMode::CASCADE) {
        _sFmDepth = tanhf(_sColor * 3.0f * 0.7f) * kFmMaxScale;
    } else {
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
    if (p.glideEnabled && p.glideTime > 0.001f) {
        const float alpha = 1.0f - expf(-1.0f / (p.glideTime * (float)_controlRate));
        _sGlidedFreq += (p.baseFreq - _sGlidedFreq) * alpha;
    } else {
        _sGlidedFreq = p.baseFreq;
    }
    float voiceFreqs[4] = {_sGlidedFreq, _sGlidedFreq, _sGlidedFreq, _sGlidedFreq};

    // ------------------------------------------------------------------
    // Voice mode — frequency assignment + pan
    // ------------------------------------------------------------------
    _voiceMode = p.voiceMode;
    const bool modeChanged = (_voiceMode != _prevVoiceMode);
    _prevVoiceMode = _voiceMode;

    switch (p.voiceMode) {
    case VoiceMode::PAIR:
    default: {
        const float semitones = roundf(_sRelation);
        if (semitones != _cachedRelPair || modeChanged) {
            _cachedRatioPair = powf(2.0f, semitones / 12.0f);
            _cachedRelPair = semitones;
        }
        voiceFreqs[0] = _sGlidedFreq + _drift.offset(0);
        if (voiceFreqs[0] < 20.0f)
            voiceFreqs[0] = 20.0f;
        voiceFreqs[1] = _sGlidedFreq * _cachedRatioPair + _drift.offset(1);
        if (voiceFreqs[1] < 20.0f)
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
        _panL[0] = 256;
        _panL[1] = 0;
        _panL[2] = 0;
        _panL[3] = 0;
        _panR[0] = 0;
        _panR[1] = 256;
        _panR[2] = 0;
        _panR[3] = 0;
        break;
    }
    case VoiceMode::CHORD: {
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
        const int chordIdx = (_sRelation / 24.0f * 10.0f + 0.5f < 10.5f)
                                 ? (int)(_sRelation / 24.0f * 10.0f + 0.5f)
                                 : 10;
        if (chordIdx != _cachedChordIdx ||
            fabsf(_sGlidedFreq - _cachedChordBase) > 0.01f || modeChanged) {
            for (int i = 0; i < 4; i++)
                _cachedFreqsChord[i] = _sGlidedFreq *
                                       powf(2.0f, kChordTable[chordIdx][i] / 12.0f);
            _cachedChordIdx = chordIdx;
            _cachedChordBase = _sGlidedFreq;
        }
        for (int i = 0; i < 4; i++) {
            voiceFreqs[i] = _cachedFreqsChord[i] + _drift.offset(i);
            if (voiceFreqs[i] < 20.0f)
                voiceFreqs[i] = 20.0f;
            _voices[i].setFreq(voiceFreqs[i]);
            _voices[i].setShape(_sShape);
            const float sm = (p.subOctave == 2) ? 0.25f : 0.5f;
            _subVoices[i].setFreq(voiceFreqs[i] * sm);
        }
        if (_sColor > 0.001f) {
            static constexpr float kColorOff[4] = {-0.5f, -1.0f / 6.0f, 1.0f / 6.0f, 0.5f};
            const float colorHz = _sColor * 50.0f;
            for (int i = 0; i < 4; i++) {
                float f = voiceFreqs[i] + colorHz * kColorOff[i];
                if (f < 20.0f)
                    f = 20.0f;
                _voices[i].setFreq(f);
                const float sm = (p.subOctave == 2) ? 0.25f : 0.5f;
                _subVoices[i].setFreq(f * sm);
            }
        }
        _activeVoices = 4;
        _panL[0] = 128;
        _panL[1] = 90;
        _panL[2] = 38;
        _panL[3] = 0;
        _panR[0] = 0;
        _panR[1] = 38;
        _panR[2] = 90;
        _panR[3] = 128;
        break;
    }
    case VoiceMode::CLOUD: {
        if (fabsf(_sRelation - _cachedRelCloud) > 0.05f ||
            fabsf(_sGlidedFreq - _cachedBaseCloud) > 0.01f || modeChanged) {
            const float spreadCents = (_sRelation / 24.0f) * 50.0f;
            const float offCents[4] = {
                -spreadCents * 0.5f, -spreadCents * (1.0f / 6.0f),
                spreadCents * (1.0f / 6.0f), spreadCents * 0.5f};
            for (int i = 0; i < 4; i++)
                _cachedFreqsCloud[i] = _sGlidedFreq * powf(2.0f, offCents[i] / 1200.0f);
            _cachedRelCloud = _sRelation;
            _cachedBaseCloud = _sGlidedFreq;
        }
        static constexpr float kColorOff[4] = {-0.5f, -1.0f / 6.0f, 1.0f / 6.0f, 0.5f};
        for (int i = 0; i < 4; i++) {
            voiceFreqs[i] = _cachedFreqsCloud[i] + _drift.offset(i) * 1.5f + _sColor * 50.0f * kColorOff[i];
            if (voiceFreqs[i] < 20.0f)
                voiceFreqs[i] = 20.0f;
            _voices[i].setFreq(voiceFreqs[i]);
            _voices[i].setShape(_sShape);
            const float sm = (p.subOctave == 2) ? 0.25f : 0.5f;
            _subVoices[i].setFreq(voiceFreqs[i] * sm);
        }
        _activeVoices = 4;
        _panL[0] = 128;
        _panL[1] = 90;
        _panL[2] = 38;
        _panL[3] = 0;
        _panR[0] = 0;
        _panR[1] = 38;
        _panR[2] = 90;
        _panR[3] = 128;
        break;
    }
    case VoiceMode::CASCADE: {
        static constexpr float kCascadeRatios[] = {1.0f, 1.333f, 1.5f, 2.0f, 2.5f, 3.0f};
        const int zoneIdx = (_sRelation / 4.0f < 5.0f) ? (int)(_sRelation / 4.0f) : 5;
        voiceFreqs[0] = _sGlidedFreq + _drift.offset(0);
        if (voiceFreqs[0] < 20.0f)
            voiceFreqs[0] = 20.0f;
        voiceFreqs[1] = _sGlidedFreq * kCascadeRatios[zoneIdx] + _drift.offset(1);
        if (voiceFreqs[1] < 20.0f)
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
        _panL[0] = 256;
        _panL[1] = 0;
        _panL[2] = 0;
        _panL[3] = 0;
        _panR[0] = 256;
        _panR[1] = 0;
        _panR[2] = 0;
        _panR[3] = 0;
        break;
    }
    case VoiceMode::STRING: {
        if (fabsf(_sRelation - _cachedRelStr) > 0.05f ||
            fabsf(_sGlidedFreq - _cachedBaseStr) > 0.01f || modeChanged) {
            const float spreadCents = (_sRelation / 24.0f) * 30.0f;
            const float offCents[4] = {
                -spreadCents * 0.5f, -spreadCents * (1.0f / 6.0f),
                spreadCents * (1.0f / 6.0f), spreadCents * 0.5f};
            for (int i = 0; i < 4; i++)
                _cachedFreqsStr[i] = _sGlidedFreq * powf(2.0f, offCents[i] / 1200.0f);
            _cachedRelStr = _sRelation;
            _cachedBaseStr = _sGlidedFreq;
        }
        static constexpr float kColorOff[4] = {-0.5f, -1.0f / 6.0f, 1.0f / 6.0f, 0.5f};
        for (int i = 0; i < 4; i++) {
            voiceFreqs[i] = _cachedFreqsStr[i] + _drift.offset(i) * 3.0f + _sColor * 50.0f * kColorOff[i];
            if (voiceFreqs[i] < 20.0f)
                voiceFreqs[i] = 20.0f;
            _voices[i].setFreq(voiceFreqs[i]);
            _voices[i].setShape(_sShape);
            const float sm = (p.subOctave == 2) ? 0.25f : 0.5f;
            _subVoices[i].setFreq(voiceFreqs[i] * sm);
        }
        _activeVoices = 4;
        _panL[0] = 128;
        _panL[1] = 90;
        _panL[2] = 38;
        _panL[3] = 0;
        _panR[0] = 0;
        _panR[1] = 38;
        _panR[2] = 90;
        _panR[3] = 128;
        break;
    }
    case VoiceMode::POLY: {
        const float subMult = (p.subOctave == 2) ? 0.25f : 0.5f;
        static constexpr float kColorOff[6] = {-0.5f, -0.3f, -0.1f, 0.1f, 0.3f, 0.5f};
        const float polyColorHz = _sColor * 50.0f;
        _activeVoices = 6;
        // Equal-power normalisation: scale by 1/sqrt(sounding) so adding voices
        // keeps perceived loudness stable (uncorrelated oscillators sum at ~3 dB
        // per doubling) while still preventing accumulator clipping.
        int sounding = 0;
        for (int i = 0; i < 6; i++)
            if (_polyEnvArr[i].level() > 0.001f)
                sounding++;
        const float wScale = 256.0f / sqrtf((float)(sounding > 0 ? sounding : 1));
        for (int i = 0; i < 6; i++) {
            float f = polySlots[i].freq + _drift.offset(i) * 0.3f + polyColorHz * kColorOff[i];
            if (f < 20.0f)
                f = 20.0f;
            _voices[i].setFreq(f);
            _voices[i].setShape(_sShape);
            _polyEnvArr[i].setCurve(_sCurve, p.curveTime);
            _subVoices[i].setFreq((f * subMult < 20.0f) ? 20.0f : f * subMult);
            _subVoices[i].setShape(0.75f);
            const int16_t w = (int16_t)(wScale * polySlots[i].velocity);
            _panL[i] = w;
            _panR[i] = w;
        }
        break;
    }
    } // end switch

    // Mode transition cleanup
    if (modeChanged) {
        for (int i = 0; i < 6; i++) {
            _polyEnvArr[i].reset();
            sPolySlots[i].midiNote = 255;
        }
        sPolyRR = 0;
    }

    // Sub weight
    _sSubWf = _sFatness * 0.5f;

    // Chorus depth — STRING keeps a 0.3 minimum
    chorusDepth = (p.voiceMode == VoiceMode::STRING)
                      ? (_sMotion > 0.3f ? _sMotion : 0.3f)
                      : _sMotion;
    _chorusMode = p.chorusMode;

    // ------------------------------------------------------------------
    // Filter type switch
    // ------------------------------------------------------------------
    if (p.filterType != _prevFilterType) {
        filterInst->reset();
        filterInst = (p.filterType == FilterType::SVF)
                         ? static_cast<FilterEngine *>(&_svfFilter)
                         : static_cast<FilterEngine *>(&_otaLadder);
        gFilterInst = filterInst;
        _filterType = p.filterType;
        _prevFilterType = p.filterType;
    }
    {
        _sFilterCutoff += (p.filterCutoff - _sFilterCutoff) * 0.1f;
        _sFilterRes += (p.filterRes - _sFilterRes) * 0.1f;
        filterInst->setParams(_sFilterCutoff, _sFilterRes, p.filterMode);
    }

    // ------------------------------------------------------------------
    // Envelope type switch
    // ------------------------------------------------------------------
    if (p.envelopeType != _prevEnvType) {
        curveEng->reset();
        curveEng = (p.envelopeType == EnvelopeType::AR)
                       ? static_cast<EnvelopeEngine *>(&_arEnv)
                       : static_cast<EnvelopeEngine *>(&_adsrEnv);
        gCurveEng = curveEng;
        _envType = p.envelopeType;
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
    // Firmware (ARDUINO): reverb runs on Core 1, so parameter updates are
    // applied in main.cpp::loop1() to avoid Core 0/Core 1 races inside the
    // reverb object. VCV: single-threaded path keeps updates here.
#if !defined(ARDUINO)
    if (p.revSize != _prevRevSize || p.revDamping != _prevRevDamping) {
        reverb->setParams(p.revSize, p.revDamping);
        _prevRevSize = p.revSize;
        _prevRevDamping = p.revDamping;
    }
    if (p.revModSpeed != _prevRevModSpeed || p.revModDepth != _prevRevModDepth) {
        reverb->setModulation(p.revModSpeed, p.revModDepth);
        _prevRevModSpeed = p.revModSpeed;
        _prevRevModDepth = p.revModDepth;
    }
    if (p.revFrozen != _prevRevFrozen) {
        reverb->freeze(p.revFrozen);
        _prevRevFrozen = p.revFrozen;
    }
#endif

    // ------------------------------------------------------------------
    // Fill output snapshot for Core 1 bookkeeping
    // ------------------------------------------------------------------
    out.freq1 = voiceFreqs[0];
    out.freq2 = voiceFreqs[1];
    out.shape = _sShape;
    out.fatness = _sFatness;
    out.motion = _sMotion;
    out.curve = _sCurve;
    out.volume = _sVolume;
}

// ---------------------------------------------------------------------------
// SynthEngine::audio()
// ---------------------------------------------------------------------------
void SynthEngine::audio(int32_t revWetL, int32_t revWetR, float revMix, bool revEnabled,
                        int32_t *finalL, int32_t *finalR,
                        int32_t *dryForRevL, int32_t *dryForRevR) {
    const bool isPolyMode = (_voiceMode == VoiceMode::POLY);
    const bool isFmMode = (_voiceMode == VoiceMode::CASCADE ||
                           _voiceMode == VoiceMode::PAIR);

    // ------------------------------------------------------------------
    // Oscillator mix — sum sActiveVoices into L/R via pan weights.
    // FM modes: voice[1] PM-modulates voice[0].
    // POLY mode: per-voice envelopes applied inside the loop.
    // ------------------------------------------------------------------
    int32_t left = 0, right = 0;
    if (isFmMode) {
        const int16_t modSample = _voices[1].next();
        const int32_t sub1 = _subVoices[1].next();
        const int32_t pmOffset = (int32_t)((float)modSample * _sFmDepth);
        const int32_t carrier = _voices[0].nextPM(pmOffset);
        const int32_t sub0 = _subVoices[0].next();
        const int32_t m0 = (int32_t)((float)carrier + (float)sub0 * _sSubWf);
        const int32_t m1 = (int32_t)((float)modSample + (float)sub1 * _sSubWf);
        left = ((m0 * _panL[0]) + (m1 * _panL[1])) >> 8;
        right = ((m0 * _panR[0]) + (m1 * _panR[1])) >> 8;
    } else if (isPolyMode) {
        // Main oscillator is ALWAYS advanced to keep its phase accumulator live —
        // skipping next() freezes the phase at DC and causes silent/corrupt output
        // when a new note starts.  Sub-voice, multiply, and pan accumulation are
        // still skipped for silent voices (env < threshold) to save CPU.
        const bool hasSub = _sSubWf > 0.001f;
        for (uint8_t i = 0; i < 6; i++) {
            const float env = _polyEnvArr[i].next(); // direct call, no virtual dispatch
            const int32_t s = _voices[i].next();     // always advance phase
            if (env < 0.001f)
                continue; // skip expensive work only
            int32_t m;
            if (hasSub) {
                const int32_t sub = _subVoices[i].next();
                m = (int32_t)(((float)s + (float)sub * _sSubWf) * env);
            } else {
                m = (int32_t)((float)s * env);
            }
            left += (m * _panL[i]) >> 8;
            right += (m * _panR[i]) >> 8;
        }
    } else {
        // Non-FM non-POLY: pull sub-voice work out of the inner loop.
        if (_sSubWf > 0.001f) {
            for (uint8_t i = 0; i < _activeVoices; i++) {
                const int32_t s = _voices[i].next();
                const int32_t sub = _subVoices[i].next();
                const int32_t m = (int32_t)((float)s + (float)sub * _sSubWf);
                left += (m * _panL[i]) >> 8;
                right += (m * _panR[i]) >> 8;
            }
        } else {
            for (uint8_t i = 0; i < _activeVoices; i++) {
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
    if (left > 32767)
        left = 32767;
    else if (left < -32767)
        left = -32767;
    if (right > 32767)
        right = 32767;
    else if (right < -32767)
        right = -32767;

    // ------------------------------------------------------------------
    // VCA — envelope × volume × velocity, with de-click on downward moves
    // ------------------------------------------------------------------
    const float envLevel = (!isPolyMode && gGatePatched) ? curveEng->next() : 1.0f;
    const float gainTarget = _sVolume * _sMidiVel * envLevel;
    if (gainTarget < _sGainSmooth)
        _sGainSmooth += (gainTarget - _sGainSmooth) * 0.2f;
    else
        _sGainSmooth = gainTarget;
    left = (int32_t)((float)left * _sGainSmooth);
    right = (int32_t)((float)right * _sGainSmooth);

    // ------------------------------------------------------------------
    // Post-effects chain — two configurable positions
    // ------------------------------------------------------------------

    // [FILTER — PRE-CHORUS]
    if (!_fxOrder.filterPostChorus)
        filterInst->process(left, right, &left, &right);

    // Chorus
    {
        int32_t outL, outR;
        _chorus.process(left, right, chorusDepth, _chorusMode, &outL, &outR);
        left = outL;
        right = outR;
    }

    // [FILTER — POST-CHORUS]
    if (_fxOrder.filterPostChorus)
        filterInst->process(left, right, &left, &right);

    // [DELAY — PRE-REVERB]
    if (!_fxOrder.delayPostReverb)
        _delay.process(left, right, &left, &right);

    // Export pre-reverb signal for Core 1
    *dryForRevL = left;
    *dryForRevR = right;

    // Mix reverb wet return
    if (revEnabled) {
        left += (int32_t)((float)revWetL * revMix);
        right += (int32_t)((float)revWetR * revMix);
        // Hard-limit to prevent int16 wrapping crackle when passed to from16Bit.
        // The previous Padé soft-clip caused ~6-7% continuous non-linear distortion
        // at typical reverb tail levels (s≈0.4-0.6), adding harmonics to the tail
        // and producing audible shimmer. A hard-limit is fully transparent at all
        // levels up to ±32767 and only activates at simultaneous dry+wet peaks.
        if (left > 32767)
            left = 32767;
        else if (left < -32767)
            left = -32767;
        if (right > 32767)
            right = 32767;
        else if (right < -32767)
            right = -32767;
    }

    // [DELAY — POST-REVERB]
    if (_fxOrder.delayPostReverb)
        _delay.process(left, right, &left, &right);

    // SPACE — stereo width
    if (_sSpace < 0.995f || _sSpace > 1.005f) {
        int32_t spL, spR;
        SpaceEngine::process(left, right, _sSpace, &spL, &spR);
        left = spL;
        right = spR;
    }

    *finalL = left;
    *finalR = right;
}

// ---------------------------------------------------------------------------
// Wavetable generation (extracted verbatim from main.cpp)
// ---------------------------------------------------------------------------
void SynthEngine::_normaliseTable(const float *buf, int16_t *dst, int n) {
    float peak = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(buf[i]) > peak)
            peak = fabsf(buf[i]);
    if (peak < 1e-6f)
        peak = 1.0f;
    const float scale = 32767.0f / peak;
    for (int i = 0; i < n; i++)
        dst[i] = (int16_t)(buf[i] * scale);
}

void SynthEngine::_generateWavetables() {
    static float buf[TABLE_CELLS]; // static: avoids 4 KB stack frame
    const int N = (int)TABLE_CELLS;
    const int maxH = ((int)_audioRate / 2) / 440; // 37 @ 32768 Hz

    // Sine
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * 3.14159265f * i / N;
        buf[i] = sinf(phase);
    }
    _normaliseTable(buf, _sineTable, N);

    // Triangle
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * 3.14159265f * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h += 2) {
            const float x = (float)h * 3.14159265f / (maxH + 1);
            const float sigma = sinf(x) / x;
            const float sign = (((h - 1) / 2) & 1) ? -1.0f : 1.0f;
            val += sigma * sign * sinf((float)h * phase) / ((float)h * h);
        }
        buf[i] = val;
    }
    _normaliseTable(buf, _triTable, N);

    // Sawtooth
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * 3.14159265f * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h++) {
            const float x = (float)h * 3.14159265f / (maxH + 1);
            const float sigma = sinf(x) / x;
            val += sigma * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    _normaliseTable(buf, _sawTable, N);

    // Square / 50% pulse
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * 3.14159265f * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h += 2) {
            const float x = (float)h * 3.14159265f / (maxH + 1);
            const float sigma = sinf(x) / x;
            val += sigma * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    _normaliseTable(buf, _squareTable, N);

    // Hollow / 25% duty-cycle pulse
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * 3.14159265f * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h++) {
            const float x = (float)h * 3.14159265f / (maxH + 1);
            const float sigma = sinf(x) / x;
            const float duty = sinf((float)h * 3.14159265f * 0.25f);
            val += sigma * duty * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    _normaliseTable(buf, _narrowPulseTable, N);
}
