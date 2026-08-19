/**
 * AlloyFlux — Dual Relation Oscillator, Juno-inspired Eurorack voice
 * Hardware : Raspberry Pi Pico 2 (RP2350) + PCM5102A I2S DAC
 *
 * Audio: stereo I2S via PIO, 16-bit in 32-bit frames, 48000 Hz.
 * Serial dev console active when SERIAL_CONTROL is defined (build flag).
 * See reference/AlloyFlux-module-reference.md for full design specification.
 *
 * Core split (M63b2):
 *   Core 1 — the whole audio path.  Owns the I2S driver and runs every DSP
 *            engine, reverb included, inside renderAudio().
 *   Core 0 — everything else: knobs, CV, buttons, LEDs, USB MIDI, serial
 *            console, flash.  Paced by the driver's control-tick counter, so
 *            the control rate stays derived from the audio clock.
 *
 * Control writes engine state that audio reads without a lock.  That is safe
 * here because every such value is a single aligned word (atomic on the M33)
 * and is smoothed at control rate, so the worst a torn multi-field update can
 * produce is one sample computed from two adjacent parameter values.  Anything
 * that resizes a buffer would not be safe, and there is none.
 */

// ---------------------------------------------------------------------------
// Audio configuration
//
// Module-local on purpose: nothing under platform/ may depend on these.  The
// sample rate is a property of the module — it is what lets Audrey run at a
// different one — and a shared header hardcoding a value would defeat that.
// SynthEngine takes both rates as init() arguments.
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

// ---------------------------------------------------------------------------
// Includes
// ---------------------------------------------------------------------------
#include "VoiceMode.h"
#include "alloy_config.h"
#include "config_store.h" // platform: save/load/reset
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
#include "SynthEngine.h" // M37b: SynthParams, SynthEngine
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
float     gRelation  = 0.0f;       // 0.0 = unison, 1.0 = +2 octaves (PAIR mode)
float     gCurve     = 0.5f;       // 0.0 = pluck, 0.5 = natural, 1.0 = swell
float     gCurveTime = 1.0f;       // overall envelope time scale (0.25–4.0)
float     gGateLength      = 0.0f; // GATE note length ms; 0 = follow the gate
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
    = ChorusMode::I_II; // default: Juno I+II (maximum stereo spread)
float gSpace = 1.0f;    // stereo width: 0.0 = mono, 1.0 = full stereo
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

// POLY + GATE jack. The slot the current CV gate owns, and the previous gate
// level for edge detection. 255 = no gate-owned slot.
//
// Each rising edge on GATE claims the next slot round-robin and plays the
// pitch on V/OCT, so a sequence of gates stacks voices that ring together
// rather than one voice retriggering — the behaviour the VCV build has had
// since M37i. The falling edge releases that voice but leaves the slot marked
// kPolySlotReleasing, so the tail is audible and the slot is still preferred
// for reuse over stealing a held note.
static uint8_t sCvPolySlot = 255;
static bool    sPrevCvGate = false;

// Per-slot auto-release deadlines for gGateLength > 0, in millis(). 0 = the
// slot has no timed release pending.
//
// One per slot rather than the single sTrigReleaseAt the serial `trig` command
// uses, because the whole point is that timed notes overlap: six of them can be
// counting down at once, which a single deadline cannot express.
static uint32_t sCvPolyReleaseAt[6] = {0, 0, 0, 0, 0, 0};

// The firmware's engine instance.  File-scope, not a shared global: nothing
// outside this file needs to know the engine exists (M63d).
static SynthEngine gSynthEngine;

