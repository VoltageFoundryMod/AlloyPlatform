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
#include "VoiceMode.h"
#include "config_store.h"
#include "dsp/ChorusEngine.h"
#include "dsp/ShapeOsc.h"
#include "dsp/SpaceEngine.h"
#include "io/ButtonEngine.h"
#include "io/usb_midi.h"
#include <Mozzi.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Button pin assignments (Milestone 31)
// ---------------------------------------------------------------------------
#define PIN_BUTTON_MODE 10  // GP10 — mode cycle button
#define PIN_BUTTON_SHIFT 11 // GP11 — shift button (secondary pot functions + combo actions)

// ---------------------------------------------------------------------------
// Band-limited wavetables — all generated at startup via additive synthesis.
// Lanczos sigma factor: sigma(h) = sinc(h*pi/(maxH+1)) kills Gibbs ringing.
// maxH = floor(AUDIO_RATE/2 / 440) = 37 @ 32768 Hz — band-limited for 440 Hz+.
//
// SHAPE spectrum across the five tables (0.0 → 1.0):
//   sine      triangle     saw     pulse(50%)   hollow(25%)
// ---------------------------------------------------------------------------
static int16_t gSineTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];
static int16_t gTriTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];
static int16_t gSawTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];
static int16_t gSquareTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];
static int16_t gNarrowPulseTable[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS];

