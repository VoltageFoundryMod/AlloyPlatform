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
 * ⚠ Panel I/O is not wired yet. There is no ADC mux, no LEDs and no buttons on
 * this build: every parameter arrives over USB MIDI, SysEx or the serial
 * console. That is deliberate for bring-up — it is the shortest path to
 * hearing the engine on real hardware and driving it from the Web
 * Configurator, and the I/O layer can land once there is a panel to drive it.
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
#include "io/serial_console.h"
#include "io/usb_midi.h"
#include "params.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Parameter globals — one per params.json row. Written by MIDI/SysEx/serial,
// read once per control tick and pushed into the engine.
// ---------------------------------------------------------------------------
float gStringPitch   = 40.0f;
float gFeedbackGain  = -30.0f;
float gFeedbackDelay = 0.001f;
float gFeedbackLPF   = 18000.0f;
float gFeedbackHPF   = 250.0f;
float gEchoSend      = 0.0f;
float gEchoTime      = 0.5f;
float gEchoFeedback  = 0.0f;
float gReverbMix     = 0.0f;
float gReverbDecay   = 0.2f;
float gOutputLevel   = 0.5f;
float gExciterLevel  = 1.0f;

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

    // Nothing here talks to the engine any more. This tick's job is to bring
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
            const float budget   = (float)gAudioBudgetUs;
            const float headroom = (budget - (float)gAudioElapsedUs) / budget
                                   * 100.0f;
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
    static uint32_t     sSmoothPhase = 0;
    const uint32_t      smoothFrames = gSmoothFrames;
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