// Button engines (Milestone 31) — polled at 128 Hz in updateControl().
static ButtonEngine gBtnMode(PIN_BUTTON_MODE);   // mode cycle
static ButtonEngine gBtnShift(PIN_BUTTON_SHIFT); // shift / combo
// M37d — hardware IO abstraction layer; owns readPot/readCV/readButton/writeLight.
static HardwarePicoIO sHardwareIO(gBtnMode, gBtnShift);
// M62 — backs the `pot sync` console command (handler lives in commands.cpp,
// which has no visibility of sHardwareIO).
void potsReattach()
{ sHardwareIO.reattachPots(); }

// M63d — the one entry point into the engine for anything that starts a note.
// Declared in params.h; defined here because this is where the engine lives.
// usb_midi.cpp and commands.cpp used to hold identical copies of this allocator
// and each reached into gSynthEngine directly to finish the job.
//
// MIDI, the serial `trig` command and the GATE jack all come through here, and
// they share one pool of six slots rather than each owning a fixed subset — a
// gate is simply another note source. Preference order matters: a slot still
// ringing out its release tail (kPolySlotReleasing) is a worse choice than a
// silent one but a better choice than stealing a note that is still held, so it
// sits between the two.
uint8_t polyNoteOn(float freq, float velocity, float subMult, uint8_t noteTag)
{
    uint8_t slot    = 255;
    uint8_t relSlot = 255;
    for(uint8_t i = 0; i < 6; i++)
    {
        const uint8_t idx = (sPolyRR + i) % 6;
        if(sPolySlots[idx].midiNote == kPolySlotFree)
        {
            slot = idx;
            break;
        }
        if(sPolySlots[idx].midiNote == kPolySlotReleasing && relSlot == 255)
            relSlot = idx;
    }
    if(slot == 255)
    {
        // Nothing free: take the oldest release tail, else steal round-robin.
        slot = (relSlot != 255) ? relSlot : (uint8_t)(sPolyRR % 6);
    }
    sPolyRR = (sPolyRR + 1) % 6;

    sPolySlots[slot].freq     = freq;
    sPolySlots[slot].velocity = velocity;
    sPolySlots[slot].midiNote = noteTag;
    gSynthEngine.polyRetrigger(slot, freq, subMult);
    return slot;
}
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

// Reverb (M26b) — Dattorro plate, processed inline in renderAudio() alongside
// the rest of the DSP.  Wet latency is one sample, constant by construction.
//
// It used to live on its own core behind a pair of ring buffers, which cost a
// variable dry→wet delay — a swept comb, audible as shimmer — and had to be
// re-tuned every time the audio cadence changed.  Now that the whole audio path
// owns Core 1 there is nothing left to hand across.

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
// Audio driver.  Started and pumped by Core 1; renderAudio() produces one
// stereo frame and the driver calls it kBlockFrames times per block.
//
// updateControl() runs on Core 0 at kControlRate, paced by the driver's
// control-tick counter — see loop().
// ---------------------------------------------------------------------------
static AudioDriver sAudioDriver;

// Core 0 → Core 1: set once Core 0 has finished bringing up the engine, config
// and I/O.  Core 1 must not start the audio clock before this, or the first
// blocks render against half-initialised wavetables and empty delay lines.
//
// Core 1 → Core 0: the outcome of AudioDriver::begin(), so Core 0 can report a
// failed I2S start and fall back to pacing control from millis().
enum AudioState : uint8_t
{
    kAudioStarting = 0,
    kAudioRunning  = 1,
    kAudioFailed   = 2
};
static volatile bool       sCore0Ready = false;
static volatile AudioState sAudioState = kAudioStarting;

void updateControl();
void renderAudio(float *outL, float *outR);

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
    // USB MIDI before the serial console, so both interfaces are claimed and
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

    // Release Core 1: everything it renders from is now built.  The barrier
    // orders the init stores above ahead of the flag, so Core 1 cannot observe
    // the release before the state it guards.
    __asm volatile("dmb" ::: "memory");
    sCore0Ready = true;
    while(sAudioState == kAudioStarting)
        tight_loop_contents();

    serialConsole_ready();
    // Reported after serialConsole_ready() so the line is not swallowed by the
    // USB re-enumeration wait.  A failure here means the PIO state machine
    // never started: no BCK/WS clocks at all, and the DAC sees nothing.
    if(sAudioState == kAudioFailed)
        DLOGLN("AUDIO: I2S begin() FAILED — no bit clock, check PIO resources");