static void normaliseTable(const float *buf, int16_t *dst, int n) {
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

static void generateWavetables() {
    static float buf[ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS]; // static: avoids 8 KB stack frame
    const int N = ShapeOsc<MOZZI_AUDIO_RATE>::TABLE_CELLS;
    const int maxH = (MOZZI_AUDIO_RATE / 2) / 440; // 37 @ 32768 Hz

    // --- Sine: fundamental only (no harmonics needed) ---
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * (float)M_PI * i / N;
        buf[i] = sinf(phase);
    }
    normaliseTable(buf, gSineTable, N);

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
#include "debug.h"
#include "dsp/CurveEngine.h"
#include "dsp/DattorroReverb.h"
#include "dsp/DelayEngine.h"
#include "dsp/DriftEngine.h"
#include "dsp/FilterEngine.h"
#include "dsp/FxChain.h"
#include "dsp/OTALadder.h"
#include "dsp/ReverbEngine.h"
#include "dsp/SVFFilter.h"
#include "dsp_shared.h"
#include "io/serial_console.h"
#include "params.h"
// No longer using SIN2048_DATA (int8_t) — replaced by gSineTable (int16_t)
// for 96dBFS noise floor vs the 48dBFS of the 8-bit Mozzi table.

// ---------------------------------------------------------------------------
// Oscillators: 4 main voices + 4 sub voices (one octave down, fixed square).
// PAIR uses voices[0]+[1]; CHORD/CLOUD use all four. sActiveVoices controls
// how many are summed in updateAudio() to save ISR budget in 2-voice modes.
// ---------------------------------------------------------------------------
static ShapeOsc<MOZZI_AUDIO_RATE> voices[4] = {
    {gSineTable, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable},
    {gSineTable, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable},
    {gSineTable, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable},
    {gSineTable, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable},
};
static ShapeOsc<MOZZI_AUDIO_RATE> subVoices[4] = {
    {gSineTable, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable},
    {gSineTable, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable},
    {gSineTable, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable},
    {gSineTable, gTriTable, gSawTable, gSquareTable, gNarrowPulseTable},
};

// ---------------------------------------------------------------------------
// Shared synthesis parameters (declared extern in params.h)
// ---------------------------------------------------------------------------
float gBaseFreq = 440.0f;
float gDetune = 0.0f;
float gShape = 0.0f;
float gFatness = 0.4f;                  // default: sub audible but not boomy
uint8_t gSubOctave = 1;                 // 1 = one octave below (×0.5), 2 = two octaves below (×0.25)
float gMotion = 0.0f;                   // 0.0 = static  …  1.0 = full drift
float gDriftSpeed = 0.04f;              // one-pole glide coeff: 0.001–0.10
VoiceMode gVoiceMode = VoiceMode::PAIR; // synthesis personality (default: PAIR)
float gRelation = 0.0f;                 // 0.0 = unison, 1.0 = +2 octaves (PAIR mode)
float gCurve = 0.5f;                    // 0.0 = pluck, 0.5 = natural, 1.0 = swell
float gCurveTime = 1.0f;                // overall envelope time scale (0.25–4.0)
volatile bool gGateHigh = false;        // true while gate is asserted
volatile bool gGatePatched = false;     // false = drone (bypass VCA)
float gVolume = 1.0f;
float gMidiVelocity = 1.0f;                // set by MIDI Note On; 1.0 for CV / drone / button sources
ChorusMode gChorusMode = ChorusMode::I_II; // default: Juno I+II (maximum stereo spread)
float gSpace = 1.0f;                       // stereo width: 0.0 = mono, 1.0 = full stereo
uint8_t gMidiChannel = 0;                  // 0 = omni, 1–16 = specific MIDI channel

// Smoothed values — consumed by updateAudio(), updated in updateControl()
static float sShape = 0.0f;
static float sFatness = 0.4f;
static float sMotion = 0.0f;    // slower smoother for drift/chorus depth
static float sCurve = 0.5f;     // smoothed CURVE value for CurveEngine
static float sCurveTime = 1.0f; // smoothed time scale
static float sVolume = 0.8f;
static float sMidiVelocity = 1.0f;
static float sSpace = 1.0f;
static float sRelation = 0.0f; // smoothed interval ratio input
// Sub oscillator mix weight for the audio hot path (0.0..0.5 = 0..50% of main).
// Float: same cost as integer on M33 FPU; avoids 128-step quantization zipper.
// 32-bit aligned float — ISR reads are atomic on Cortex-M33.
static float sSubWf = 0.2f; // initial = gFatness(0.4) * 0.5

// Drift engine: per-voice slow frequency random-walk (Milestone 11)
static DriftEngine<4> gDrift;

// Per-voice stereo pan weights (×256 fixed-point), updated each updateControl().
// sum(sPanL) = sum(sPanR) = 256 — keeps per-channel output within ±32512.
// PAIR: voice 0 → L, voice 1 → R.  CHORD: hard-L, soft-L, soft-R, hard-R.
static uint8_t sActiveVoices = 2;
static int16_t sPanL[4] = {256, 0, 0, 0};
static int16_t sPanR[4] = {0, 256, 0, 0};

// Envelope engine: AR (default) or ADSR (runtime-selectable, M5x).
// gCurveEng is the pointer used everywhere; sArEnv / sAdsrEnv are the
// concrete instances — only one is active at a time.  Switch by pointing
// gCurveEng at the other and calling reset().
static AREnvelope<MOZZI_AUDIO_RATE> sArEnv;
static ADSREnvelope<MOZZI_AUDIO_RATE> sAdsrEnv;
EnvelopeEngine *gCurveEng = &sArEnv; // default: single-knob AR
EnvelopeType gEnvelopeType = EnvelopeType::AR;

// Chorus engine: phasor-LFO BBD-inspired stereo chorus (Milestone 12).
// Declared here so updateAudio() can call it; 8 KB delay buffers live in BSS.
static ChorusEngine<MOZZI_AUDIO_RATE> gChorus;

// Trig pulse timer — set by cmd_trig, cleared in updateControl() when elapsed.
// 0 means no trig pending.
uint32_t sTrigReleaseAt = 0;

// Button engines (Milestone 31) — polled at 128 Hz in updateControl().
static ButtonEngine gBtnMode(PIN_BUTTON_MODE);   // mode cycle
static ButtonEngine gBtnShift(PIN_BUTTON_SHIFT); // shift / combo
// Set to true whenever SHIFT is consumed by a combo or knob action so the
// trig-on-release is suppressed. Reset automatically on SHIFT release.
static bool sShiftConsumed = false;

// ---------------------------------------------------------------------------
// M26 Post-effects engines and parameters
// ---------------------------------------------------------------------------

// Filter (M26a / M5x) — runtime-selectable algorithm.
// gFilterInst is the active pointer; sSvfFilter / sOtaLadder are the
// concrete instances.  Switch with cmd_filter_type in commands.cpp.
static SVFFilter sSvfFilter;
static OTALadder sOtaLadder;
FilterEngine *gFilterInst = &sSvfFilter; // default: clean Cytomic SVF
FilterType gFilterType = FilterType::SVF;
float gFilterCutoff = 8000.0f;
float gFilterRes = 0.0f;
FilterMode gFilterMode = FilterMode::OFF;
// ADSR envelope params (used when gEnvelopeType == ADSR)
float gAdsrAttack = 0.05f;  // seconds
float gAdsrDecay = 0.10f;   // seconds
float gAdsrSustain = 0.8f;  // 0.0–1.0
float gAdsrRelease = 0.30f; // seconds
bool gAdsrLoop = false;

// Effect ordering (M26a) — 2 bool flags → 4 chain orderings.
FxOrder gFxOrder = {false, false}; // filter pre-chorus, delay pre-reverb

// Reverb (M26b) — Dattorro plate algorithm runs on Core 1.
// Core 0 ISR → gRevIn → Core 1 → gRevOut → Core 0 ISR (1-frame latency, inaudible).
static DattorroReverb sDattorroReverb;
ReverbEngine *gReverb = &sDattorroReverb;
volatile int32_t gRevIn_L = 0;
volatile int32_t gRevIn_R = 0;
volatile int32_t gRevOut_L = 0;
volatile int32_t gRevOut_R = 0;
volatile float gRevMix = 0.35f;
volatile bool gRevEnabled = false;
// Sequence counter: Core 0 ISR increments after writing gRevIn each sample.
// Core 1 spins until seq changes, then processes exactly once — prevents
// the Dattorro algorithm from being called ~4500x on the same stale sample.
volatile uint32_t gRevSampleSeq = 0;
float gRevSize = 0.5f;
float gRevDamping = 0.5f;
float gRevModSpeed = 1.0f; // M40: LFO rate multiplier (0.1–4.0)
float gRevModDepth = 1.0f; // M40: LFO amplitude multiplier (0.0–1.0)
bool gRevFrozen = false;   // M41: infinite sustain when true

// Delay (M26c) — ping-pong stereo delay; pass-through stub until ring buffer lands.
static DelayEngine gDelay;
float gDelayTime = 100.0f;
float gDelayFeedback = 0.5f;
float gDelayMix = 0.0f;

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
    // Enable Flush-to-Zero mode on Core 0's FPU.
    // Denormal floats (values < ~1.2e-38) cause ~100x slower FPU ops on Cortex-M33;
    // with FTZ they flush to zero instead — inaudible and prevents ISR overruns.
    {
        uint32_t fpscr;
        asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1u << 24); // FZ: Flush-to-Zero
        asm volatile("vmsr fpscr, %0" ::"r"(fpscr));
    }
    // appear in the same USB descriptor on first host enumeration.
    // Adafruit_USBD_MIDI::begin() triggers a disconnect/reconnect; the
    // serialConsole_init() wait loop below catches that reconnect cleanly.
