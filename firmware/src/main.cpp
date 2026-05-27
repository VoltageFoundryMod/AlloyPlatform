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

// Reverb isolation switch:
// 1 = run reverb inline on Core 0 (single-core path, like VCV) to isolate
//     multicore transport/coherency effects.
// 0 = normal Core 1 offload path.
#ifndef REVERB_FORCE_CORE0
#define REVERB_FORCE_CORE0 0
#endif

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
// Module includes
// ---------------------------------------------------------------------------
#include "SynthEngine.h" // M37b: SynthParams, SynthEngine, gSynthEngine
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
#include "io/HardwarePicoIO.h" // M37d — IHardwareIO implementation for Pico
#include "io/IOBridge.h"       // M37d — fillSynthParams()
#include "io/commands.h"       // doTrig()
#include "io/serial_console.h"
#include "params.h"
// ---------------------------------------------------------------------------
// Shared synthesis parameters (declared extern in params.h)
// ---------------------------------------------------------------------------
float gBaseFreq = 440.0f;
float gColor = 0.0f; // COLOR knob: FM depth (PAIR/CASCADE) or Hz fine spread (ensemble)
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
bool gVelocitySensitive = true;            // true = MIDI velocity scales output; false = always 1.0
float gGlideTime = 0.0f;                   // portamento slide time: 0.0 = instant, 0.001–2.0 s
bool gGlideEnabled = false;                // portamento on/off (CC 65)
ChorusMode gChorusMode = ChorusMode::I_II; // default: Juno I+II (maximum stereo spread)
float gSpace = 1.0f;                       // stereo width: 0.0 = mono, 1.0 = full stereo
uint8_t gMidiChannel = 0;                  // 0 = omni, 1–16 = specific MIDI channel

EnvelopeType gEnvelopeType = EnvelopeType::AR;
// Trig pulse timer — set by cmd_trig / doTrig(), cleared in updateControl() when elapsed.
// 0 means no trig pending.
uint32_t sTrigReleaseAt = 0;
// Poly voice slot claimed by a trig pulse; 255 = not set (non-poly or no active trig).
uint8_t sTrigPolySlot = 255;

// Button engines (Milestone 31) — polled at 128 Hz in updateControl().
static ButtonEngine gBtnMode(PIN_BUTTON_MODE);   // mode cycle
static ButtonEngine gBtnShift(PIN_BUTTON_SHIFT); // shift / combo
// M37d — hardware IO abstraction layer; owns readPot/readCV/readButton/writeLight.
static HardwarePicoIO sHardwareIO(gBtnMode, gBtnShift);
// Set to true whenever SHIFT is consumed by a combo or knob action so the
// trig-on-release is suppressed. Reset automatically on SHIFT release.
static bool sShiftConsumed = false; // suppresses trig-on-release when SHIFT used in combo
static bool sModeConsumed = false;  // suppresses mode-cycle-on-release when MODE used in combo

// ---------------------------------------------------------------------------
// M26 Post-effects engines and parameters
// ---------------------------------------------------------------------------

// Filter (M26a / M5x) — runtime-selectable algorithm.
// NOTE: FilterEngine *gFilterInst now defined in SynthEngine.cpp.
FilterType gFilterType = FilterType::SVF;
float gFilterCutoff = kDefaultFilterCutoff;
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
// Core 0 ISR pushes pre-reverb frames into a small SPSC queue.
// Core 1 pops them in order and publishes the latest wet frame back to Core 0.
struct RevInFrame {
    int32_t l;
    int32_t r;
};
static constexpr uint32_t kRevInQueueSize = 128;
static constexpr uint32_t kRevInQueueMask = kRevInQueueSize - 1;
volatile RevInFrame gRevInQueue[kRevInQueueSize] = {};
volatile uint32_t gRevInWriteIdx = 0;
volatile uint32_t gRevInReadIdx = 0;
volatile uint32_t gRevInDrops = 0; // diagnostic: input frames dropped due to full queue
// Fixed-delay output ring buffer — Core 1 writes wet frames here.
// Core 0 reads kRevReadDelay slots behind the write pointer, guaranteeing a
// constant 2-sample wet latency regardless of Core 1 execution jitter.
// Constant latency = constant comb = no shimmer.
static constexpr uint32_t kRevOutBufDepth = 8u; // power-of-2; must be > kRevReadDelay
static constexpr uint32_t kRevReadDelay = 3u;   // fixed wet latency in ISR ticks (~90 µs)
volatile int32_t gRevOutBuf_L[kRevOutBufDepth] = {};
volatile int32_t gRevOutBuf_R[kRevOutBufDepth] = {};
// Pre-seeded to kRevReadDelay so slots [0..kRevReadDelay-1] are valid silence
// when Core 0's read cursor starts at 0 on first reverb enable.
volatile uint32_t gRevOutWriteIdx = kRevReadDelay;

