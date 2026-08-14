/**
 * AlloyFlux — Dual Relation Oscillator, Juno-inspired Eurorack voice
 * Hardware : Raspberry Pi Pico 2 (RP2350) + PCM5102A I2S DAC
 *
 * Audio: stereo I2S via PIO, 16-bit in 32-bit frames, 48000 Hz.
 * Serial dev console active when SERIAL_CONTROL is defined (build flag).
 * See reference/AlloyFlux-module-reference.md for full design specification.
 */

// ---------------------------------------------------------------------------
// Audio configuration
//
// Firmware-local on purpose: nothing under common/ may depend on these.  The
// sample rate is a property of the module, not of the platform, and a shared
// header hardcoding one rate would defeat that.  SynthEngine takes both rates
// as init() arguments.
//
// 48 kHz is exactly representable at the default 150 MHz system clock: the bit
// clock is 48000 × 32 bits × 2 channels = 3.072 MHz, and 150/3.072 = 48.828125,
// whose fraction lands exactly on 212/256 in the PIO's 16.8 divider.
//
// I2S pin assignments — PCM5102A.  WS must be BCK+1; the PIO program derives it
// and there is no way to place it elsewhere.
//   GP16 → BCK (bit clock)     GP17 → LCK (word select, implicit BCK+1)
//   GP18 → DIN (serial data)
//   3V3  → VCC, XSMT           GND → FLT, DMP, SCL, FMT (I2S slave mode)
// ---------------------------------------------------------------------------
#include <stdint.h>

static constexpr uint32_t kAudioRate   = 48000u;
static constexpr uint32_t kControlRate = 128u;

static constexpr uint8_t kPinI2sBCK  = 16u;
static constexpr uint8_t kPinI2sData = 18u;

// Reverb isolation switch:
// 1 = run reverb inline on Core 0 (single-core path, like VCV) to isolate
//     multicore transport/coherency effects.
// 0 = normal Core 1 offload path.
#ifndef REVERB_FORCE_CORE0
#define REVERB_FORCE_CORE0 0
#endif

// ---------------------------------------------------------------------------
// Includes
// ---------------------------------------------------------------------------
#include "VoiceMode.h"
#include "config_store.h"
#include "dsp/ChorusEngine.h"
#include "dsp/ShapeOsc.h"
#include "dsp/SpaceEngine.h"
#include "io/AudioDriver.h" // block I2S output + control tick
#include "io/ButtonEngine.h"
#include "io/usb_midi.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Button pin assignments (Milestone 31)
// ---------------------------------------------------------------------------
#define PIN_BUTTON_MODE 10 // GP10 — mode cycle button
#define PIN_BUTTON_SHIFT \
    11 // GP11 — shift button (secondary pot functions + combo actions)

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
#include "io/HardwarePicoIO.h" // M37d — IHardwareIO implementation for Pico
#include "io/IOBridge.h"       // M37d — fillSynthParams()
#include "io/LedEngine.h"      // M30/M37k — shared LED language
#include "io/commands.h"       // doTrig()
#include "io/serial_console.h"
#include "params.h"
// ---------------------------------------------------------------------------
// Shared synthesis parameters (declared extern in params.h)
// ---------------------------------------------------------------------------
float gBaseFreq = 440.0f;
float gColor
    = 0.0f; // COLOR knob: FM depth (PAIR/CASCADE) or Hz fine spread (ensemble)
float   gShape   = 0.0f;
float   gFatness = 0.4f; // default: sub audible but not boomy
uint8_t gSubOctave
    = 1; // 1 = one octave below (×0.5), 2 = two octaves below (×0.25)
float     gMotion     = 0.0f;           // 0.0 = static  …  1.0 = full drift
float     gDriftSpeed = 0.04f;          // one-pole glide coeff: 0.001–0.10
VoiceMode gVoiceMode = VoiceMode::PAIR; // synthesis personality (default: PAIR)
float     gRelation  = 0.0f; // 0.0 = unison, 1.0 = +2 octaves (PAIR mode)
float     gCurve     = 0.5f; // 0.0 = pluck, 0.5 = natural, 1.0 = swell
float     gCurveTime = 1.0f; // overall envelope time scale (0.25–4.0)
volatile bool gGateHigh    = false; // true while gate is asserted
volatile bool gGatePatched = false; // false = drone (bypass VCA)
float         gVolume      = 1.0f;
float         gMidiVelocity
    = 1.0f; // set by MIDI Note On; 1.0 for CV / drone / button sources
