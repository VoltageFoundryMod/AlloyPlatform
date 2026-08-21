/**
 * Alloy Coil — feedback resonator, Alloy platform module.
 * Hardware : Raspberry Pi Pico 2 (RP2350) + PCM5102A I2S DAC
 *
 * Engine by Synthux Academy (Nick Donaldson / Roey Tsemah), vendored from
 * alloycoil-ii-simple — see modules/alloycoil/README.md and CREDITS.md.
 *
 * Core split, as for every module on this platform:
 *   Core 1 — the whole audio path: owns the I2S driver, runs renderAudio().
 *   Core 0 — controls, USB MIDI, serial console, flash. Paced by the driver's
 *            control-tick counter, so the control rate follows the audio clock.
 *
 * ⚠ Panel I/O is only half wired. The two switches — SW2 (WARP) and SW3
 * (SHIFT) — are read here; the ADC mux, the CV jacks and the LED chain are not,
 * so every *parameter* still arrives over USB MIDI, SysEx or the serial console.
 * That split is the proto board's, not a decision: the buttons are two GPIOs
 * and the rest is a mux that has not been built. io/HardwarePicoIO.h holds the
 * seam, and it answers for the unwired half in a way that stays correct when
 * the rest lands.
 */

#include <stdint.h>

// ---------------------------------------------------------------------------
// Audio configuration
//
// Module-local, like every module's: the sample rate is a property of the
// engine, not the platform.
//
// Alloy Coil runs at 48 kHz rather than AlloyFlux's historical 32768 Hz for a
// concrete reason — its FeedbackLPFCutoff defaults to 18 kHz, which at 32768 Hz
// would sit *above* the 16384 Hz Nyquist, where the biquad's bilinear prewarp
// diverges. The filter would be degenerate at boot. At 48 kHz it is not.
//
// This module runs the core at 192 MHz rather than the stock 150 — the engine
// needs ~690 µs per block against a 666 µs budget at 150 MHz. 192 keeps the
// I2S divider exact (192 / 3.072 MHz = 62.5); see the rationale and the table
// of safe clocks in platformio.ini's [env:alloycoil].
// ---------------------------------------------------------------------------
static constexpr uint32_t kAudioRate   = 48000u;
static constexpr uint32_t kControlRate = 128u;

// How often the parameter smoother interpolates, in audio frames.
//
// 128 Hz is an I/O rate — it is how often a CC or a knob needs looking at — and
// it is far too coarse to interpolate *with*: a 7.8 ms tick is longer than the
// 7.2 ms tau a 50 ms glide asks for, so a one-pole stepped there clamps to 1.0
// and does nothing at all. Nine of the twelve parameters use that 50 ms time.
//
// So the goals are read at the control tick and the glide runs here, once per
// 32 frames = 1500 Hz, which is 75 steps across a 50 ms move. Matches
// AudioDriver::kBlockFrames, but nothing requires it to: it is a smoothing
// rate, not a block boundary. See ControlSmoother.h.
#ifndef COIL_SMOOTH_FRAMES
#define COIL_SMOOTH_FRAMES 32
#endif
static constexpr uint32_t kSmoothFrames = COIL_SMOOTH_FRAMES;


static constexpr uint8_t kPinI2sBCK  = 16u; // GP16; WS is implicitly GP17
static constexpr uint8_t kPinI2sData = 18u;

#include "ControlSmoother.h"
#include "FeedbackSynthEngine.h"
#include "OutputStage.h"
#include "coil_config.h"
#include "config_store.h" // platform: save/load/reset
#include "debug.h"
#include "io/AudioDriver.h"
#include "io/ButtonEngine.h"
#include "io/HardwarePicoIO.h" // IHardwareIO — buttons live, ADC/CV/LEDs pending
#include "io/IOBridge.h"       // fillCoilButtons()
#include "io/PanelMap.h"       // Btn:: — Alloy Coil's slot names
#include "io/serial_console.h"
#include "io/usb_midi.h"
#include "params.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Parameter globals — one per params.json row. Written by MIDI/SysEx/serial,
// read once per control tick and pushed into the engine.
//
// Defined by the generated header, initialised to params.json's `default`
// column. This is the one translation unit in the firmware image that includes
// it; see the header for why that matters.
// ---------------------------------------------------------------------------
#include "param_globals.generated.h"

// External excitation — see params.h. Control-rate on this platform.
volatile float gExciterIn = 0.0f;

// ---------------------------------------------------------------------------
// Audio-path cost switches — runtime, not compile-time, so the two things M63i
// added to the block can be turned off on a live board with `cpu` running
// rather than by reflashing three times. Read on the audio core every frame.
//
//   smooth <n>    Step() every n frames; 0 disables it entirely
//   limiter <0|1> bypass the output stage
//
// Both default to the shipping behaviour. See commands.cpp.
// ---------------------------------------------------------------------------
volatile uint32_t gSmoothFrames   = kSmoothFrames;
volatile bool     gLimiterEnabled = true;