// Reverb transport state — file-scope so revGetWet/revFeedDry helpers can share them.
#if REVERB_FORCE_CORE0
static int32_t sRevPrevDryL = 0; // Core 0 inline: previous dry frame fed back to reverb
static int32_t sRevPrevDryR = 0;
#else
static uint32_t sRevReadIdx = 0; // Core 1: ISR read cursor, advances +1 per ISR tick
#endif

volatile float gRevMix = 0.35f;
volatile bool gRevEnabled = false;
volatile float gRevSize = 0.5f;
volatile float gRevDamping = 0.5f;
volatile float gRevModSpeed = 1.0f; // M40: LFO rate multiplier (0.1–4.0)
volatile float gRevModDepth = 1.0f; // M40: LFO amplitude multiplier (0.0–1.0)
volatile bool gRevFrozen = false;   // M41: infinite sustain when true

// Delay (M26c) — ping-pong stereo delay; pass-through stub until ring buffer lands.
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
    // M37b: SynthEngine owns all DSP state; init() generates wavetables,
    // seeds all engines, and warms the powf/trig caches.
    // Must run before startMozzi() so chorus delay buffers are filled.
    gSynthEngine.init(MOZZI_AUDIO_RATE, MOZZI_CONTROL_RATE);
    gBtnMode.begin();
    gBtnShift.begin();
    startMozzi();
    serialConsole_ready();
#ifdef CPU_PROFILE
    // Clear any overruns that occurred during Mozzi's startup DMA/PIO init —
    // they are not representative of steady-state audio performance.
    gAudioOverruns = 0;
#endif
}

// Forward declarations for reverb helpers defined before updateAudio().
static void revApplyParams();
static void revGetWet(int32_t *wetL, int32_t *wetR, bool wasActive);
static void revFeedDry(int32_t dryL, int32_t dryR);