#ifdef CPU_PROFILE
    // Clear any overruns that occurred during the driver's DMA/PIO init —
    // they are not representative of steady-state audio performance.
    sAudioDriver.resetOverruns();
    gAudioOverruns = 0;
#endif
}

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
            sPolySlots[sTrigPolySlot].midiNote = kPolySlotFree; // free the slot
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
        gRevEnabled     = p.revEnabled; // renderAudio() reads the global
        p.revDamping    = gRevDamping;
        p.revModSpeed   = gRevModSpeed;
        p.revModDepth   = gRevModDepth;
        p.revFrozen     = gRevFrozen;
        p.delayFeedback = gDelayFeedback;

        SynthControlOutput co;
        gSynthEngine.control(p, sPolySlots, co);

        // -------------------------------------------------------------------
        // POLY + GATE jack — round-robin note allocation from CV.
        //
        // Each rising edge claims the next slot and plays whatever pitch is on
        // V/OCT, so a run of gates stacks voices that ring together instead of
        // one voice retriggering. The falling edge releases that voice but
        // leaves the slot marked releasing, so its tail stays audible.
        //
        // Runs *after* control() on purpose: control() frees every slot on the
        // tick it sees a mode change, so a voice claimed before it would be
        // erased on the way into POLY. Ordering it here costs nothing, because
        // polyRetrigger() sets the oscillator frequency, resets phase and arms
        // the envelope immediately — exactly the path a MIDI note already
        // takes when it arrives between two control ticks.
        //
        // In POLY the mono envelope is unused (control() skips its gate
        // handling), so the gate's only job here is per-voice envelopes.
        //
        // gGateLength splits this into two behaviours:
        //   0  — follow the gate. The falling edge releases the voice.
        //   >0 — trigger. The gate width is ignored and the voice is released
        //        gGateLength ms after it started, so an arpeggio builds into a
        //        chord even at sustaining CURVE settings, where the envelope
        //        would otherwise hold each voice until its gate fell.
        // -------------------------------------------------------------------
        {
            // Shared by both branches; a slot's release is identical either way.
            auto releaseCvSlot = [](uint8_t slot)
            {
                // Only release if the slot is still ours. A panic (CC 123) or a
                // mode change can free it underneath us, and it may already
                // have been re-claimed by a MIDI note we must not cut off.
                if(slot < 6 && sPolySlots[slot].midiNote == kPolySlotCvHeld)
                {
                    if(sPolyEnvs[slot])
                        sPolyEnvs[slot]->setGate(false);
                    sPolySlots[slot].midiNote = kPolySlotReleasing;
                }
                if(slot < 6)
                    sCvPolyReleaseAt[slot] = 0;
            };

            if(p.voiceMode == VoiceMode::POLY)
            {
                const bool     timed   = (gGateLength > 0.5f);
                const bool     gateNow = p.gateHigh;
                const uint32_t now     = millis();

                if(gateNow && !sPrevCvGate)
                {
                    const float   subMult = (p.subOctave == 2) ? 0.25f : 0.5f;
                    const uint8_t slot    = polyNoteOn(
                        p.baseFreq, 1.0f, subMult, kPolySlotCvHeld);
                    if(timed)
                    {
                        // Trigger mode: arm this slot's own deadline and do not
                        // track it as "the" gate-held slot, so the next gate is
                        // free to claim another voice while this one runs on.
                        sCvPolyReleaseAt[slot] = now + (uint32_t)gGateLength;
                        sCvPolySlot            = 255;
                    }
                    else
                    {
                        sCvPolyReleaseAt[slot] = 0;
                        sCvPolySlot            = slot;
                    }
                }
                else if(!gateNow && sPrevCvGate && !timed)
                {
                    releaseCvSlot(sCvPolySlot);
                    sCvPolySlot = 255;
                }
                sPrevCvGate = gateNow;

                // Timed releases. Compare as a signed difference so the
                // millis() rollover at ~49.7 days is a non-event.
                for(uint8_t i = 0; i < 6; i++)
                {
                    if(sCvPolyReleaseAt[i]
                       && (int32_t)(now - sCvPolyReleaseAt[i]) >= 0)
                        releaseCvSlot(i);
                }
            }
            else
            {
                // Leaving POLY abandons any gate-owned slot; control() frees
                // the slots themselves on the mode-change tick.
                sCvPolySlot = 255;
                sPrevCvGate = false;
                for(uint8_t i = 0; i < 6; i++)
                    sCvPolyReleaseAt[i] = 0;
            }
        }

        // Reap gate-owned slots whose release tail has decayed to silence.
        // Held slots and MIDI notes are left alone.
        for(uint8_t i = 0; i < 6; i++)
        {
            if(sPolySlots[i].midiNote == kPolySlotReleasing && sPolyEnvs[i]
               && sPolyEnvs[i]->level() < 0.001f)
                sPolySlots[i].midiNote = kPolySlotFree;
        }

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
                    if(sPolySlots[i].midiNote != kPolySlotFree)
                        sig.activeVoices++;
                    if(gSynthEngine.polyEnvs[i]
                       && gSynthEngine.polyEnvs[i]->level() > sig.envLevel)
                        sig.envLevel = gSynthEngine.polyEnvs[i]->level();
                }
            }
            else if(gSynthEngine.curveEng)
                sig.envLevel = gSynthEngine.curveEng->level();

            sig.peakL     = (float)gLedPeakL * kSignalToFloat;
            sig.peakR     = (float)gLedPeakR * kSignalToFloat;
            gLedPeakL     = 0;
            gLedPeakR     = 0;
            sig.droneMode = !p.gatePatched;
            sig.shiftHeld = gBtnShift.isDown();
            sig.gateHigh  = p.gateHigh;

            sLedEngine.update(p, sig, 1.0f / (float)kControlRate);
            sLedEngine.writeTo(sHardwareIO);
        }
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
            const float budget           = (float)gAudioBudgetUs;
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