#ifdef CPU_PROFILE
volatile bool     gPerformancePrintEnabled = false;
volatile uint32_t gAudioElapsedUs          = 0;
volatile uint32_t gAudioOverruns           = 0;
volatile uint32_t gAudioBudgetUs           = 1;
#endif

// ---------------------------------------------------------------------------
// Engine and driver
// ---------------------------------------------------------------------------

// File-scope, not a shared global: nothing outside this file needs to know the
// engine exists. ~443 KiB of static storage — see modules/alloycoil/README.md.
static infrasonic::FeedbackSynth::Engine gEngine;

// The glide between the goal values and the engine, and the peak limiter
// between the engine and the DAC. Both are upstream behaviour that the port did
// not carry over at first; see ControlSmoother.h and OutputStage.h.
static infrasonic::FeedbackSynth::ControlSmoother sSmoother;
static infrasonic::FeedbackSynth::OutputStage     sOutput;

static AudioDriver sAudioDriver;

// ---------------------------------------------------------------------------
// Panel buttons — SW2 (WARP) and SW3 (SHIFT), polled at the control tick.
//
// The rest of the panel is not here yet: no pot mux, no CV jacks, no LED chain.
// The buttons are, so they work. sHardwareIO is what turns "GP10 is low" into
// "Btn::WARP is down" without main.cpp learning either the pin or the parameter
// — the same seam AlloyFlux reads its two switches through, and the one the ADC
// driver will slot into.
// ---------------------------------------------------------------------------
static ButtonEngine   gBtnWarp(PIN_BUTTON_WARP);   // SW2 — doppler warp, held
static ButtonEngine   gBtnShift(PIN_BUTTON_SHIFT); // SW3 — knob secondaries
static HardwarePicoIO sHardwareIO(gBtnWarp, gBtnShift);
// Last button levels the bridge acted on. It writes gWarp on the edges only, so
// that CC 20 can own the flag between presses — see io/IOBridge.h.
static CoilButtonState sBtnState;

// Backs the `status` command's button line — commands.cpp has no visibility of
// the engines above, and a proto board with no LEDs has no other way to tell a
// miswired switch from a dead one. Declared in params.h.
uint8_t coilButtonsDown()
{
    return (uint8_t)((gBtnWarp.isDown() ? 0x1u : 0u)
                     | (gBtnShift.isDown() ? 0x2u : 0u));
}

// Core 0 → Core 1: Core 1 must not start the audio clock before the engine is
// built. Core 1 → Core 0: the outcome of AudioDriver::begin().
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
    // Flush-to-Zero on Core 0's FPU. Denormals cost ~100x on Cortex-M33.
    {
        uint32_t fpscr;
        asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1u << 24);
        asm volatile("vmsr fpscr, %0" ::"r"(fpscr));
    }

    // USB MIDI before the serial console, so both interfaces are claimed and
    // appear in the same USB descriptor on first host enumeration.
#ifdef USE_TINYUSB
    usbMidi_init();
#endif
    serialConsole_init();

    // Compile-time defaults first, then the saved slot on top if there is one.
    // A slot written by another module is skipped, not misread — that is what
    // the engine tag in ConfigSlot is for.
    configStore_applyDefaults();
    configStore_load();

    // Claim the two button GPIOs (INPUT_PULLUP, active low) and resolve each
    // pot slot to its manifest row. Both are cheap and neither touches audio,
    // so they go here with the rest of the once-only setup.
    gBtnWarp.begin();
    gBtnShift.begin();
    sHardwareIO.begin();

    gEngine.Init((float)kAudioRate);
    sOutput.Init();
    // Stepped once per kSmoothFrames in renderAudio(), so that — not the
    // control tick and not the sample rate — is the rate its coefficients
    // belong to.
    sSmoother.Init((float)kAudioRate / (float)kSmoothFrames);

    __asm volatile("dmb" ::: "memory");
    sCore0Ready = true;
    while(sAudioState == kAudioStarting)
        tight_loop_contents();

    serialConsole_ready();
    if(sAudioState == kAudioFailed)
        DLOGLN("AUDIO: I2S begin() FAILED — no bit clock, check PIO resources");
#ifdef CPU_PROFILE
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

    // Panel buttons, at the control tick ButtonEngine's ~31 ms debounce window
    // is specified against. Poll both before reading either, so a combo would
    // see one consistent tick — Alloy Coil has no combos yet, and this is the
    // ordering the one it eventually gets will need.
    //
    // fillCoilButtons() rather than fillCoilParams(): the buttons are wired and
    // the pot mux is not, and the full bridge would drive twelve parameters
    // from an ADC that does not exist. Switching to fillCoilParams() is the
    // whole change when it does — HardwarePicoIO already answers every other
    // question the bridge asks, honestly, and test/params_check.cpp covers what
    // it will then be doing. See io/HardwarePicoIO.h.
    gBtnWarp.poll();
    gBtnShift.poll();
    fillCoilButtons(sHardwareIO, sBtnState);

    // Nothing else here talks to the engine. This tick's job is to bring
    // the gXxx *goal* values up to date from MIDI, SysEx and the console; the
    // glide onto them, and the engine writes it produces, belong to
    // sSmoother.Step() in renderAudio(). Splitting the two is what lets the
    // goals be read at a sensible I/O rate while the interpolation runs fast
    // enough for a 50 ms glide to exist — see kSmoothFrames above.
    //
    // It also puts every write to engine state on the audio core, which is
    // where it was always read: SetFeedbackLPFCutoff() re-solves five biquad
    // coefficients, and doing that from this core meant Process() could be
    // halfway through reading them.