bool gVelocitySensitive
    = true; // true = MIDI velocity scales output; false = always 1.0
float gGlideTime    = 0.0f; // portamento slide time: 0.0 = instant, 0.001–2.0 s
bool  gGlideEnabled = false; // portamento on/off (CC 65)
ChorusMode gChorusMode
    = ChorusMode::I_II;      // default: Juno I+II (maximum stereo spread)
float   gSpace       = 1.0f; // stereo width: 0.0 = mono, 1.0 = full stereo
uint8_t gMidiChannel = 0;    // 0 = omni, 1–16 = specific MIDI channel
// M62 — knob takeover. SCALE (soft pickup) by default: a knob moved after the
// web/MIDI changed a parameter steers it proportionally toward the end of
// travel, so there is neither an audible jump nor a dead knob.
PotTakeoverMode gPotTakeoverMode = PotTakeoverMode::SCALE;

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
// M62 — backs the `pot sync` console command (handler lives in commands.cpp,
// which has no visibility of sHardwareIO).
void potsReattach()
{ sHardwareIO.reattachPots(); }
// M30/M37k — LED language. Platform-independent colour logic shared with the
// VCV build; writeTo() pushes the result through sHardwareIO, which shifts it
// out to the APA102 chain on GP6/GP7.
static LedEngine sLedEngine;
// Output peak-hold for LED metering — written by the audio ISR, read and
// cleared by updateControl(). Aligned int32 = atomic on Cortex-M33.
volatile int32_t gLedPeakL = 0;
volatile int32_t gLedPeakR = 0;
// Voice mode last rendered by the LEDs; a change triggers the ripple animation.
static VoiceMode sLedPrevMode = VoiceMode::PAIR;
// Set to true whenever SHIFT is consumed by a combo or knob action so the
// trig-on-release is suppressed. Reset automatically on SHIFT release.
static bool sShiftConsumed
    = false; // suppresses trig-on-release when SHIFT used in combo
static bool sModeConsumed
    = false; // suppresses mode-cycle-on-release when MODE used in combo

// ---------------------------------------------------------------------------
// M26 Post-effects engines and parameters
// ---------------------------------------------------------------------------

// Filter (M26a / M5x) — runtime-selectable algorithm.
// NOTE: FilterEngine *gFilterInst now defined in SynthEngine.cpp.
FilterType gFilterType   = FilterType::SVF;
float      gFilterCutoff = kDefaultFilterCutoff;
float      gFilterRes    = 0.0f;
FilterMode gFilterMode   = FilterMode::OFF;
// ADSR envelope params (used when gEnvelopeType == ADSR)
float gAdsrAttack  = 0.05f; // seconds
float gAdsrDecay   = 0.10f; // seconds
float gAdsrSustain = 0.8f;  // 0.0–1.0
float gAdsrRelease = 0.30f; // seconds
bool  gAdsrLoop    = false;

// Effect ordering (M26a) — 2 bool flags → 4 chain orderings.
FxOrder gFxOrder = {false, false}; // filter pre-chorus, delay pre-reverb

// Reverb (M26b) — Dattorro plate algorithm runs on Core 1.
// Core 0 ISR pushes pre-reverb frames into a small SPSC queue.
// Core 1 pops them in order and publishes the latest wet frame back to Core 0.
struct RevInFrame
{
    int32_t l;
    int32_t r;
};
static constexpr uint32_t kRevInQueueSize              = 128;
static constexpr uint32_t kRevInQueueMask              = kRevInQueueSize - 1;
volatile RevInFrame       gRevInQueue[kRevInQueueSize] = {};
volatile uint32_t         gRevInWriteIdx               = 0;
volatile uint32_t         gRevInReadIdx                = 0;
volatile uint32_t         gRevInDrops
    = 0; // diagnostic: input frames dropped due to full queue
