/**
 * AlloyFlux — Dual Relation Oscillator, Juno-inspired Eurorack voice
 * Hardware : Raspberry Pi Pico 2 (RP2350) + PCM5102A I2S DAC
 *
 * Audio: stereo I2S via PIO, 16-bit, 32768 Hz — see platformio.ini for pin assignments.
 * Serial dev console active when SERIAL_CONTROL is defined (build flag).
 * See reference/AlloyFlux-module-reference.md for full design specification.
 */

// ---------------------------------------------------------------------------
// Mozzi configuration — must precede all Mozzi includes
// ---------------------------------------------------------------------------
#include "MozziConfigValues.h"

#define MOZZI_AUDIO_MODE MOZZI_OUTPUT_I2S_DAC
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
#define MOZZI_AUDIO_BITS 16
#define MOZZI_AUDIO_RATE 32768
#define MOZZI_CONTROL_RATE 128

// ---------------------------------------------------------------------------
// Mozzi includes
// ---------------------------------------------------------------------------
#include "ChorusEngine.h"
#include "ShapeOsc.h"
#include "SpaceEngine.h"
#include "VoiceMode.h"
#include <Mozzi.h>
#include <math.h>
#include <tables/sin2048_int8.h>

// ---------------------------------------------------------------------------
// Band-limited wavetables — all generated at startup via additive synthesis.
// Lanczos sigma factor: sigma(h) = sinc(h*pi/(maxH+1)) kills Gibbs ringing.
// maxH = floor(AUDIO_RATE/2 / 440) = 37 @ 32768 Hz — band-limited for 440 Hz+.
//
// SHAPE spectrum across the five tables (0.0 → 1.0):
//   sine      triangle     saw     pulse(50%)   hollow(25%)
// ---------------------------------------------------------------------------
static int8_t gTriTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];
static int8_t gSawTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];
static int8_t gSquareTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];
static int8_t gNarrowPulseTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];

static void normaliseTable(const float *buf, int8_t *dst, int n) {
    float peak = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(buf[i]) > peak)
            peak = fabsf(buf[i]);
    if (peak < 1e-6f)
        peak = 1.0f;
    const float scale = 127.0f / peak;
    for (int i = 0; i < n; i++)
        dst[i] = (int8_t)(buf[i] * scale);
}

static void generateWavetables() {
    static float buf[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS]; // static: avoids 8 KB stack frame
    const int N = ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS;
    const int maxH = (MOZZI_AUDIO_RATE / 2) / 440; // 37 @ 32768 Hz

    // --- Triangle: odd harmonics, alternating sign, 1/h² rolloff ---
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * (float)M_PI * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h += 2) {
            const float x = (float)h * (float)M_PI / (maxH + 1);
            const float sigma = sinf(x) / x;
            const float sign = (((h - 1) / 2) & 1) ? -1.0f : 1.0f;
            val += sigma * sign * sinf((float)h * phase) / ((float)h * h);
        }
        buf[i] = val;
    }
    normaliseTable(buf, gTriTable, N);

    // --- Saw: all harmonics, 1/h rolloff ---
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * (float)M_PI * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h++) {
            const float x = (float)h * (float)M_PI / (maxH + 1);
            const float sigma = sinf(x) / x;
            val += sigma * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    normaliseTable(buf, gSawTable, N);

    // --- Square / Pulse 50%: odd harmonics only, 1/h rolloff ---
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * (float)M_PI * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h += 2) {
            const float x = (float)h * (float)M_PI / (maxH + 1);
            const float sigma = sinf(x) / x;
            val += sigma * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    normaliseTable(buf, gSquareTable, N);

    // --- Hollow pulse 25% duty: all harmonics weighted by sin(h*pi/4)/h ---
    // Every 4th harmonic (h=4,8,12...) is nulled; h=2,6,10 are phase-inverted.
    // This gives the characteristic hollow/nasal 25%-duty-cycle tone.
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * (float)M_PI * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h++) {
            const float x = (float)h * (float)M_PI / (maxH + 1);
            const float sigma = sinf(x) / x;
            const float duty = sinf((float)h * (float)M_PI * 0.25f);
            val += sigma * duty * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
    }
    normaliseTable(buf, gNarrowPulseTable, N);
}