#ifdef USE_TINYUSB
    usbMidi_init();
#endif
    serialConsole_init();
    // Load persisted config from flash before Mozzi starts so all gXxx
    // globals are at their saved values when the first updateControl() runs.
    configStore_load(); // silently uses compile-time defaults if no valid config found
    mutex_init(&gDspMutex);
    generateWavetables();
    // Pre-warm powf() and its flash-resident math tables so the first CHORD or PAIR
    // updateControl() call doesn't cause cold-cache flash misses that spike the ISR.
    volatile float _pw = powf(2.0f, 7.0f / 12.0f);
    (void)_pw;
    gChorus.init(); // must run before startMozzi() to fill delay buffers before first ISR
    gBtnMode.begin();
    gBtnShift.begin();
    startMozzi();
    for (uint8_t i = 0; i < 4; i++) {
        voices[i].setFreq(gBaseFreq);
        subVoices[i].setShape(0.75f); // fixed square shape for all sub oscillators
        subVoices[i].setFreq(gBaseFreq * ((gSubOctave == 2) ? 0.25f : 0.5f));
    }
    serialConsole_ready();
#ifdef CPU_PROFILE
    // Clear any overruns that occurred during Mozzi's startup DMA/PIO init —
    // they are not representative of steady-state audio performance.
    gAudioOverruns = 0;
