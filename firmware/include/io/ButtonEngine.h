#pragma once

#include <Arduino.h>

/**
 * ButtonEngine — debounced digital button reader (Milestone 31)
 *
 * Active-low with internal pull-up (INPUT_PULLUP).
 * Call poll() at 128 Hz (every updateControl() tick).
 * Debounce: 4 consecutive matching reads = ~31 ms settle window.
 *
 * Edge events (valid for one poll() call only):
 *   pressed()  — falling edge  (button just went down)
 *   released() — rising edge   (button just came up)
 *   held()     — button has been down for >= holdTicks ticks
 *                holdTicks default = 64 ticks = ~500 ms @ 128 Hz
 */
class ButtonEngine
{
  public:
    explicit ButtonEngine(uint8_t pin, uint8_t holdTicks = 64)
    : _pin(pin),
      _holdTicks(holdTicks),
      _state(true),
      _debounceState(true),
      _debounceCount(0),
      _pressed(false),
      _released(false),
      _held(false),
      _downTicks(0)
    {
    }

    void begin() { pinMode(_pin, INPUT_PULLUP); }

    /** Call once per updateControl() tick (~128 Hz). */
    void poll()
    {
        _pressed  = false;
        _released = false;
        _held     = false;

        const bool raw
            = (bool)digitalRead(_pin); // true = released (active-low)

        // Debounce: require kDebounce consecutive matching reads before accepting
        if(raw == _debounceState)
        {
            _debounceCount = 0;
        }
        else
        {
            _debounceCount++;
            if(_debounceCount >= kDebounce)
            {
                _debounceState = raw;
                _debounceCount = 0;

                const bool wasDown = !_state; // previous stable state
                _state             = raw;
                const bool isDown  = !_state; // new stable state

                if(isDown && !wasDown)
                {
                    _pressed   = true;
                    _downTicks = 0;
                }
                else if(!isDown && wasDown)
                {
                    _released  = true;
                    _downTicks = 0;
                }
            }
        }

        // Count ticks while held down; fire held event once at threshold
        if(!_state)
        { // button is down (active-low)
            if(_downTicks < 0xFFFF)
                _downTicks++;
            if(_downTicks == _holdTicks)
            {
                _held = true;
            }
        }
    }

    /** True for the one poll() call on button press (falling edge). */
    bool pressed() const { return _pressed; }
    /** True for the one poll() call on button release (rising edge). */
    bool released() const { return _released; }
    /** True for the one poll() call when held duration is reached. */
    bool held() const { return _held; }
    /** True while button is physically down (debounced). */
    bool isDown() const { return !_state; }

  private:
    static constexpr uint8_t kDebounce = 4; // ticks to accept stable transition

    uint8_t  _pin;
    uint8_t  _holdTicks;
    bool     _state; // current stable state (true = not pressed, active-low)
    bool     _debounceState; // candidate new state accumulating
    uint8_t  _debounceCount;
    bool     _pressed;
    bool     _released;
    bool     _held;
    uint16_t _downTicks;
};