// Thin audio render: delegates all DSP to SynthEngine and closes the reverb
// loop.  Called kBlockFrames times per block by AudioDriver::pump() on Core 1.
void __attribute__((section(".time_critical.renderAudio")))
renderAudio(float *pOutL, float *pOutR)
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
        const float     s      = sinf(sPhase) * 0.5f; // −6 dBFS
        sPhase += kInc;
        if(sPhase >= kTwoPi)
            sPhase -= kTwoPi;
        *pOutL = s;
        *pOutR = s;
        return;
    }
#endif

    // Sampled once: Core 0 can change either between statements, and the wet
    // return must be mixed on the same terms it was fetched under.
    const bool  revOn  = gRevEnabled;
    const float revMix = gRevMix;

    // Reverb runs one sample behind: this frame's dry has not been computed
    // yet, so it processes the previous one.  A fixed one-sample offset keeps
    // the dry/wet comb stationary, which is what stops it shimmering.
    static int32_t sRevPrevDryL = 0;
    static int32_t sRevPrevDryR = 0;
    int32_t        revWetL = 0, revWetR = 0;
    if(revOn)
    {
        float wL, wR;
        gSynthEngine.reverb->process((float)sRevPrevDryL * kSignalToFloat,
                                     (float)sRevPrevDryR * kSignalToFloat,
                                     &wL,
                                     &wR);
        // Clamp before scaling back — algorithmic edge cases can spike past
        // unity and wrapping the int conversion sounds like a gunshot.
        if(wL > 1.0f)
            wL = 1.0f;
        else if(wL < -1.0f)
            wL = -1.0f;
        if(wR > 1.0f)
            wR = 1.0f;
        else if(wR < -1.0f)
            wR = -1.0f;
        revWetL = (int32_t)(wL * kFloatToSignal);
        revWetR = (int32_t)(wR * kFloatToSignal);
    }

    int32_t outL, outR, dryL, dryR;
    gSynthEngine.audio(
        revWetL, revWetR, revMix, revOn, &outL, &outR, &dryL, &dryR);

    // Kept current even while the reverb is bypassed, so re-enabling it feeds
    // the frame that just played rather than one from the last time it was on.
    sRevPrevDryL = dryL;
    sRevPrevDryR = dryR;

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

    // The module's edge: internal signal convention → the platform's float
    // ±1.0.  kSignalFullScale maps to 1.0, so this is 0.07 dB louder than the
    // pre-M63d path, which mapped it to 32512/32767 of full scale.
    *pOutL = (float)outL * kSignalToFloat;
    *pOutR = (float)outR * kSignalToFloat;
}