#endif
}

void updateControl() {
    serialConsole_update();
#ifdef USE_TINYUSB
    usbMidi_update();
#endif

    // -----------------------------------------------------------------------
    // Button polling (Milestone 31) — 128 Hz, ~31 ms debounce window.
    // Poll both buttons before acting so isDown() reflects the same tick.
    // -----------------------------------------------------------------------
    gBtnMode.poll();
    gBtnShift.poll();

    // Mode + Shift held simultaneously → return to drone mode (once per combo).
    // A static flag prevents repeated firings while both are held.
    // sShiftConsumed (file-scope): set by any combo or shift+knob handler so the
    // trig on release is suppressed. Reset each time SHIFT is released.
    {
        static bool sDroneComboFired = false;
        if (gBtnMode.isDown() && gBtnShift.isDown()) {
            if (!sDroneComboFired) {
                sDroneComboFired = true;
                sShiftConsumed = true; // don't trig on SHIFT release
                gGatePatched = false;
                gGateHigh = false;
                gMidiVelocity = 1.0f; // restore full volume when returning to drone/CV
#ifdef SERIAL_CONTROL
                Serial.println(F("gate -> free (drone)"));
#endif
            }
        } else {
            sDroneComboFired = false;
            // Mode solo (Shift not held): cycle voice mode.
            if (gBtnMode.pressed()) {
                // Cycle through implemented voice modes only.
                // Add to kActiveModes[] as each milestone lands.
                static const VoiceMode kActiveModes[] = {VoiceMode::PAIR, VoiceMode::CHORD};
                static constexpr uint8_t kN = sizeof(kActiveModes) / sizeof(kActiveModes[0]);
                uint8_t idx = 0;
                for (uint8_t i = 0; i < kN; i++) {
                    if (kActiveModes[i] == gVoiceMode) {
                        idx = i;
                        break;
                    }
                }
                gVoiceMode = kActiveModes[(idx + 1) % kN];
#ifdef SERIAL_CONTROL
                Serial.print(F("mode -> "));
                Serial.println(voiceModeName(gVoiceMode));
#endif
            }
            // Shift solo: trig fires on RELEASE (not press) so holding SHIFT for
            // a combo or future SHIFT+pot functions doesn't accidentally trigger.
            // sShiftConsumed suppresses the trig if the press was used for a combo.
            if (gBtnShift.released()) {
                if (!sShiftConsumed) {
                    gGatePatched = true;
                    gGateHigh = true;
                    sTrigReleaseAt = millis() + 100u;
#ifdef SERIAL_CONTROL
                    Serial.println(F("shift -> trig"));
#endif
                }
                sShiftConsumed = false; // reset for next press
            }
        }
    }

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
    sMidiVelocity += (gMidiVelocity - sMidiVelocity) * 0.1f;
    sSpace += (gSpace - sSpace) * 0.1f;
    sRelation += (gRelation - sRelation) * 0.1f;

    // Update envelope engine: AR or ADSR depending on gEnvelopeType.
    if (gEnvelopeType == EnvelopeType::AR) {
        static_cast<AREnvelope<MOZZI_AUDIO_RATE> *>(gCurveEng)->setCurve(sCurve, sCurveTime);
    } else {
        static_cast<ADSREnvelope<MOZZI_AUDIO_RATE> *>(gCurveEng)->setADSR(
            gAdsrAttack, gAdsrDecay, gAdsrSustain, gAdsrRelease, gAdsrLoop);
    }
    {
        static bool prevGate = false;
        const bool curGate = gGateHigh;
        if (curGate != prevGate) {
            gCurveEng->setGate(curGate);
            // On rising gate edge (retrigger), reset all oscillator phases so the
            // attack always starts at the waveform zero-crossing.  Without this,
            // re-triggering during release at a random phase point creates a click
            // whose ring modulation sidebands sound metallic / PWM-like.
            if (curGate) {
                for (uint8_t i = 0; i < 4; i++) {
                    voices[i].resetPhase();
                    subVoices[i].resetPhase();
                }
            }
            prevGate = curGate;
        }
    }

    // Apply per-voice drift offsets; sub oscillators track their main automatically.
    // PAIR mode: voice 2 is offset by gRelation semitones above ROOT (0=unison, 12=octave).
    // gDetune adds a symmetric Hz fine-spread on both voices.
    // powf() only recomputed when sRelation changes meaningfully; never called in the ISR.
    gDrift.setSpeed(gDriftSpeed);
    gDrift.update(sMotion);
    float voiceFreqs[4] = {gBaseFreq, gBaseFreq, gBaseFreq, gBaseFreq};
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
        voiceFreqs[0] = max(gBaseFreq - gDetune * 0.5f + gDrift.offset(0), 20.0f);
        voiceFreqs[1] = max(gBaseFreq * cachedRatio2 + gDetune * 0.5f + gDrift.offset(1), 20.0f);
        voices[0].setFreq(voiceFreqs[0]);
        voices[1].setFreq(voiceFreqs[1]);
        voices[0].setShape(sShape);
        voices[1].setShape(sShape);
        sActiveVoices = 2;
        sPanL[0] = 256;
        sPanL[1] = 0;
        sPanL[2] = 0;
        sPanL[3] = 0;
        sPanR[0] = 0;
        sPanR[1] = 256;
        sPanR[2] = 0;
        sPanR[3] = 0;
        {
            const float subMul = (gSubOctave == 2) ? 0.25f : 0.5f;
            subVoices[0].setFreq(voiceFreqs[0] * subMul);
            subVoices[1].setFreq(voiceFreqs[1] * subMul);
        }
        break;
    }
    case VoiceMode::CHORD: {
        // 11 chord shapes — RELATION (0–24 semitones) sweeps continuously through them.
        // Columns: semitone offsets for voices 0–3 from ROOT pitch.
        static const int8_t kChordTable[11][4] = {
            {0, 0, 0, 0},    // 0.0  Unison
            {0, 7, 12, 19},  // 0.1  Power
            {0, 3, 7, 12},   // 0.2  Minor
            {0, 4, 7, 12},   // 0.3  Major
            {0, 2, 7, 12},   // 0.4  Sus2
            {0, 5, 7, 12},   // 0.5  Sus4
            {0, 4, 7, 11},   // 0.6  Major 7
            {0, 3, 7, 10},   // 0.7  Minor 7
            {0, 4, 7, 10},   // 0.8  Dominant 7
            {0, 3, 6, 9},    // 0.9  Diminished
            {0, 12, 24, 36}, // 1.0  Octaves
        };
        // sRelation 0–24 st → continuous position 0.0–10.0 across table rows.
        const float chordFrac = (sRelation / 24.0f) * 10.0f;
        const int idx0 = (int)chordFrac < 9 ? (int)chordFrac : 9;
        const float blend = chordFrac - (float)idx0;
        // Cache: only recompute 4× powf when base freq or relation changes.
        static float sCachedChordRel = -1.0f;
        static float sCachedChordBase = -1.0f;
        static float sCachedFreqs[4] = {440.0f, 440.0f, 440.0f, 440.0f};
        if (fabsf(sRelation - sCachedChordRel) > 0.05f ||
            fabsf(gBaseFreq - sCachedChordBase) > 0.01f) {
            for (int i = 0; i < 4; i++) {
                const float st = kChordTable[idx0][i] * (1.0f - blend) +
                                 kChordTable[idx0 + 1][i] * blend;
                sCachedFreqs[i] = gBaseFreq * powf(2.0f, st / 12.0f);
            }
            sCachedChordRel = sRelation;
            sCachedChordBase = gBaseFreq;
        }
        for (int i = 0; i < 4; i++) {
            voiceFreqs[i] = max(sCachedFreqs[i] + gDrift.offset(i), 20.0f);
            voices[i].setFreq(voiceFreqs[i]);
            voices[i].setShape(sShape);
            {
                const float subMul = (gSubOctave == 2) ? 0.25f : 0.5f;
                subVoices[i].setFreq(voiceFreqs[i] * subMul);
            }
        }
        sActiveVoices = 4;
        // Stereo spread: hard-L, soft-L, soft-R, hard-R — normalized so sum(L)=sum(R)=256.
        sPanL[0] = 128;
        sPanL[1] = 90;
        sPanL[2] = 38;
        sPanL[3] = 0;
        sPanR[0] = 0;
        sPanR[1] = 38;
        sPanR[2] = 90;
        sPanR[3] = 128;
        break;
    }
    }

    // sSubWf: fatness 0..1 → sub contributing 0..50% of main amplitude.
    // Written here (Core 0 control rate), read in updateAudio() ISR — atomic on M33.
    sSubWf = sFatness * 0.5f;

    // Chorus depth — single float, atomic on M33, no mutex needed.
    gChorusDepth = sMotion;

    // Filter (M26a / M5x) — runtime type switch + smooth coefficients.
    // gFilterType drives re-pointing of gFilterInst; integrators cleared on switch.
    {
        static FilterType sPrevFilterType = FilterType::SVF;
        if (gFilterType != sPrevFilterType) {
            gFilterInst->reset(); // silence old integrators
            gFilterInst = (gFilterType == FilterType::SVF)
                              ? static_cast<FilterEngine *>(&sSvfFilter)
                              : static_cast<FilterEngine *>(&sOtaLadder);
            sPrevFilterType = gFilterType;
        }
    }
    // Smooth cutoff + resonance, then recompute coefficients (tanf — safe at 128 Hz).
    {
        static float sFilterCutoff = 8000.0f;
        static float sFilterRes = 0.0f;
        sFilterCutoff += (gFilterCutoff - sFilterCutoff) * 0.1f;
        sFilterRes += (gFilterRes - sFilterRes) * 0.1f;
        gFilterInst->setParams(sFilterCutoff, sFilterRes, gFilterMode);
    }

    // Envelope type switch (M5x) — re-point gCurveEng and reset on change.
    {
        static EnvelopeType sPrevEnvType = EnvelopeType::AR;
        if (gEnvelopeType != sPrevEnvType) {
            gCurveEng->reset(); // silence old envelope
            gCurveEng = (gEnvelopeType == EnvelopeType::AR)
                            ? static_cast<EnvelopeEngine *>(&sArEnv)
                            : static_cast<EnvelopeEngine *>(&sAdsrEnv);
            sPrevEnvType = gEnvelopeType;
        }
    }

    // Delay (M26c) — update params at control rate (stub; full implementation in M26c).
    gDelay.setParams(gDelayTime, gDelayFeedback, gDelayMix);

    // Reverb (M26b) — setParams at control rate; safe on Core 1 (tanf allowed here).
    // Change-detect: only recalculate coefficients when parameters actually change.
    {
        static float sPrevRevSize = -1.0f, sPrevRevDamping = -1.0f;
        if (gRevSize != sPrevRevSize || gRevDamping != sPrevRevDamping) {
            gReverb->setParams(gRevSize, gRevDamping);
            sPrevRevSize = gRevSize;
            sPrevRevDamping = gRevDamping;
        }
    }
    {
        static float sPrevRevModSpeed = -1.0f, sPrevRevModDepth = -1.0f;
        static bool sPrevFrozen = false;
        if (gRevModSpeed != sPrevRevModSpeed || gRevModDepth != sPrevRevModDepth) {
            gReverb->setModulation(gRevModSpeed, gRevModDepth);
            sPrevRevModSpeed = gRevModSpeed;
            sPrevRevModDepth = gRevModDepth;
        }
        if (gRevFrozen != sPrevFrozen) {
            gReverb->freeze(gRevFrozen);
            sPrevFrozen = gRevFrozen;
        }
    }

    // Publish smoothed params for Core 1 DSP engines (chorus, drift — Milestones 12+).
    mutex_enter_blocking(&gDspMutex);
    gDsp.freq1 = voiceFreqs[0];
    gDsp.freq2 = voiceFreqs[1];
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
            const uint32_t upSec = now / 1000;
            const uint32_t mm = upSec / 60;
            const uint32_t ss = upSec % 60;
            // delta overruns since last auto-print interval
            static uint32_t lastAutoOver = 0;
            const uint32_t delta = gAudioOverruns - lastAutoOver;
            lastAutoOver = gAudioOverruns;
            float headroom = (30.0f - (float)us) / 30.0f * 100.0f;
            Serial.print(F("[cpu] "));
            Serial.print(us);
            Serial.print(F("us/30us  headroom "));
            Serial.print(headroom, 1);
            Serial.print(F("%  overruns "));
            Serial.print(gAudioOverruns);
            Serial.print(F(" (+"));
            Serial.print(delta);
            Serial.print(F("/5s)  up "));
            if (mm < 10)
                Serial.print('0');
            Serial.print(mm);
            Serial.print(':');
            if (ss < 10)
                Serial.print('0');
            Serial.println(ss);
        }
    }