// Fixed-delay output ring buffer — Core 1 writes wet frames here.
// Core 0 reads kRevReadDelay slots behind the write pointer, guaranteeing a
// constant 2-sample wet latency regardless of Core 1 execution jitter.
// Constant latency = constant comb = no shimmer.
// Core 0 consumes wet frames a whole block at a time (32 back-to-back in
// ~190 µs, then idle), so the ring must absorb a full block plus margin and the
// read delay must sit far enough behind the write cursor that a burst cannot
// catch it.  Sizing these too tightly lets Core 0 wrap the ring inside a single
// block while Core 1 is still producing, which scrambles the wet return into
// broadband noise.  The latency it costs must stay *constant* — that is what
// keeps the dry/wet comb fixed and the shimmer away.
static constexpr uint32_t kRevOutBufDepth
    = 128u; // power-of-2; must exceed kRevReadDelay + AudioDriver::kBlockFrames
static constexpr uint32_t kRevReadDelay
    = 48u; // fixed wet latency in frames (~1.0 ms at 48 kHz)
volatile int32_t gRevOutBuf_L[kRevOutBufDepth] = {};
volatile int32_t gRevOutBuf_R[kRevOutBufDepth] = {};
// Pre-seeded to kRevReadDelay so slots [0..kRevReadDelay-1] are valid silence
// when Core 0's read cursor starts at 0 on first reverb enable.
volatile uint32_t gRevOutWriteIdx = kRevReadDelay;

// Reverb transport state — file-scope so revGetWet/revFeedDry helpers can share them.
#if REVERB_FORCE_CORE0
static int32_t sRevPrevDryL
    = 0; // Core 0 inline: previous dry frame fed back to reverb
static int32_t sRevPrevDryR = 0;
#else
static uint32_t sRevReadIdx
    = 0; // Core 1: ISR read cursor, advances +1 per ISR tick
#endif

// 0.0 so a module with no saved config boots dry, matching both
// configStore_applyDefaults() and the REVERB knob's fully-CCW hard bypass.
volatile float gRevMix      = 0.0f;
volatile bool  gRevEnabled  = false;
volatile float gRevSize     = 0.5f;
volatile float gRevDamping  = 0.5f;
volatile float gRevModSpeed = 1.0f;  // M40: LFO rate multiplier (0.1–4.0)
volatile float gRevModDepth = 1.0f;  // M40: LFO amplitude multiplier (0.0–1.0)
volatile bool  gRevFrozen   = false; // M41: infinite sustain when true

// Delay (M26c) — ping-pong stereo delay; pass-through stub until ring buffer lands.
float gDelayTime     = 100.0f;
float gDelayFeedback = 0.5f;
float gDelayMix      = 0.0f;

// CPU profiling counters (Milestone 8) — compiled out when CPU_PROFILE is not set.
#ifdef CPU_PROFILE
volatile bool     gPerformancePrintEnabled = false;
volatile uint32_t gAudioElapsedUs          = 0;
volatile uint32_t gAudioOverruns           = 0;
// One block's wall-clock period — the budget gAudioElapsedUs is measured
// against.  Published from AudioDriver rather than hardcoded so it follows the
// sample rate and block size.
volatile uint32_t gAudioBudgetUs = 1;
#endif

// ---------------------------------------------------------------------------
// Audio driver and its two callbacks.
//
// renderAudio() produces one stereo frame; the driver calls it kBlockFrames
// times per block.  updateControl() runs at 128 Hz — the driver counts rendered
// frames and calls it on a block boundary, in thread context.
// ---------------------------------------------------------------------------
static AudioDriver sAudioDriver;

void updateControl();
void renderAudio(int32_t *outL, int32_t *outR);

