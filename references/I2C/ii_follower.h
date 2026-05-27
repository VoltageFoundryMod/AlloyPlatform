#pragma once

#include <climits>
#include <cstdint>

#ifndef UNIT_TEST
#include "stm32h7xx_hal.h"
#endif

namespace fourseas
{

// Default I2C address (0x58, avoids collision with Orca at 0x48)
static constexpr uint8_t kIIDefaultAddress = 0x58;

// Maximum receive buffer size (1 cmd + 6 param bytes)
static constexpr uint8_t kIIMaxMessageSize = 8;

// Query flag: command | 0x80
static constexpr uint8_t kIIQueryFlag = 0x80;

// Sentinel values indicating "use hardware"
static constexpr int16_t kI2CUnset16 = INT16_MIN;
static constexpr int8_t  kI2CUnset8  = -1;

// Command IDs for the ii protocol
enum IICommand : uint8_t
{
    // Global parameters (cmd + 16-bit value MSB-first)
    II_X           = 0x00,
    II_Y           = 0x01,
    II_Z           = 0x02,
    II_X_SPREAD    = 0x03,
    II_Y_SPREAD    = 0x04,
    II_Z_SPREAD    = 0x05,
    II_TUNE        = 0x06,
    II_MOD_DEPTH_1 = 0x09,
    II_MOD_DEPTH_2 = 0x0A,
    II_BANK        = 0x0B,
    II_SYNC_1      = 0x0C,
    II_SYNC_2      = 0x0D,
    II_MOD_1       = 0x0E,
    II_MOD_2       = 0x0F,
    II_LFO_1       = 0x10,
    II_LFO_2       = 0x11,
    II_INTERP      = 0x12,
    II_SPREAD_TYPE = 0x13,
    II_FREQ_SPREAD = 0x14,

    // Per-oscillator (cmd + 8-bit osc index + 16-bit value)
    II_OSC_NOTE = 0x20,
    II_OSC_X    = 0x21,
    II_OSC_Y    = 0x22,
    II_OSC_Z    = 0x23,

    // Control
    II_RELEASE = 0x30,
};

// Per-oscillator override state
struct I2COscState
{
    volatile int16_t note; // MIDI note * 128, or kI2CUnset16
    volatile int16_t x;    // 14-bit position, or kI2CUnset16
    volatile int16_t y;    // 14-bit position, or kI2CUnset16
    volatile int16_t z;    // 14-bit position, or kI2CUnset16

    void Reset()
    {
        note = kI2CUnset16;
        x    = kI2CUnset16;
        y    = kI2CUnset16;
        z    = kI2CUnset16;
    }
};

// Shared state between ISR and main thread
struct I2CParamState
{
    // Set when any I2C command has been received
    volatile bool active;

    // Continuous overrides: s16V signed int16, summed with knob/CV.
    // V 5 (8192) = full positive; values past saturate downstream.
    // kI2CUnset16 = no override (treated as 0 for queries).
    volatile int16_t x;           // V 5 -> +7.0 position offset
    volatile int16_t y;           // V 5 -> +7.0
    volatile int16_t z;           // V 5 -> +7.0
    volatile int16_t x_spread;    // V 5 -> +1.0
    volatile int16_t y_spread;    // V 5 -> +1.0
    volatile int16_t z_spread;    // V 5 -> +1.0
    volatile int16_t tune;        // semitones * 128
    volatile int16_t mod_depth_1; // V 5 -> +1.0 (clamps to 0..1 final)
    volatile int16_t mod_depth_2; // V 5 -> +1.0 (clamps to 0..1 final)
    volatile int16_t freq_spread; // V 5 -> +1.0

    // Mode/toggle command inbox: written by ISR on receive,
    // consumed-and-cleared by main loop on apply. kI2CUnset8 = no
    // pending write. Button presses cycle from the *_live mirror.
    volatile int8_t bank;
    volatile int8_t sync_1;
    volatile int8_t sync_2;
    volatile int8_t mod_1;
    volatile int8_t mod_2;
    volatile int8_t lfo_1;
    volatile int8_t lfo_2;
    volatile int8_t interp;
    volatile int8_t spread_type;

    // Live mirror: written by main loop each tick, read by ISR for
    // queries. Single-byte writes are atomic on M7, so queries always
    // see a coherent value. Reflects the *current* mode/bank
    // regardless of whether it was last set by i2c or button/pot.
    volatile int8_t bank_live;
    volatile int8_t sync_1_live;
    volatile int8_t sync_2_live;
    volatile int8_t mod_1_live;
    volatile int8_t mod_2_live;
    volatile int8_t lfo_1_live;
    volatile int8_t lfo_2_live;
    volatile int8_t interp_live;
    volatile int8_t spread_type_live;

    // Per-oscillator overrides
    I2COscState osc[4];

    void Reset()
    {
        active      = false;
        x           = kI2CUnset16;
        y           = kI2CUnset16;
        z           = kI2CUnset16;
        x_spread    = kI2CUnset16;
        y_spread    = kI2CUnset16;
        z_spread    = kI2CUnset16;
        tune        = kI2CUnset16;
        mod_depth_1 = kI2CUnset16;
        mod_depth_2 = kI2CUnset16;
        freq_spread = kI2CUnset16;
        bank        = kI2CUnset8;
        sync_1      = kI2CUnset8;
        sync_2      = kI2CUnset8;
        mod_1       = kI2CUnset8;
        mod_2       = kI2CUnset8;
        lfo_1       = kI2CUnset8;
        lfo_2       = kI2CUnset8;
        interp      = kI2CUnset8;
        spread_type = kI2CUnset8;

        // Zero the live mirror so queries before the first UpdateParams
        // tick don't return garbage. The main loop will overwrite these
        // with the real state within ~0.5ms. After FS.REL, this means a
        // query in the brief window before the next tick reads "0"
        // instead of the actual mode — self-corrects on next tick.
        bank_live        = 0;
        sync_1_live      = 0;
        sync_2_live      = 0;
        mod_1_live       = 0;
        mod_2_live       = 0;
        lfo_1_live       = 0;
        lfo_2_live       = 0;
        interp_live      = 0;
        spread_type_live = 0;

        for(auto &o : osc)
        {
            o.Reset();
        }
    }
};

class IIFollower
{
  public:
    IIFollower() {}
    ~IIFollower() {}

    void Init(uint8_t address = kIIDefaultAddress);
    void StartListening();

    // Call from main loop to recover from I2C errors
    void Poll();

    I2CParamState &GetParamState() { return state_; }

    // Register-level IRQ handlers (public for extern "C" access)
    void HandleEventIRQ();
    void HandleErrorIRQ();

  private:
    void ProcessCommand();
    void PrepareQueryResponse(uint8_t cmd);
    void EnableListen();

    // Read a 16-bit value MSB-first from rx buffer at offset
    int16_t ReadInt16(uint8_t offset) const
    {
        return static_cast<int16_t>(
            (static_cast<uint16_t>(rx_buf_[offset]) << 8)
            | static_cast<uint16_t>(rx_buf_[offset + 1]));
    }

    I2CParamState state_;

    uint8_t rx_buf_[kIIMaxMessageSize];
    uint8_t tx_buf_[2];
    uint8_t rx_idx_;    // current write position in rx_buf_
    uint8_t rx_size_;   // total bytes received in completed transaction
    uint8_t tx_idx_;    // current read position in tx_buf_
    uint8_t tx_size_;   // total bytes to transmit
    bool    listening_; // true when address match interrupt is armed
};

} // namespace fourseas