#endif
}

// Place the audio ISR in SRAM so it is never evicted from the 16KB XIP cache
// by TinyUSB / Serial activity in updateControl().  A cold cache miss on the
// ISR entry takes 15-30µs — unmeasured by time_us_32() (read after re-fill) —
// and manifests as sporadic overruns even when the measured time looks fine.
AudioOutput __attribute__((section(".time_critical.updateAudio"))) updateAudio() {
#ifdef CPU_PROFILE
    const uint32_t _t0 = time_us_32();
#endif

    // Sum sActiveVoices into L/R via fixed-point pan weights (×256).
    // sum(sPanL) = sum(sPanR) = 256 keeps each channel within ±32512 max.
    int32_t left = 0, right = 0;
    for (uint8_t i = 0; i < sActiveVoices; i++) {
        const int32_t s = voices[i].next();
        const int32_t sub = subVoices[i].next();
        const int32_t m = (int32_t)((float)s + (float)sub * sSubWf);
        left += (m * sPanL[i]) >> 8;
        right += (m * sPanR[i]) >> 8;
    }

    // Soft clip: cap at ±32512 before volume scaling to prevent from16Bit wrap.
    if (left > 32512)
        left = 32512;
    else if (left < -32512)
        left = -32512;
    if (right > 32512)
        right = 32512;
    else if (right < -32512)
        right = -32512;

    // CURVE envelope VCA (Milestone 15): bypassed in drone mode (no gate patched).
    // Float multiply — eliminates the 256-step integer quantization that produces
    // audible zipper artifacts during exponential decay (steps space out as amplitude
    // falls, creating periodic clicks at ~40 Hz early → sub-audio rate near zero).
    // Cortex-M33 FPU: two float multiplies, same cost as the previous integer path.
    const float envLevel = gGatePatched ? gCurveEng->next() : 1.0f;
    const float gain = sVolume * sMidiVelocity * envLevel;
    left = (int32_t)((float)left * gain);
    right = (int32_t)((float)right * gain);

    // ---------------------------------------------------------------------------
    // M26 Post-effects chain — 4 orderings via two bool flags (gFxOrder).
    // All bool reads are atomic on Cortex-M33 (8-bit aligned); no mutex needed.
    // ---------------------------------------------------------------------------

    // [FILTER — PRE-CHORUS position]
    if (!gFxOrder.filterPostChorus) {
        gFilterInst->process(left, right, &left, &right);
    }

    // Chorus — Core 0 ISR phasor LFO (~50 cycles, no trig).
    // gChorusDepth and gChorusMode are written by updateControl() at 128 Hz;
    // single 32-bit aligned reads are atomic on Cortex-M33, no mutex needed.
    int32_t outL, outR;
    gChorus.process(left, right, gChorusDepth, gChorusMode, &outL, &outR);

    // [FILTER — POST-CHORUS position]
    if (gFxOrder.filterPostChorus) {
        gFilterInst->process(outL, outR, &outL, &outR);
    }

    // [DELAY — PRE-REVERB position]
    if (!gFxOrder.delayPostReverb) {
        gDelay.process(outL, outR, &outL, &outR);
    }

    // REVERB — only when enabled: deposit dry signal, wake Core 1, mix wet return.
    // When disabled, Core 1 stays permanently in WFE — zero SRAM bus traffic from
    // Core 1, eliminating the bus-arbitration stalls that cause subtle crackle even
    // when reverb is not audibly active. gRevOut_L/R remain 0 (written by setup1).
    if (gRevEnabled) {
        // Write dry signal BEFORE sev so Core 1 never reads a half-written pair.
        gRevIn_L = outL;
        gRevIn_R = outR;
        gRevSampleSeq++;
        __asm volatile("sev"); // wake Core 1
        // Mix the wet return from Core 1 (1 sample old — inaudible for reverb tails).
        // Float multiply — avoids 256-step quantization on gRevMix changes.
        const float wetMix = gRevMix;
        outL += (int32_t)((float)gRevOut_L * wetMix);
        outR += (int32_t)((float)gRevOut_R * wetMix);
        // Soft-clip after wet addition (BP resonance + reverb can exceed ±32512).
        if (outL > 32512)
            outL = 32512;
        if (outL < -32512)
            outL = -32512;
        if (outR > 32512)
            outR = 32512;
        if (outR < -32512)
            outR = -32512;
    }

    // [DELAY — POST-REVERB position]
    if (gFxOrder.delayPostReverb) {
        gDelay.process(outL, outR, &outL, &outR);
    }

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
    // Enable Flush-to-Zero mode on Core 1's FPU.
    // The reverb tail decays into denormal territory; without FTZ the FPU takes
    // ~100x longer per operation, Core 1 falls behind Core 0, and crackle results.
    {
        uint32_t fpscr;
        asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1u << 24); // FZ: Flush-to-Zero
        asm volatile("vmsr fpscr, %0" ::"r"(fpscr));
    }
    // Zero all reverb delay lines before audio starts.
    gReverb->reset();
}