void updateControl() {
    serialConsole_update();
#ifdef USE_TINYUSB
    usbMidi_update();
    static uint32_t sLastMidiFeedbackMs = 0;
    const uint32_t nowMidi = millis();
    if (nowMidi - sLastMidiFeedbackMs >= 250u) {
        sLastMidiFeedbackMs = nowMidi;
        usbMidi_sendFeedback();
    }
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
    // sModeConsumed: same pattern for MODE — set when MODE is used in a combo so
    // the mode-cycle on release is suppressed.
    {
        static bool sDroneComboFired = false;
        if (gBtnMode.isDown() && gBtnShift.isDown()) {
            if (!sDroneComboFired) {
                sDroneComboFired = true;
                sShiftConsumed = true; // don't trig on SHIFT release
                sModeConsumed = true;  // don't cycle on MODE release
                gGatePatched = false;
                gGateHigh = false;
                gMidiVelocity = 1.0f; // restore full volume when returning to drone/CV
#ifdef SERIAL_CONTROL
                Serial.println(F("gate -> free (drone)"));
#endif
            }
        } else {
            sDroneComboFired = false;
            // Mode solo: cycle voice mode on RELEASE so holding MODE can be used
            // as a secondary shift key for future combos (mirrors SHIFT behaviour).
            if (gBtnMode.released()) {
                if (!sModeConsumed) {
                    // Cycle through implemented voice modes only.
                    static const VoiceMode kActiveModes[] = {VoiceMode::PAIR, VoiceMode::CLOUD,
                                                             VoiceMode::CHORD, VoiceMode::CASCADE,
                                                             VoiceMode::STRING, VoiceMode::POLY};
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
                sModeConsumed = false; // reset for next press
            }
            // Shift solo: trig fires on RELEASE (not press) so holding SHIFT for
            // a combo or future SHIFT+pot functions doesn't accidentally trigger.
            // sShiftConsumed suppresses the trig if the press was used for a combo.
            if (gBtnShift.released()) {
                if (!sShiftConsumed) {
                    doTrig(100u);
#ifdef SERIAL_CONTROL
                    Serial.println(F("shift -> trig"));
#endif
                }
                sShiftConsumed = false; // reset for next press
            }
        }
    }

    // Auto-release for doTrig: lower gate (or release poly voice) when pulse has elapsed.
    if (sTrigReleaseAt && millis() >= sTrigReleaseAt) {
        if (sTrigPolySlot != 255) {
            sPolyEnvs[sTrigPolySlot]->setGate(false);
            sPolySlots[sTrigPolySlot].midiNote = 255; // free the slot
            sTrigPolySlot = 255;
        } else {
            gGateHigh = false;
        }
        sTrigReleaseAt = 0;
    }

    // -----------------------------------------------------------------------
    // M37d/M37b: populate SynthParams — IO layer first, then MIDI/serial globals.
    // fillSynthParams() covers hardware-knob/CV driven fields (baseFreq, gate).
    // All other fields (filter, reverb, ADSR, …) continue via gXxx globals.
    // -----------------------------------------------------------------------
    {
        SynthParams p;
        fillSynthParams(sHardwareIO, p); // M37d: baseFreq, gateHigh, gatePatched
        // MIDI note-on overrides V/OCT CV pitch so the played note is heard
        // rather than whatever voltage is on the V/OCT jack.
        if (sActiveNote != 255)
            p.baseFreq = gBaseFreq;
        p.shape = gShape;
        p.fatness = gFatness;
        p.subOctave = gSubOctave;
        p.motion = gMotion;
        p.driftSpeed = gDriftSpeed;
        p.voiceMode = gVoiceMode;
        p.relation = gRelation;
        p.curve = gCurve;
        p.curveTime = gCurveTime;
        // p.gateHigh / p.gatePatched — set by fillSynthParams() above.
        p.volume = gVolume;
        p.midiVelocity = gMidiVelocity;
        p.glideTime = gGlideTime;
        p.glideEnabled = gGlideEnabled;
        p.chorusMode = gChorusMode;
        p.space = gSpace;
        p.color = gColor;
        p.envelopeType = gEnvelopeType;
        p.adsrAttack = gAdsrAttack;
        p.adsrDecay = gAdsrDecay;
        p.adsrSustain = gAdsrSustain;
        p.adsrRelease = gAdsrRelease;
        p.adsrLoop = gAdsrLoop;
        p.filterType = gFilterType;
        p.filterCutoff = gFilterCutoff;
        p.filterRes = gFilterRes;
        p.filterMode = gFilterMode;
        p.fxOrder = gFxOrder;
        p.revMix = gRevMix;
        gRevEnabled = (gRevMix > 0.001f); // derived from mix; no longer set by CC
        p.revEnabled = gRevEnabled;
        p.revSize = gRevSize;
        p.revDamping = gRevDamping;
        p.revModSpeed = gRevModSpeed;
        p.revModDepth = gRevModDepth;
        p.revFrozen = gRevFrozen;
        p.delayTime = gDelayTime;
        p.delayFeedback = gDelayFeedback;
        p.delayMix = gDelayMix;

        SynthControlOutput co;
        gSynthEngine.control(p, sPolySlots, co);

        revApplyParams();

        // Publish smoothed params for Core 1 DSP engines.
        mutex_enter_blocking(&gDspMutex);
        gDsp.freq1 = co.freq1;
        gDsp.freq2 = co.freq2;
        gDsp.shape = co.shape;
        gDsp.fatness = co.fatness;
        gDsp.motion = co.motion;
        gDsp.curve = co.curve;
        gDsp.volume = co.volume;
        mutex_exit(&gDspMutex);
    }

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
#if !REVERB_FORCE_CORE0
            static uint32_t lastRevDrops = 0;
            const uint32_t deltaRevDrops = gRevInDrops - lastRevDrops;
            lastRevDrops = gRevInDrops;
#endif
            float headroom = (30.0f - (float)us) / 30.0f * 100.0f;
            Serial.print(F("[cpu] "));
            Serial.print(us);
            Serial.print(F("us/30us  headroom "));
            Serial.print(headroom, 1);
            Serial.print(F("%  overruns "));
            Serial.print(gAudioOverruns);
            Serial.print(F(" (+"));
            Serial.print(delta);
            Serial.print(F("/5s)"));
#if !REVERB_FORCE_CORE0
            Serial.print(F("  revdrops +"));
            Serial.print(deltaRevDrops);
            Serial.print(F("/5s"));
#endif
            Serial.print(F("  up "));
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

// ---------------------------------------------------------------------------
// Reverb transport helpers — hide the Core0-inline vs Core1-offload divergence
// so updateAudio() has a single unified control flow regardless of the flag.
// Both helpers are hot-path: placed in SRAM alongside updateAudio.
// ---------------------------------------------------------------------------

// revApplyParams() — called at control rate (updateControl, 128 Hz).
// Core 0:  applies reverb params directly (same core that calls process()).
// Core 1:  no-op — loop1() applies params there with the same deadbands.
static void revApplyParams() {
#if REVERB_FORCE_CORE0
    static float sPrevSize = -1.0f, sPrevDamp = -1.0f;
    static float sPrevModSpeed = -1.0f, sPrevModDepth = -1.0f;
    static bool sPrevFrozen = !false;
    if (fabsf(gRevSize - sPrevSize) > 0.0025f || fabsf(gRevDamping - sPrevDamp) > 0.0025f) {
        gSynthEngine.reverb->setParams(gRevSize, gRevDamping);
        sPrevSize = gRevSize;
        sPrevDamp = gRevDamping;
    }
    if (fabsf(gRevModSpeed - sPrevModSpeed) > 0.01f || fabsf(gRevModDepth - sPrevModDepth) > 0.01f) {
        gSynthEngine.reverb->setModulation(gRevModSpeed, gRevModDepth);
        sPrevModSpeed = gRevModSpeed;
        sPrevModDepth = gRevModDepth;
    }
    if (gRevFrozen != sPrevFrozen) {
        gSynthEngine.reverb->freeze(gRevFrozen);
        sPrevFrozen = gRevFrozen;
    }
#endif
    // Core 1 path: loop1() handles param updates — nothing to do here.
}

// revGetWet() — ISR hot path, called only when gRevEnabled.
// wasActive: true if reverb was also enabled on the previous ISR tick.
// Core 0: run reverb inline on the previous dry frame (1-sample constant latency).
// Core 1: read from the fixed-delay ring buffer (constant kRevReadDelay latency).
static __attribute__((section(".time_critical.revGetWet"))) void revGetWet(int32_t *wetL, int32_t *wetR, bool wasActive) {
#if REVERB_FORCE_CORE0
    constexpr float kNorm = 1.0f / 32512.0f;
    float wL, wR;
    gSynthEngine.reverb->process((float)sRevPrevDryL * kNorm,
                                 (float)sRevPrevDryR * kNorm, &wL, &wR);
    if (wL > 1.0f)
        wL = 1.0f;
    if (wL < -1.0f)
        wL = -1.0f;
    if (wR > 1.0f)
        wR = 1.0f;
    if (wR < -1.0f)
        wR = -1.0f;
    *wetL = (int32_t)(wL * 32512.0f);
    *wetR = (int32_t)(wR * 32512.0f);
#else
    // Re-anchor read cursor when reverb transitions from disabled→enabled so we
    // don't replay stale slots from a previous session (Core 1 kept writing while
    // reverb was off; sRevReadIdx was frozen at its last value).
    if (!wasActive)
        sRevReadIdx = gRevOutWriteIdx - kRevReadDelay;
    __asm volatile("dmb" ::: "memory");
    *wetL = gRevOutBuf_L[sRevReadIdx & (kRevOutBufDepth - 1u)];
    *wetR = gRevOutBuf_R[sRevReadIdx & (kRevOutBufDepth - 1u)];
    ++sRevReadIdx;
#endif
}

// revFeedDry() — ISR hot path, called only when gRevEnabled.
// Core 0: stash dry frame for next tick's inline reverb call.
// Core 1: push dry frame into the SPSC input queue for loop1().
static __attribute__((section(".time_critical.revFeedDry"))) void revFeedDry(int32_t dryL, int32_t dryR) {
#if REVERB_FORCE_CORE0
    sRevPrevDryL = dryL;
    sRevPrevDryR = dryR;
#else
    const uint32_t writeIdx = gRevInWriteIdx;
    const bool wasEmpty = (writeIdx == gRevInReadIdx);
    const uint32_t nextWriteIdx = (writeIdx + 1u) & kRevInQueueMask;
    // Queue full: drop oldest so newest dry frame always reaches the reverb.
    if (nextWriteIdx == gRevInReadIdx) {
        gRevInReadIdx = (gRevInReadIdx + 1u) & kRevInQueueMask;
        gRevInDrops++;
    }
    gRevInQueue[writeIdx].l = dryL;
    gRevInQueue[writeIdx].r = dryR;
    __asm volatile("dmb" ::: "memory");
    gRevInWriteIdx = nextWriteIdx;
    if (wasEmpty)
        __asm volatile("sev"); // wake Core 1 on empty→non-empty transition
#endif
}

// M37b — thin audio ISR: delegate all DSP to SynthEngine, handle reverb transport.
AudioOutput __attribute__((section(".time_critical.updateAudio"))) updateAudio() {
#ifdef CPU_PROFILE
    const uint32_t _t0 = time_us_32();
#endif

    static bool sPrevRevActive = false;
    int32_t revWetL = 0, revWetR = 0;
    if (gRevEnabled)
        revGetWet(&revWetL, &revWetR, sPrevRevActive);
    sPrevRevActive = (bool)gRevEnabled;

    int32_t outL, outR, dryL, dryR;
    gSynthEngine.audio(revWetL, revWetR, gRevMix, gRevEnabled,
                       &outL, &outR, &dryL, &dryR);

    if (gRevEnabled)
        revFeedDry(dryL, dryR);

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
    // reverb is pre-set by SynthEngine's constructor so this is always safe,
    // even if Core 1 reaches here before Core 0 calls init().
    gSynthEngine.reverb->reset();
    // Apply initial reverb params on Core 1 to keep all reverb state mutation
    // on the same core that runs reverb->process().
    gSynthEngine.reverb->setParams(gRevSize, gRevDamping);
    gSynthEngine.reverb->setModulation(gRevModSpeed, gRevModDepth);
    gSynthEngine.reverb->freeze(gRevFrozen);
}

// loop1 in SRAM: eliminates XIP-cache code-fetch jitter that would otherwise
// add variable latency to Core 1's reverb processing and regenerate shimmer.
void __attribute__((section(".time_critical.loop1"))) loop1() {
#if REVERB_FORCE_CORE0
    // Reverb runs on Core 0 in isolation mode.
    __asm volatile("wfe");
    return;
#else
    static float sPrevRevSize = -1.0f;
    static float sPrevRevDamp = -1.0f;
    static float sPrevRevModSpeed = -1.0f;
    static float sPrevRevModDepth = -1.0f;
    static bool sPrevRevFrozen = !false;

    const float curRevSize = gRevSize;
    const float curRevDamp = gRevDamping;
    const float curRevModSpeed = gRevModSpeed;
    const float curRevModDepth = gRevModDepth;
    const bool curRevFrozen = gRevFrozen;

    // Apply with deadbands to suppress ADC/control jitter churn.
    // Exact float comparisons can retrigger at control rate (128 Hz), causing
    // unnecessary reverb reconfiguration and inter-core contention.
    if (fabsf(curRevSize - sPrevRevSize) > 0.0025f ||
        fabsf(curRevDamp - sPrevRevDamp) > 0.0025f) {
        gSynthEngine.reverb->setParams(curRevSize, curRevDamp);
        sPrevRevSize = curRevSize;
        sPrevRevDamp = curRevDamp;
    }
    if (fabsf(curRevModSpeed - sPrevRevModSpeed) > 0.01f ||
        fabsf(curRevModDepth - sPrevRevModDepth) > 0.01f) {
        gSynthEngine.reverb->setModulation(curRevModSpeed, curRevModDepth);
        sPrevRevModSpeed = curRevModSpeed;
        sPrevRevModDepth = curRevModDepth;
    }
    if (curRevFrozen != sPrevRevFrozen) {
        gSynthEngine.reverb->freeze(curRevFrozen);
        sPrevRevFrozen = curRevFrozen;
    }

    // Reverb processing — Core 1 pops one dry frame per pass.
    //
    // NOTE: no WFE sleep here. WFE causes variable wake-from-sleep latency
    // (pipeline flush + loop1 re-entry overhead) before process() runs.
    // That jitter gives the reverb wet a variable group-delay relative to
    // the dry signal. When Core 0 mixes them (dry + wet), the time-varying
    // delay becomes a swept comb filter — audible as shimmer.
    // In Core 0 inline mode the dry→wet latency is always exactly 1 sample
    // (constant comb), which is why shimmer disappears there.
    //
    // Spin-polling gives Core 1 near-constant 1-sample wet latency.
    // Core 1 burns all idle cycles here; acceptable on a dual-core MCU.
    if (gRevEnabled) {
        if (gRevInReadIdx == gRevInWriteIdx) {
            return; // queue empty — spin-poll: SDK calls loop1() immediately
        }

        const uint32_t readIdx = gRevInReadIdx;
        __asm volatile("dmb" ::: "memory");
        const int32_t inL_i = gRevInQueue[readIdx].l;
        const int32_t inR_i = gRevInQueue[readIdx].r;
        gRevInReadIdx = (readIdx + 1u) & kRevInQueueMask;

        // Normalise ±32512 int32 → ±1.0f for the reverb algorithm.
        const float inL = (float)inL_i * (1.0f / 32512.0f);
        const float inR = (float)inR_i * (1.0f / 32512.0f);
        float revL, revR;
        gSynthEngine.reverb->process(inL, inR, &revL, &revR);

        // Clamp before converting back — algorithmic edge cases can spike.
        if (revL > 1.0f)
            revL = 1.0f;
        if (revL < -1.0f)
            revL = -1.0f;
        if (revR > 1.0f)
            revR = 1.0f;
        if (revR < -1.0f)
            revR = -1.0f;

        {
            const uint32_t wi = gRevOutWriteIdx & (kRevOutBufDepth - 1u);
            gRevOutBuf_L[wi] = (int32_t)(revL * 32512.0f);
            gRevOutBuf_R[wi] = (int32_t)(revR * 32512.0f);
            __asm volatile("dmb" ::: "memory");
            gRevOutWriteIdx++;
        }
    } else {
        gRevInReadIdx = gRevInWriteIdx; // flush any stale queued dry frames
        // Ring buffer not updated when disabled; Core 0 ignores it (gRevEnabled=false).
    }
#endif
}