// ---------------------------------------------------------------------------
// Module includes
// ---------------------------------------------------------------------------
#include "CurveEngine.h"
#include "DriftEngine.h"
#include "debug.h"
#include "dsp_shared.h"
#include "params.h"
#include "serial_console.h"

// ---------------------------------------------------------------------------
// Oscillators: Voice 1 (L) + Voice 2 (R) — 5-shape morphing wavetable oscillators
// Sub oscillators: one octave down, fixed square shape, level controlled by FATNESS
// ---------------------------------------------------------------------------
static ShapeOsc<MOZZI_AUDIO_RATE> v1(SIN2048_DATA, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable);
static ShapeOsc<MOZZI_AUDIO_RATE> v2(SIN2048_DATA, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable);
static ShapeOsc<MOZZI_AUDIO_RATE> subv1(SIN2048_DATA, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable);
static ShapeOsc<MOZZI_AUDIO_RATE> subv2(SIN2048_DATA, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable);

// ---------------------------------------------------------------------------
// Shared synthesis parameters (declared extern in params.h)
// ---------------------------------------------------------------------------
float gBaseFreq = 440.0f;
float gDetune = 0.0f;
float gShape = 0.0f;
float gFatness = 0.4f;                  // default: sub audible but not boomy
float gMotion = 0.0f;                   // 0.0 = static  …  1.0 = full drift
float gDriftSpeed = 0.04f;              // one-pole glide coeff: 0.001–0.10
VoiceMode gVoiceMode = VoiceMode::PAIR; // synthesis personality (default: PAIR)
float gRelation = 0.0f;                 // 0.0 = unison, 1.0 = +2 octaves (PAIR mode)
float gCurve = 0.5f;                    // 0.0 = pluck, 0.5 = natural, 1.0 = swell
float gCurveTime = 1.0f;                // overall envelope time scale (0.25–4.0)
volatile bool gGateHigh = false;        // true while gate is asserted
volatile bool gGatePatched = false;     // false = drone (bypass VCA)
float gVolume = 0.8f;
ChorusMode gChorusMode = ChorusMode::I_II; // default: Juno I+II (maximum stereo spread)
float gSpace = 1.0f;                       // stereo width: 0.0 = mono, 1.0 = full stereo

// Smoothed values — consumed by updateAudio(), updated in updateControl()
static float sShape = 0.0f;
static float sFatness = 0.4f;
static float sMotion = 0.0f;    // slower smoother for drift/chorus depth
static float sCurve = 0.5f;     // smoothed CURVE value for CurveEngine
static float sCurveTime = 1.0f; // smoothed time scale
static float sVolume = 0.8f;
static float sSpace = 1.0f;
static float sRelation = 0.0f; // smoothed interval ratio input
// Pre-scaled integer sub weight for the audio hot path (0..128 = 0..50% of main).
// Computed once per control cycle; 32-bit aligned so ISR reads are atomic.
static int32_t sSubW = 51;

// Drift engine: per-voice slow frequency random-walk (Milestone 11)
static DriftEngine<2> gDrift;

// Curve engine: AR envelope + VCA (Milestone 15)
static CurveEngine<MOZZI_AUDIO_RATE> gCurveEng;

// Chorus engine: phasor-LFO BBD-inspired stereo chorus (Milestone 12).
// Declared here so updateAudio() can call it; 8 KB delay buffers live in BSS.
static ChorusEngine<MOZZI_AUDIO_RATE> gChorus;

// Trig pulse timer — set by cmd_trig, cleared in updateControl() when elapsed.
// 0 means no trig pending.
uint32_t sTrigReleaseAt = 0;

// CPU profiling counters (Milestone 8) — compiled out when CPU_PROFILE is not set.
#ifdef CPU_PROFILE
volatile bool gPerformancePrintEnabled = false;
volatile uint32_t gAudioElapsedUs = 0;
volatile uint32_t gAudioOverruns = 0;
#endif

// ---------------------------------------------------------------------------
// Inter-core shared state (Milestone 9) — see include/dsp_shared.h
// ---------------------------------------------------------------------------
DspParams gDsp = {};
mutex_t gDspMutex;