void loop1() {
    // Reverb processing — Core 1 processes exactly once per audio sample.
    // Uses WFE/SEV instead of a hot spin: Core 1 sleeps with zero bus traffic
    // until Core 0's ISR fires __sev() after depositing the new sample.
    // A hot spin read of a shared volatile at 150 MHz saturates the SRAM bus
    // and causes the Core 0 ISR to stall waiting for arbitration — crackle.
    static uint32_t sLastSeq = 0;
    uint32_t seq;
    while ((seq = gRevSampleSeq) == sLastSeq) {
        __asm volatile("wfe"); // sleep; wake on __sev() from Core 0 ISR
    }
    sLastSeq = seq;

    if (gRevEnabled) {
        // Normalise ±32512 int32 → ±1.0f for the reverb algorithm.
        const float inL = (float)gRevIn_L * (1.0f / 32512.0f);
        const float inR = (float)gRevIn_R * (1.0f / 32512.0f);
        float revL, revR;
        gReverb->process(inL, inR, &revL, &revR);
        // Clamp before converting back — algorithmic edge cases can spike.
        if (revL > 1.0f)
            revL = 1.0f;
        if (revL < -1.0f)
            revL = -1.0f;
        if (revR > 1.0f)
            revR = 1.0f;
        if (revR < -1.0f)
            revR = -1.0f;
        gRevOut_L = (int32_t)(revL * 32512.0f);
        gRevOut_R = (int32_t)(revR * 32512.0f);
    } else {
        gRevOut_L = 0;
        gRevOut_R = 0;
    }
}
