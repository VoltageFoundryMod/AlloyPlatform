#pragma once

// Arduino-Pico / RP2350 only — uses the SDK's SIO registers directly.
#include <Arduino.h>
#include <hardware/gpio.h>
#include <hardware/structs/sio.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Apa102<N> — bitbang SPI driver for an APA102 / SK9822 Dotstar chain (M30).
//
// AlloyFlux wiring (verified against hardware/MainPCB/LEDs.kicad_sch):
//   GP6  -> LED_DATA  (D12 pin 1, DI)
//   GP7  -> LED_CLK   (D12 pin 2, CI)
//   chain order D12 -> D13 -> D14 -> D15 -> D16 -> D21 -> D22
//   VCC = 3V3, so the Pico's 3.3 V GPIO drives DI/CI directly — no level
//   shifter, and no 5 V logic-threshold margin to worry about.
//
// Why bitbang rather than the hardware SPI blocks: both SPI peripherals are
// spoken for (PCM5102 I2S is on PIO, and the remaining SPI pins do not reach
// GP6/GP7 on this layout). The chain is 7 LEDs — 296 bits — so a bitbang costs
// well under 100 us per frame at the clock rate below, once per 128 Hz control
// tick. It is called from updateControl(), never from the audio ISR.
//
// Protocol (APA102 datasheet + the SK9822 compatibility notes):
//   start frame  32 bits of 0
//   LED frame    111 + 5-bit global brightness, then BLUE, GREEN, RED bytes
//   end frame    32 bits of 0, plus ceil(N/16) further bytes
//
// The end frame is the part that is easy to get wrong. The datasheet specifies
// 32 bits of *ones*, which works on genuine APA102s but leaves SK9822 clones
// latching the previous frame — and on a long chain a run of ones can be read
// as the start of new data. Zeros followed by N/2 extra clock edges is the
// form that satisfies both parts, so that is what this sends.
// ---------------------------------------------------------------------------
template <uint8_t N_LEDS>
class Apa102
{
  public:
    /**
     * Claims the two GPIOs and blanks the chain.
     * Call once from setup(), before the first show().
     */
    void begin(uint8_t pinData, uint8_t pinClk)
    {
        _maskData = 1u << pinData;
        _maskClk  = 1u << pinClk;

        gpio_init(pinData);
        gpio_init(pinClk);
        gpio_set_dir(pinData, GPIO_OUT);
        gpio_set_dir(pinClk, GPIO_OUT);
        sio_hw->gpio_clr = _maskData | _maskClk;

        for(uint8_t i = 0; i < N_LEDS; ++i)
            _px[i][0] = _px[i][1] = _px[i][2] = 0u;
        _ready = true;
        show(); // known-dark panel at boot, whatever the LEDs powered up as
    }

    /**
     * Stages one pixel. Normalised 0.0–1.0 per channel, clamped.
     *
     * No gamma is applied: LedEngine's output is already a perceptual
     * brightness target (its palette and levels were tuned by eye), and the
     * same numbers drive Rack's light widgets on the VCV side. Curving them
     * here would make the hardware and the plugin disagree — the place to
     * adjust overall feel is LedEngine::setMasterBrightness() and the palette,
     * which both platforms share.
     *
     * @param i  chain position, 0 = the LED nearest the MCU
     */
    void setPixel(uint8_t i, float r, float g, float b)
    {
        if(i >= N_LEDS)
            return;
        _px[i][0] = _q8(r);
        _px[i][1] = _q8(g);
        _px[i][2] = _q8(b);
    }

    /**
     * APA102 global current control, 0–31, applied to every LED.
     *
     * This is a current limit rather than a PWM level: it trades away colour
     * resolution, and some SK9822 clones only sample it every few frames,
     * which shows up as flicker if it is modulated. Set it once as a panel
     * calibration and do all animation in the 8-bit channels.
     */
    void setGlobalBrightness(uint8_t b) { _global = b > 31u ? 31u : b; }

    /** Shifts the staged frame out to the chain. ~60 us for 7 LEDs. */
    void show()
    {
        if(!_ready)
            return;

        for(uint8_t i = 0; i < 4; ++i) // start frame
            _writeByte(0x00);

        for(uint8_t i = 0; i < N_LEDS; ++i)
        {
            _writeByte(0xE0u | _global);
            _writeByte(_px[i][2]); // BLUE  — APA102 orders the bytes B, G, R
            _writeByte(_px[i][1]); // GREEN
            _writeByte(_px[i][0]); // RED
        }

        // End frame: 4 zero bytes to latch, then enough further clock edges
        // (N/2, rounded up to whole bytes) to walk the last pixel to the end
        // of the chain.
        for(uint8_t i = 0; i < 4u + (N_LEDS + 15u) / 16u; ++i)
            _writeByte(0x00);
    }

  private:
    static uint8_t _q8(float v)
    {
        if(v <= 0.0f)
            return 0u;
        if(v >= 1.0f)
            return 255u;
        return (uint8_t)(v * 255.0f + 0.5f);
    }

    // Half-cycle padding. A bare SIO store pair would clock faster than the
    // 30 MHz the parts are rated for (and far faster than panel wiring likes);
    // these land it around 8-10 MHz at a 150 MHz core.
    static inline void _tick()
    {
        for(uint8_t i = 0; i < 8; ++i)
            __asm__ volatile("nop");
    }

    // MSB first, data set while the clock is low.
    void _writeByte(uint8_t v)
    {
        for(uint8_t bit = 0; bit < 8; ++bit)
        {
            if(v & 0x80u)
                sio_hw->gpio_set = _maskData;
            else
                sio_hw->gpio_clr = _maskData;
            v <<= 1;
            _tick();
            sio_hw->gpio_set = _maskClk;
            _tick();
            sio_hw->gpio_clr = _maskClk;
        }
    }

    uint8_t  _px[N_LEDS][3] = {};
    uint32_t _maskData      = 0u;
    uint32_t _maskClk       = 0u;
    uint8_t  _global        = 31u;
    bool     _ready         = false;
};
