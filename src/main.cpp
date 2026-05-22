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
// Trig pulse timer — set by cmd_trig, cleared in updateControl() when elapsed.
// 0 means no trig pending.
uint32_t sTrigReleaseAt = 0;

// Button engines (Milestone 31) — polled at 128 Hz in updateControl().
static ButtonEngine gBtnMode(PIN_BUTTON_MODE);   // mode cycle
static ButtonEngine gBtnShift(PIN_BUTTON_SHIFT); // shift / combo
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
// Core 0 ISR → gRevIn → Core 1 → gRevOut → Core 0 ISR (1-frame latency, inaudible).
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

    // -----------------------------------------------------------------------
    // M37b: populate SynthParams from goal globals and run SynthEngine
    // -----------------------------------------------------------------------
    {
        SynthParams p;
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
        p.gateHigh = gGateHigh;
        p.gatePatched = gGatePatched;
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

// M37b — thin audio ISR: delegate all DSP to SynthEngine, handle Core 1 protocol.
AudioOutput __attribute__((section(".time_critical.updateAudio"))) updateAudio() {
#ifdef CPU_PROFILE
    const uint32_t _t0 = time_us_32();
#endif

    // Feed last-frame reverb wet return (or zeros when disabled).
    const int32_t revWetL = gRevEnabled ? gRevOut_L : 0;
    const int32_t revWetR = gRevEnabled ? gRevOut_R : 0;

    int32_t outL, outR, dryL, dryR;
    gSynthEngine.audio(revWetL, revWetR, gRevMix, gRevEnabled,
                       &outL, &outR, &dryL, &dryR);

    // Wake Core 1 with the pre-reverb signal for next-frame processing.
    if (gRevEnabled) {
        gRevIn_L = dryL;
        gRevIn_R = dryR;
        gRevSampleSeq++;
        __asm volatile("sev"); // wake Core 1
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
    // reverb is pre-set by SynthEngine's constructor so this is always safe,
    // even if Core 1 reaches here before Core 0 calls init().
    gSynthEngine.reverb->reset();
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
        gRevOut_L = (int32_t)(revL * 32512.0f);
        gRevOut_R = (int32_t)(revR * 32512.0f);
    } else {
        gRevOut_L = 0;
        gRevOut_R = 0;
    }
}