// Chorus depth — written by updateControl() at 128 Hz, read atomically by ISR.
volatile float gChorusDepth = 0.0f;

// ---------------------------------------------------------------------------
// Mozzi callbacks
// ---------------------------------------------------------------------------

void setup() {
    serialConsole_init();
    mutex_init(&gDspMutex);
    generateWavetables();
    gChorus.init(); // must run before startMozzi() to fill delay buffers before first ISR
    startMozzi();
    v1.setFreq(gBaseFreq);
    v2.setFreq(gBaseFreq);
    // Sub oscillators: fixed square shape (0.75), one octave below main freq
    subv1.setShape(0.75f);
    subv2.setShape(0.75f);
    subv1.setFreq(gBaseFreq * 0.5f);
    subv2.setFreq(gBaseFreq * 0.5f);
    serialConsole_ready();
#ifdef CPU_PROFILE
    // Clear any overruns that occurred during Mozzi's startup DMA/PIO init —
    // they are not representative of steady-state audio performance.
    gAudioOverruns = 0;
#endif
}

void updateControl() {
    serialConsole_update();

    // Auto-release for cmd_trig: lower gate when the pulse duration has elapsed.
    if (sTrigReleaseAt && millis() >= sTrigReleaseAt) {
        gGateHigh = false;
        sTrigReleaseAt = 0;
    }

    // One-pole smoothing — eliminates zipper noise on parameter changes
    sShape += (gShape - sShape) * 0.1f;
    sFatness += (gFatness - sFatness) * 0.1f;
    sMotion += (gMotion - sMotion) * 0.05f; // slower: drift/chorus ramps gracefully
    sCurve += (gCurve - sCurve) * 0.1f;
    sCurveTime += (gCurveTime - sCurveTime) * 0.1f;
    sVolume += (gVolume - sVolume) * 0.1f;
    sSpace += (gSpace - sSpace) * 0.1f;
    sRelation += (gRelation - sRelation) * 0.1f;

    // Update CURVE engine: recompute A/R coefficients and forward gate edges.
    gCurveEng.setCurve(sCurve, sCurveTime);
    {
        static bool prevGate = false;
        const bool curGate = gGateHigh;
        if (curGate != prevGate) {
            gCurveEng.setGate(curGate);
            prevGate = curGate;
        }
    }

    // Apply per-voice drift offsets; sub oscillators track their main automatically.
    // PAIR mode: voice 2 is offset by gRelation semitones above ROOT (0=unison, 12=octave).
    // gDetune adds a symmetric Hz fine-spread on both voices.
    // powf() only recomputed when sRelation changes meaningfully; never called in the ISR.
    gDrift.setSpeed(gDriftSpeed);
    gDrift.update(sMotion);
    float freq1, freq2;
    switch (gVoiceMode) {
    case VoiceMode::PAIR:
    default: {
        // ratio = 2^(semitones/12) — standard equal-temperament semitone-to-ratio
        // Cache result — powf is expensive on first call (flash miss); recompute only on change.
        static float cachedRelation = -1.0f;
        static float cachedRatio2 = 1.0f;
        if (fabsf(sRelation - cachedRelation) > 0.005f) {
            cachedRatio2 = powf(2.0f, sRelation / 12.0f);
            cachedRelation = sRelation;
        }
        freq1 = max(gBaseFreq - gDetune * 0.5f + gDrift.offset(0), 20.0f);
        freq2 = max(gBaseFreq * cachedRatio2 + gDetune * 0.5f + gDrift.offset(1), 20.0f);
        break;
    }
    }

    v1.setFreq(freq1);
    v2.setFreq(freq2);
    subv1.setFreq(freq1 * 0.5f);
    subv2.setFreq(freq2 * 0.5f);
    v1.setShape(sShape);
    v2.setShape(sShape);
    // sSubW: 0..128 maps fatness 0..1 to sub contributing 0..50% of main amplitude.
    // Written here (Core 0 control rate), read in updateAudio() ISR — atomic on M33.
    sSubW = (int32_t)(sFatness * 128.0f);

    // Chorus depth — single float, atomic on M33, no mutex needed.
    gChorusDepth = sMotion;

    // Publish smoothed params for Core 1 DSP engines (chorus, drift — Milestones 12+).
    mutex_enter_blocking(&gDspMutex);
    gDsp.freq1 = freq1;
    gDsp.freq2 = freq2;
    gDsp.shape = sShape;
    gDsp.fatness = sFatness;
    gDsp.motion = sMotion;
    gDsp.curve = sCurve;
    gDsp.volume = sVolume;
    mutex_exit(&gDspMutex);

#if defined(CPU_PROFILE) && defined(SERIAL_CONTROL)
    // Print audio ISR timing once every 5 s so it doesn't flood the console.
    static uint32_t lastCpuReport = 0;
    const uint32_t now = millis();
    if (now - lastCpuReport >= 5000) {
        lastCpuReport = now;
        const uint32_t us = gAudioElapsedUs;
        if (gPerformancePrintEnabled) {
            float headroom = (30.0f - (float)us) / 30.0f * 100.0f;
            Serial.print(F("[cpu] "));
            Serial.print(us);
            Serial.print(F("us/30us  headroom "));
            Serial.print(headroom, 1);
            Serial.print(F("%  overruns "));
            Serial.println(gAudioOverruns);
        }
    }
#endif
}

