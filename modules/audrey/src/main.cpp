/**
 * Audrey II — feedback resonator, Alloy platform module.
 * Hardware : Raspberry Pi Pico 2 (RP2350) + PCM5102A I2S DAC
 *
 * Engine by Synthux Academy (Nick Donaldson / Roey Tsemah), vendored from
 * audrey-ii-simple — see modules/audrey/README.md and CREDITS.md.
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
// Audrey runs at 48 kHz rather than AlloyFlux's historical 32768 Hz for a
// concrete reason — its FeedbackLPFCutoff defaults to 18 kHz, which at 32768 Hz
// would sit *above* the 16384 Hz Nyquist, where the biquad's bilinear prewarp
// diverges. The filter would be degenerate at boot. At 48 kHz it is not.
//
// This module runs the core at 192 MHz rather than the stock 150 — the engine
// needs ~690 µs per block against a 666 µs budget at 150 MHz. 192 keeps the
// I2S divider exact (192 / 3.072 MHz = 62.5); see the rationale and the table
// of safe clocks in platformio.ini's [env:audrey].
// ---------------------------------------------------------------------------
static constexpr uint32_t kAudioRate   = 48000u;
static constexpr uint32_t kControlRate = 128u;


static constexpr uint8_t kPinI2sBCK  = 16u; // GP16; WS is implicitly GP17
static constexpr uint8_t kPinI2sData = 18u;

#include "FeedbackSynthEngine.h"
#include "dsp.h" // daisysp::SoftClip
#include "audrey_config.h"
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

// External excitation — see params.h. Control-rate on this platform.
volatile float gExciterIn = 0.0f;

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
// engine exists. ~443 KiB of static storage — see modules/audrey/README.md.
static infrasonic::FeedbackSynth::Engine gEngine;

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

    // Push the goal values into the engine. Every setter is cheap and the
    // engine does its own smoothing where a parameter needs it, so this is an
    // unconditional write rather than a change-detected one — a deadband here
    // would only duplicate what the engine already does.
    gEngine.SetStringPitch(gStringPitch);
    gEngine.SetFeedbackGain(gFeedbackGain);
    gEngine.SetFeedbackDelay(gFeedbackDelay);
    gEngine.SetFeedbackLPFCutoff(gFeedbackLPF);
    gEngine.SetFeedbackHPFCutoff(gFeedbackHPF);
    gEngine.SetEchoDelaySendAmount(gEchoSend);
    gEngine.SetEchoDelayTime(gEchoTime);
    gEngine.SetEchoDelayFeedback(gEchoFeedback);
    gEngine.SetReverbMix(gReverbMix);
    gEngine.SetReverbFeedback(gReverbDecay);
    gEngine.SetOutputLevel(gOutputLevel);

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
            Serial.println(sAudioDriver.overruns());
        }
    }
#endif
}

// Audio render. Audrey is float-native end to end, so unlike AlloyFlux there
// is no signal-convention conversion at this edge — the engine's output *is*
// the platform's ±1.0.
void __attribute__((section(".time_critical.renderAudio")))
renderAudio(float *pOutL, float *pOutR)
{
#ifdef AUDIO_TEST_TONE
    // Bring-up aid: 1 kHz sine straight out of the driver, bypassing the
    // engine entirely. This is the bisect between "the I2S path is broken" and
    // "the engine is producing silence" — and Audrey's engine is *meant* to be
    // silent at its defaults, which makes the distinction easy to get wrong.
    // Build with: make firmware ENV=audrey PIOFLAGS='-a "-DAUDIO_TEST_TONE"'
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

    // gExciterIn is whatever the EXCITER jack last read; 0 when unpatched, in
    // which case the resonator self-excites from its own noise floor as before.
    gEngine.Process(gExciterIn, *pOutL, *pOutR);

#if AUDREY_OUTPUT_SOFTCLIP
    // Master soft clip.
    //
    // The engine deliberately runs hot: with feedback gain near unity it peaks
    // around 1.34 even with output level at 0.5, so ~0.1% of samples land
    // outside ±1.0. AudioDriver::toWire() hard-clamps those, and a sparse
    // scatter of hard discontinuities is exactly what crackle is — it is not a
    // dropout and not the engine's own SoftClip, which only bounds the signal
    // *inside* the resonator and echo loops.
    //
    // Upstream has the same headroom problem; on a Daisy the codec clips it
    // just as hard. Saturating here instead keeps the instrument's character
    // (it is a distorting feedback box) while guaranteeing nothing reaches the
    // DAC out of range. Build with -DAUDREY_OUTPUT_SOFTCLIP=0 to hear the raw
    // engine and compare.
    *pOutL = daisysp::SoftClip(*pOutL);
    *pOutR = daisysp::SoftClip(*pOutR);
#endif
}

// Backs the `status` command — commands.cpp has no visibility of the driver.
bool audioRunning()
{ return sAudioState == kAudioRunning; }

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
