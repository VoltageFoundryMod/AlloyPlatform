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
#include "Osc16.h"
#include <Mozzi.h>
#include <math.h>
#include <tables/sin2048_int8.h>

// ---------------------------------------------------------------------------
// Band-limited rising saw — generated in setup() via additive synthesis.
// Lanczos sigma factor on each harmonic eliminates Gibbs overshoot (~9%→<1%).
// maxH = floor(AUDIO_RATE/2 / 440) ≈ 37: band-limited for 440 Hz and above.
// ---------------------------------------------------------------------------
#define SAW_TABLE_CELLS 2048
static int8_t gSawTable[SAW_TABLE_CELLS];

static void generateBandLimitedSaw() {
    const int N = SAW_TABLE_CELLS;
    const int maxH = (MOZZI_AUDIO_RATE / 2) / 440; // 37 @ 32768 Hz
    static float buf[SAW_TABLE_CELLS];             // static: avoids 8 KB stack frame
    float peak = 0.0f;
    for (int i = 0; i < N; i++) {
        const float phase = 2.0f * (float)M_PI * i / N;
        float val = 0.0f;
        for (int h = 1; h <= maxH; h++) {
            // Lanczos sigma: sinc(h*pi/(maxH+1)) dampens upper harmonics
            const float x = (float)h * (float)M_PI / (maxH + 1);
            const float sigma = sinf(x) / x;
            val += sigma * sinf((float)h * phase) / (float)h;
        }
        buf[i] = val;
        if (fabsf(val) > peak)
            peak = fabsf(val);
    }
    const float scale = 127.0f / peak;
    for (int i = 0; i < N; i++)
        gSawTable[i] = (int8_t)(buf[i] * scale);
}

// ---------------------------------------------------------------------------
// Module includes
// ---------------------------------------------------------------------------
#include "debug.h"
#include "params.h"
#include "serial_console.h"

// ---------------------------------------------------------------------------
// Oscillators: Voice 1 (L) + Voice 2 (R), each a sine/saw pair
// ---------------------------------------------------------------------------
static Osc16<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> v1_sin(SIN2048_DATA);
static Osc16<SAW_TABLE_CELLS, MOZZI_AUDIO_RATE> v1_saw(gSawTable);
static Osc16<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> v2_sin(SIN2048_DATA);
static Osc16<SAW_TABLE_CELLS, MOZZI_AUDIO_RATE> v2_saw(gSawTable);

// ---------------------------------------------------------------------------
// Shared synthesis parameters (declared extern in params.h)
// ---------------------------------------------------------------------------
float gBaseFreq = 440.0f;
float gDetune = 0.0f;
float gWaveform = 0.0f;
float gVolume = 0.8f;

// Smoothed values — consumed by updateAudio(), updated in updateControl()
static float sWaveform = 0.0f;
static float sVolume = 0.8f;

// ---------------------------------------------------------------------------
// Mozzi callbacks
// ---------------------------------------------------------------------------

void setup() {
    serialConsole_init();
    generateBandLimitedSaw();
    startMozzi();
    v1_sin.setFreq(gBaseFreq);
    v1_saw.setFreq(gBaseFreq);
    v2_sin.setFreq(gBaseFreq);
    v2_saw.setFreq(gBaseFreq);
    serialConsole_ready();
}

void updateControl() {
    serialConsole_update();

    float freq1 = max(gBaseFreq - gDetune * 0.5f, 20.0f);
    float freq2 = max(gBaseFreq + gDetune * 0.5f, 20.0f);

    v1_sin.setFreq(freq1);
    v1_saw.setFreq(freq1);
    v2_sin.setFreq(freq2);
    v2_saw.setFreq(freq2);

    // One-pole smoothing — eliminates zipper noise on parameter changes
    sWaveform += (gWaveform - sWaveform) * 0.1f;
    sVolume += (gVolume - sVolume) * 0.1f;
}

AudioOutput updateAudio() {
    // Blend sine and saw with full 16-bit precision throughout.
    // Osc16::next() returns ≈±32512 (int16 range from interpolated int8 table).
    // sinW + sawW == 256, so >>8 after the weighted sum normalises back to ±32512.
    int32_t sawW = (int32_t)(sWaveform * 256.0f);
    int32_t sinW = 256 - sawW;

    int32_t v1 = ((int32_t)v1_sin.next() * sinW + (int32_t)v1_saw.next() * sawW) >> 8;
    int32_t v2 = ((int32_t)v2_sin.next() * sinW + (int32_t)v2_saw.next() * sawW) >> 8;

    // Volume: scale 0..256, >>8 keeps result in ±32512
    int32_t volW = (int32_t)(sVolume * 256.0f);
    int32_t left = (v1 * volW) >> 8;
    int32_t right = (v2 * volW) >> 8;

    return StereoOutput::from16Bit(left, right);
}

void loop() {
    audioHook();
}