AudioOutput updateAudio() {
#ifdef CPU_PROFILE
    const uint32_t _t0 = time_us_32();
#endif

    // Main oscillators: full 5-shape morph
    int32_t s1 = v1.next();
    int32_t s2 = v2.next();

    // Sub oscillators: fixed square (shape=0.75), one octave below, mixed at sSubW/256
    // sSubW 0..128 → sub adds 0..50% of ±32512 range (matches Juno sub fader range)
    int32_t sub1 = subv1.next();
    int32_t sub2 = subv2.next();
    int32_t m1 = s1 + ((sub1 * sSubW) >> 8);
    int32_t m2 = s2 + ((sub2 * sSubW) >> 8);

    // Soft clip: cap at ±32512 before volume scaling to prevent from16Bit wrap
    if (m1 > 32512)
        m1 = 32512;
    else if (m1 < -32512)
        m1 = -32512;
    if (m2 > 32512)
        m2 = 32512;
    else if (m2 < -32512)
        m2 = -32512;

    // CURVE envelope VCA (Milestone 15): bypassed in drone mode (no gate patched).
    // Volume and envelope are combined into a single integer multiply for efficiency.
    const float envLevel = gGatePatched ? gCurveEng.next() : 1.0f;
    int32_t scale = (int32_t)(sVolume * envLevel * 256.0f);
    int32_t left = (m1 * scale) >> 8;
    int32_t right = (m2 * scale) >> 8;

    // Chorus — runs here in Core 0 ISR using phasor LFO (~50 cycles, no trig).
    // gChorusDepth and gChorusMode are written by updateControl() at 128 Hz;
    // single 32-bit aligned reads are atomic on Cortex-M33, no mutex needed.
    int32_t outL, outR;
    gChorus.process(left, right, gChorusDepth, gChorusMode, &outL, &outR);

    // SPACE — mid-side stereo width (Milestone 13). sSpace=1.0 is identity.
    // 0.0=mono, 1.0=identity, 2.0=hyper-wide. Bypass guard skips processing at ~1.0.
    if (sSpace < 0.995f || sSpace > 1.005f) {
        int32_t spaceL, spaceR;
        SpaceEngine::process(outL, outR, sSpace, &spaceL, &spaceR);
        outL = spaceL;
        outR = spaceR;
    }

#ifdef CPU_PROFILE
    const uint32_t elapsed = time_us_32() - _t0;
    gAudioElapsedUs = elapsed;
    if (elapsed > 30)
        gAudioOverruns++;
#endif

    return StereoOutput::from16Bit(outL, outR);
}

void loop() {
    audioHook();
}

// ---------------------------------------------------------------------------
// Core 1 — reserved for future DSP offload (reverb, filter — M26+)
// ---------------------------------------------------------------------------

void setup1() {
    // Core 1 reserved for future DSP offload (reverb, filter — M26+).
    // Chorus runs on Core 0 ISR; nothing needed here yet.
}

void loop1() {
    // Nothing here until a heavier DSP engine warrants offload.
    tight_loop_contents();
}