void setup()
{
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
    // Load persisted config from flash before audio starts so all gXxx
    // globals are at their saved values when the first updateControl() runs.
    configStore_load(); // silently uses compile-time defaults if no valid config found
    // M37b: SynthEngine owns all DSP state; init() generates wavetables,
    // seeds all engines, and warms the powf/trig caches.
    // Must run before the driver starts so chorus delay buffers are filled.
    gSynthEngine.init(kAudioRate, kControlRate);
    gBtnMode.begin();
    gBtnShift.begin();
    // M30: claim the Dotstar GPIOs and blank the panel before audio starts, so
    // the LEDs are dark rather than showing whatever they powered up with.
    sHardwareIO.begin();
    const bool audioStarted = sAudioDriver.begin(kAudioRate,
                                                 kControlRate,
                                                 kPinI2sBCK,
                                                 kPinI2sData,
                                                 &renderAudio,
                                                 &updateControl);
    serialConsole_ready();
    // Reported after serialConsole_ready() so the line is not swallowed by the
    // USB re-enumeration wait.  A failure here means the PIO state machine
    // never started: no BCK/WS clocks at all, and the DAC sees nothing.
    if(!audioStarted)
        DLOGLN("AUDIO: I2S begin() FAILED — no bit clock, check PIO resources");
#ifdef CPU_PROFILE
    // Clear any overruns that occurred during the driver's DMA/PIO init —
    // they are not representative of steady-state audio performance.
    sAudioDriver.resetOverruns();
    gAudioOverruns = 0;
#endif
}

// Forward declarations for reverb helpers defined before renderAudio().
static void revApplyParams();
static void revGetWet(int32_t *wetL, int32_t *wetR, bool wasActive);
static void revFeedDry(int32_t dryL, int32_t dryR);