// Core 0's loop — control only.  The audio clock paces it: pump() bumps a
// counter once per control period on Core 1, and a change here means one is
// due.  Deriving the rate from the audio clock rather than from millis() keeps
// it exact, which matters because SynthEngine computes its smoothing and glide
// coefficients from kControlRate.
//
// A missed tick is skipped, not made up.  Everything driven from here — one-pole
// smoothing, drift, LED animation, button debounce — is a rate rather than a
// queue, so running two ticks back to back would double-advance them; running
// one late costs nothing anyone can hear.
void loop()
{
    if(sAudioState == kAudioFailed)
    {
        // No bit clock means no tick either.  Fall back to millis() so the
        // console and USB MIDI still come up and the fault is diagnosable
        // instead of presenting as a dead board.
        static uint32_t sLastMs = 0;
        const uint32_t  now     = millis();
        if(now - sLastMs < (1000u / kControlRate))
            return;
        sLastMs = now;
        updateControl();
        return;
    }

    static uint32_t sLastTick = 0;
    const uint32_t  tick      = sAudioDriver.controlTicks();
    if(tick == sLastTick)
    {
        tight_loop_contents();
        return;
    }
    sLastTick = tick;
    updateControl();
}

// ---------------------------------------------------------------------------
// Core 1 — the audio path, in full.
// ---------------------------------------------------------------------------

void setup1()
{
    // Enable Flush-to-Zero mode on Core 1's FPU.
    // Reverb and filter tails decay into denormal territory; without FTZ the
    // FPU takes ~100x longer per operation and the block overruns its budget.
    {
        uint32_t fpscr;
        asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1u << 24); // FZ: Flush-to-Zero
        asm volatile("vmsr fpscr, %0" ::"r"(fpscr));
    }

    // Core 1 is launched before setup() runs, so wait for Core 0 to finish
    // building the engine before starting the clock that consumes it.
    while(!sCore0Ready)
        tight_loop_contents();
    __asm volatile("dmb" ::: "memory");

    // Started here, not in setup(): the I2S library enables DMA_IRQ_0 on the
    // calling core, and its buffer bookkeeping assumes the DMA handler and the
    // writer share one core.
    const bool ok = sAudioDriver.begin(
        kAudioRate, kControlRate, kPinI2sBCK, kPinI2sData, &renderAudio);
    sAudioState = ok ? kAudioRunning : kAudioFailed;
}

// loop1 in SRAM: eliminates XIP-cache code-fetch jitter, which would otherwise
// make block render time vary with the instruction cache rather than with the
// work actually being done — and Core 0 evicts that cache whenever it touches
// flash.
void __attribute__((section(".time_critical.loop1"))) loop1()
{ sAudioDriver.pump(); }