#if defined(CPU_PROFILE) && defined(SERIAL_CONTROL)
    gAudioElapsedUs = sAudioDriver.lastBlockUs();
    gAudioOverruns  = sAudioDriver.underflows();
    gAudioBudgetUs  = sAudioDriver.blockPeriodUs();

    static uint32_t lastCpuReport = 0;
    const uint32_t  now           = millis();
    if(now - lastCpuReport >= 5000)
    {
        lastCpuReport = now;
        if(gPerformancePrintEnabled)
        {
            const float budget = (float)gAudioBudgetUs;
            const float headroom
                = (budget - (float)gAudioElapsedUs) / budget * 100.0f;
            Serial.print(F("[cpu] "));
            Serial.print(gAudioElapsedUs);
            Serial.print(F("us/"));
            Serial.print(gAudioBudgetUs);
            Serial.print(F("us  headroom "));
            Serial.print(headroom, 1);
            Serial.print(F("%  underruns "));
            Serial.print(gAudioOverruns);
            Serial.print(F("  slow-blk "));
            Serial.print(sAudioDriver.overruns());
            // Should be 0 whenever nothing is being moved — see params.h.
            Serial.print(F("  set/step "));
            Serial.println(coilSmootherPushes());
        }
    }
#endif
}

// Audio render. Alloy Coil is float-native end to end, so unlike AlloyFlux there
// is no signal-convention conversion at this edge — the engine's output *is*
// the platform's ±1.0.
void __attribute__((section(".time_critical.renderAudio")))
renderAudio(float *pOutL, float *pOutR)
{
#ifdef AUDIO_TEST_TONE
    // Bring-up aid: 1 kHz sine straight out of the driver, bypassing the
    // engine entirely. This is the bisect between "the I2S path is broken" and
    // "the engine is producing silence" — and Alloy Coil's engine is *meant* to be
    // silent at its defaults, which makes the distinction easy to get wrong.
    // Build with: make firmware ENV=alloycoil PIOFLAGS='-a "-DAUDIO_TEST_TONE"'
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

    // Parameter glide. Twelve one-poles and, on a settled patch, zero setters —
    // ControlSmoother's deadband is what makes this affordable here.
    //
    // It was not affordable without one. Pushing all twelve setters every step
    // measured 942 µs per block against a 666 µs budget on hardware: four of
    // them end in a transcendental and those cost ~90 µs each on this part.
    // With the deadband the same patch runs at 578 µs. Do not add an
    // unconditional setter to this path.
    static uint32_t sSmoothPhase = 0;
    const uint32_t  smoothFrames = gSmoothFrames;
    if(smoothFrames != 0u && ++sSmoothPhase >= smoothFrames)
    {
        sSmoothPhase = 0;
        sSmoother.Step(gEngine);
    }

    // gExciterIn is whatever the EXCITER jack last read; 0 when unpatched, in
    // which case the resonator self-excites from its own noise floor as before.
    gEngine.Process(gExciterIn, *pOutL, *pOutR);

    // The module's output edge — upstream's peak limiter, which the port
    // originally replaced with a bare SoftClip. That was ~6 dB hot and
    // waveshaping every loud sample; the limiter's 0.49 static gain keeps the
    // signal in SoftLimit's linear region until it genuinely needs catching.
    // Full reasoning and the transfer comparison are in OutputStage.h.
    //
    // Bypassable at runtime for cost measurement only — with it off the engine
    // runs past ±1.0 and AudioDriver::toWire() hard-clamps, which is the
    // crackle the output stage exists to prevent. Not a voicing option.
    if(gLimiterEnabled)
        sOutput.Process(*pOutL, *pOutR);
}

// Backs the `status` command — commands.cpp has no visibility of the driver.
bool audioRunning()
{ return sAudioState == kAudioRunning; }

// Called from wherever the goal values change for a reason other than someone
// moving a control — preset recall, factory reset, MIDI panic. See
// ControlSmoother::Snap().
void coilControlSnap()
{ sSmoother.Snap(); }

uint8_t coilSmootherPushes()
{ return sSmoother.LastPushCount(); }

void loop()
{
    if(sAudioState == kAudioFailed)
    {
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
// Core 1 — the audio path.
// ---------------------------------------------------------------------------

void setup1()
{
    // Flush-to-Zero on Core 1's FPU. The resonator, reverb and echo tails all
    // decay into denormal territory; without FTZ the block overruns its budget.
    {
        uint32_t fpscr;
        asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
        fpscr |= (1u << 24);
        asm volatile("vmsr fpscr, %0" ::"r"(fpscr));
    }

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

void __attribute__((section(".time_critical.loop1"))) loop1()
{ sAudioDriver.pump(); }