void updateControl()
{
    serialConsole_update();
#ifdef USE_TINYUSB
    usbMidi_update();
    static uint32_t sLastMidiFeedbackMs = 0;
    const uint32_t  nowMidi             = millis();
    if(nowMidi - sLastMidiFeedbackMs >= 250u)
    {
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
        if(gBtnMode.isDown() && gBtnShift.isDown())
        {
            if(!sDroneComboFired)
            {
                sDroneComboFired = true;
                sShiftConsumed   = true; // don't trig on SHIFT release
                sModeConsumed    = true; // don't cycle on MODE release
                gGatePatched     = false;
                gGateHigh        = false;
                gMidiVelocity
                    = 1.0f; // restore full volume when returning to drone/CV
#ifdef SERIAL_CONTROL
                Serial.println(F("gate -> free (drone)"));
#endif
            }
        }
        else
        {
            sDroneComboFired = false;
            // Mode solo: cycle voice mode on RELEASE so holding MODE can be used
            // as a secondary shift key for future combos (mirrors SHIFT behaviour).
            if(gBtnMode.released())
            {
                if(!sModeConsumed)
                {
                    // Cycle through implemented voice modes only.
                    static const VoiceMode kActiveModes[] = {VoiceMode::PAIR,
                                                             VoiceMode::CLOUD,
                                                             VoiceMode::CHORD,
                                                             VoiceMode::CASCADE,
                                                             VoiceMode::STRING,
                                                             VoiceMode::POLY};
                    static constexpr uint8_t kN
                        = sizeof(kActiveModes) / sizeof(kActiveModes[0]);
                    uint8_t idx = 0;
                    for(uint8_t i = 0; i < kN; i++)
                    {
                        if(kActiveModes[i] == gVoiceMode)
                        {
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
            if(gBtnShift.released())
            {
                if(!sShiftConsumed)
                {
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
    if(sTrigReleaseAt && millis() >= sTrigReleaseAt)
    {
        if(sTrigPolySlot != 255)
        {
            sPolyEnvs[sTrigPolySlot]->setGate(false);
            sPolySlots[sTrigPolySlot].midiNote = 255; // free the slot
            sTrigPolySlot                      = 255;
        }
        else
        {
            gGateHigh = false;
        }
        sTrigReleaseAt = 0;
    }

    // -----------------------------------------------------------------------
    // M37d/M37b: populate SynthParams — IO layer first, then MIDI/serial globals.
    // fillSynthParams() covers hardware-knob/CV driven fields (baseFreq, gate,
    // and since M56 the DELAY/REVERB sends).  All other fields (filter, ADSR,
    // …) continue via gXxx globals.
    // -----------------------------------------------------------------------
    {
        // M62: knobs → globals first, so everything downstream (this snapshot,
        // CC feedback, preset save, LEDs) sees one consistent set of values.
        // Knobs only win where PotTakeover says they may — a parameter last
        // set from the web holds until its knob is moved.
        sHardwareIO.updatePots();

        SynthParams p;
        fillSynthParams(sHardwareIO,
                        p); // M37d: baseFreq, gateHigh, gatePatched
        // MIDI note-on overrides V/OCT CV pitch so the played note is heard
        // rather than whatever voltage is on the V/OCT jack.
        if(sActiveNote != 255)
            p.baseFreq = gBaseFreq;
        p.shape      = gShape;
        p.fatness    = gFatness;
        p.subOctave  = gSubOctave;
        p.motion     = gMotion;
        p.driftSpeed = gDriftSpeed;
        p.voiceMode  = gVoiceMode;
        p.relation   = gRelation;
        p.curve      = gCurve;
        p.curveTime  = gCurveTime;
        // p.gateHigh / p.gatePatched — set by fillSynthParams() above.
        p.volume       = gVolume;
        p.midiVelocity = gMidiVelocity;
        p.glideTime    = gGlideTime;
        p.glideEnabled = gGlideEnabled;
        p.chorusMode   = gChorusMode;
        p.space        = gSpace;
        p.color        = gColor;
        p.envelopeType = gEnvelopeType;
        p.adsrAttack   = gAdsrAttack;
        p.adsrDecay    = gAdsrDecay;
        p.adsrSustain  = gAdsrSustain;
        p.adsrRelease  = gAdsrRelease;
        p.adsrLoop     = gAdsrLoop;
        p.filterType   = gFilterType;
        p.filterCutoff = gFilterCutoff;
        p.filterRes    = gFilterRes;
        p.filterMode   = gFilterMode;
        p.fxOrder      = gFxOrder;
        // M56: revMix/revEnabled/revSize/delayMix/delayTime now arrive from the
        // DELAY and REVERB panel knobs via fillSynthParams() — HardwarePicoIO
        // reads them back out of the gXxx globals, so MIDI/serial writes still
        // reach the engine through the same path.
        gRevEnabled     = p.revEnabled; // Core 1 loop reads the global
        p.revDamping    = gRevDamping;
        p.revModSpeed   = gRevModSpeed;
        p.revModDepth   = gRevModDepth;
        p.revFrozen     = gRevFrozen;
        p.delayFeedback = gDelayFeedback;

        SynthControlOutput co;
        gSynthEngine.control(p, sPolySlots, co);

        // -------------------------------------------------------------------
        // M30/M37k — LED language. All colour decisions live in LedEngine so
        // hardware and VCV behave identically; only the transport differs.
        // -------------------------------------------------------------------
        {
            if(p.voiceMode != sLedPrevMode)
            {
                sLedPrevMode = p.voiceMode;
                sLedEngine.notifyModeChanged();
            }

            LedSignals sig;
            if(p.voiceMode == VoiceMode::POLY)
            {
                for(uint8_t i = 0; i < 6; i++)
                {
                    if(sPolySlots[i].midiNote != 255)
                        sig.activeVoices++;
                    if(gSynthEngine.polyEnvs[i]
                       && gSynthEngine.polyEnvs[i]->level() > sig.envLevel)
                        sig.envLevel = gSynthEngine.polyEnvs[i]->level();
                }
            }
            else if(gSynthEngine.curveEng)
                sig.envLevel = gSynthEngine.curveEng->level();

            constexpr float kPeakNorm = 1.0f / 32512.0f;
            sig.peakL                 = (float)gLedPeakL * kPeakNorm;
            sig.peakR                 = (float)gLedPeakR * kPeakNorm;
            gLedPeakL                 = 0;
            gLedPeakR                 = 0;
            sig.droneMode             = !p.gatePatched;
            sig.shiftHeld             = gBtnShift.isDown();
            sig.gateHigh              = p.gateHigh;

            sLedEngine.update(p, sig, 1.0f / (float)kControlRate);
            sLedEngine.writeTo(sHardwareIO);
        }

        revApplyParams();
    }

#if defined(CPU_PROFILE) && defined(SERIAL_CONTROL)
    // Print audio ISR timing once every 5 s so it doesn't flood the console.
    // Republish the driver's block timings.  gAudioOverruns carries real DMA
    // underflows — an audible dropout — not blocks that merely ran long.
    gAudioElapsedUs = sAudioDriver.lastBlockUs();
    gAudioOverruns  = sAudioDriver.underflows();
    gAudioBudgetUs  = sAudioDriver.blockPeriodUs();

    static uint32_t lastCpuReport = 0;
    const uint32_t  now           = millis();
    if(now - lastCpuReport >= 5000)
    {
        lastCpuReport     = now;
        const uint32_t us = gAudioElapsedUs;
        if(gPerformancePrintEnabled)
        {
            const uint32_t upSec = now / 1000;
            const uint32_t mm    = upSec / 60;
            const uint32_t ss    = upSec % 60;
            // delta overruns since last auto-print interval
            static uint32_t lastAutoOver = 0;
            const uint32_t  delta        = gAudioOverruns - lastAutoOver;
            lastAutoOver                 = gAudioOverruns;
#if !REVERB_FORCE_CORE0
            static uint32_t lastRevDrops  = 0;
            const uint32_t  deltaRevDrops = gRevInDrops - lastRevDrops;
            lastRevDrops                  = gRevInDrops;
#endif
            const float budget   = (float)gAudioBudgetUs;
            float       headroom = (budget - (float)us) / budget * 100.0f;
            Serial.print(F("[cpu] "));
            Serial.print(us);
            Serial.print(F("us/"));
            Serial.print(gAudioBudgetUs);
            Serial.print(F("us  headroom "));
            Serial.print(headroom, 1);
            Serial.print(F("%  overruns "));
            Serial.print(gAudioOverruns);
            Serial.print(F(" slow-blk "));
            Serial.print(sAudioDriver.overruns());
            Serial.print(F(" (+"));
            Serial.print(delta);
            Serial.print(F("/5s)"));
#if !REVERB_FORCE_CORE0
            Serial.print(F("  revdrops +"));
            Serial.print(deltaRevDrops);
            Serial.print(F("/5s"));
#endif
            Serial.print(F("  up "));
            if(mm < 10)
                Serial.print('0');
            Serial.print(mm);
            Serial.print(':');
            if(ss < 10)
                Serial.print('0');
            Serial.println(ss);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Reverb transport helpers — hide the Core0-inline vs Core1-offload divergence
// so renderAudio() has a single unified control flow regardless of the flag.
// Both helpers are hot-path: placed in SRAM alongside renderAudio().
// ---------------------------------------------------------------------------

// revApplyParams() — called at control rate (updateControl, 128 Hz).
// Core 0:  applies reverb params directly (same core that calls process()).
// Core 1:  no-op — loop1() applies params there with the same deadbands.
static void revApplyParams()
{
#if REVERB_FORCE_CORE0
    static float sPrevSize = -1.0f, sPrevDamp = -1.0f;
    static float sPrevModSpeed = -1.0f, sPrevModDepth = -1.0f;
    static bool  sPrevFrozen = !false;
    if(fabsf(gRevSize - sPrevSize) > 0.0025f
       || fabsf(gRevDamping - sPrevDamp) > 0.0025f)
    {
        gSynthEngine.reverb->setParams(gRevSize, gRevDamping);
        sPrevSize = gRevSize;
        sPrevDamp = gRevDamping;
    }
    if(fabsf(gRevModSpeed - sPrevModSpeed) > 0.01f
       || fabsf(gRevModDepth - sPrevModDepth) > 0.01f)
    {
        gSynthEngine.reverb->setModulation(gRevModSpeed, gRevModDepth);
        sPrevModSpeed = gRevModSpeed;
        sPrevModDepth = gRevModDepth;
    }
    if(gRevFrozen != sPrevFrozen)
    {
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
static __attribute__((section(".time_critical.revGetWet"))) void
revGetWet(int32_t *wetL, int32_t *wetR, bool wasActive)
{
#if REVERB_FORCE_CORE0
    constexpr float kNorm = 1.0f / 32512.0f;
    float           wL, wR;
    gSynthEngine.reverb->process(
        (float)sRevPrevDryL * kNorm, (float)sRevPrevDryR * kNorm, &wL, &wR);
    if(wL > 1.0f)
        wL = 1.0f;
    if(wL < -1.0f)
        wL = -1.0f;
    if(wR > 1.0f)
        wR = 1.0f;
    if(wR < -1.0f)
        wR = -1.0f;
    *wetL = (int32_t)(wL * 32512.0f);
    *wetR = (int32_t)(wR * 32512.0f);
#else
    // Re-anchor read cursor when reverb transitions from disabled→enabled so we
    // don't replay stale slots from a previous session (Core 1 kept writing while
    // reverb was off; sRevReadIdx was frozen at its last value).
    if(!wasActive)
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
static __attribute__((section(".time_critical.revFeedDry"))) void
revFeedDry(int32_t dryL, int32_t dryR)
{
#if REVERB_FORCE_CORE0
    sRevPrevDryL = dryL;
    sRevPrevDryR = dryR;
#else
    const uint32_t writeIdx     = gRevInWriteIdx;
    const bool     wasEmpty     = (writeIdx == gRevInReadIdx);
    const uint32_t nextWriteIdx = (writeIdx + 1u) & kRevInQueueMask;
    // Queue full: drop oldest so newest dry frame always reaches the reverb.
    if(nextWriteIdx == gRevInReadIdx)
    {
        gRevInReadIdx = (gRevInReadIdx + 1u) & kRevInQueueMask;
        gRevInDrops++;
    }
    gRevInQueue[writeIdx].l = dryL;
    gRevInQueue[writeIdx].r = dryR;
    __asm volatile("dmb" ::: "memory");
    gRevInWriteIdx = nextWriteIdx;
    if(wasEmpty)
        __asm volatile("sev"); // wake Core 1 on empty→non-empty transition
#endif
}

// Thin audio render: delegates all DSP to SynthEngine and handles the reverb
// transport.  Called kBlockFrames times per block by AudioDriver::pump().
void __attribute__((section(".time_critical.renderAudio")))
renderAudio(int32_t *pOutL, int32_t *pOutR)
{
#ifdef AUDIO_TEST_TONE
    // Bring-up aid: 1 kHz sine straight out of the driver, bypassing the
    // engine, effects chain and envelope.  Bisects "the I2S path is broken"
    // from "the engine is producing silence" — flat on a scope means the fault
    // is below renderAudio(); clean means it is in gate/envelope/level state.
    // Build with: pio run -e alloyflux -a "-DAUDIO_TEST_TONE"
    {
        static float    sPhase = 0.0f;
        constexpr float kTwoPi = 6.28318530718f;
        constexpr float kInc   = kTwoPi * 1000.0f / (float)kAudioRate;
        const int32_t   s = (int32_t)(sinf(sPhase) * 16000.0f); // ~ -6 dBFS
        sPhase += kInc;
        if(sPhase >= kTwoPi)
            sPhase -= kTwoPi;
        *pOutL = s;
        *pOutR = s;
        return;
    }
#endif

    static bool sPrevRevActive = false;
    int32_t     revWetL = 0, revWetR = 0;
    if(gRevEnabled)
        revGetWet(&revWetL, &revWetR, sPrevRevActive);
    sPrevRevActive = (bool)gRevEnabled;

    int32_t outL, outR, dryL, dryR;
    gSynthEngine.audio(
        revWetL, revWetR, gRevMix, gRevEnabled, &outL, &outR, &dryL, &dryR);

    if(gRevEnabled)
        revFeedDry(dryL, dryR);

    // M37k — LED metering. Integer peak-hold only (abs + compare, no FPU,
    // no allocation); updateControl() reads and clears it at 128 Hz.
    {
        const int32_t aL = outL < 0 ? -outL : outL;
        const int32_t aR = outR < 0 ? -outR : outR;
        if(aL > gLedPeakL)
            gLedPeakL = aL;
        if(aR > gLedPeakR)
            gLedPeakR = aR;
    }

    *pOutL = outL;
    *pOutR = outR;
}

// The render loop.  pump() queues one block whenever the I2S DMA has room, so
// the bit clock paces this loop and no timer is involved.  updateControl() is
// invoked from inside pump(), keeping it in thread context.
void loop()
{ sAudioDriver.pump(); }

// ---------------------------------------------------------------------------
// Core 1 — reserved for future DSP offload (reverb, filter — M26+)
// ---------------------------------------------------------------------------

void setup1()
{
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
void __attribute__((section(".time_critical.loop1"))) loop1()
{
#if REVERB_FORCE_CORE0
    // Reverb runs on Core 0 in isolation mode.
    __asm volatile("wfe");
    return;
#else
    static float sPrevRevSize     = -1.0f;
    static float sPrevRevDamp     = -1.0f;
    static float sPrevRevModSpeed = -1.0f;
    static float sPrevRevModDepth = -1.0f;
    static bool  sPrevRevFrozen   = !false;

    const float curRevSize     = gRevSize;
    const float curRevDamp     = gRevDamping;
    const float curRevModSpeed = gRevModSpeed;
    const float curRevModDepth = gRevModDepth;
    const bool  curRevFrozen   = gRevFrozen;

    // Apply with deadbands to suppress ADC/control jitter churn.
    // Exact float comparisons can retrigger at control rate (128 Hz), causing
    // unnecessary reverb reconfiguration and inter-core contention.
    if(fabsf(curRevSize - sPrevRevSize) > 0.0025f
       || fabsf(curRevDamp - sPrevRevDamp) > 0.0025f)
    {
        gSynthEngine.reverb->setParams(curRevSize, curRevDamp);
        sPrevRevSize = curRevSize;
        sPrevRevDamp = curRevDamp;
    }
    if(fabsf(curRevModSpeed - sPrevRevModSpeed) > 0.01f
       || fabsf(curRevModDepth - sPrevRevModDepth) > 0.01f)
    {
        gSynthEngine.reverb->setModulation(curRevModSpeed, curRevModDepth);
        sPrevRevModSpeed = curRevModSpeed;
        sPrevRevModDepth = curRevModDepth;
    }
    if(curRevFrozen != sPrevRevFrozen)
    {
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
    if(gRevEnabled)
    {
        if(gRevInReadIdx == gRevInWriteIdx)
        {
            return; // queue empty — spin-poll: SDK calls loop1() immediately
        }

        const uint32_t readIdx = gRevInReadIdx;
        __asm volatile("dmb" ::: "memory");
        const int32_t inL_i = gRevInQueue[readIdx].l;
        const int32_t inR_i = gRevInQueue[readIdx].r;
        gRevInReadIdx       = (readIdx + 1u) & kRevInQueueMask;

        // Normalise ±32512 int32 → ±1.0f for the reverb algorithm.
        const float inL = (float)inL_i * (1.0f / 32512.0f);
        const float inR = (float)inR_i * (1.0f / 32512.0f);
        float       revL, revR;
        gSynthEngine.reverb->process(inL, inR, &revL, &revR);

        // Clamp before converting back — algorithmic edge cases can spike.
        if(revL > 1.0f)
            revL = 1.0f;
        if(revL < -1.0f)
            revL = -1.0f;
        if(revR > 1.0f)
            revR = 1.0f;
        if(revR < -1.0f)
            revR = -1.0f;

        {
            const uint32_t wi = gRevOutWriteIdx & (kRevOutBufDepth - 1u);
            gRevOutBuf_L[wi]  = (int32_t)(revL * 32512.0f);
            gRevOutBuf_R[wi]  = (int32_t)(revR * 32512.0f);
            __asm volatile("dmb" ::: "memory");
            gRevOutWriteIdx++;
        }
    }
    else
    {
        gRevInReadIdx = gRevInWriteIdx; // flush any stale queued dry frames
        // Ring buffer not updated when disabled; Core 0 ignores it (gRevEnabled=false).
    }
#endif
}
