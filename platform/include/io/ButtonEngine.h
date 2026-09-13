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
 *   pressed()   — falling edge  (button just went down)
 *   released()  — rising edge   (button just came up)
 *   held()      — button has been down for >= holdTicks ticks
 *                 holdTicks default = 64 ticks = ~500 ms @ 128 Hz
 *   heldLong()  — button has been down for >= longHoldMs **milliseconds**
 *                 default = 3000 ms
 *
 * Two thresholds rather than one (M78b): a gesture that has to be deliberate —
 * opening the BLE pairing window — needs a hold long enough that nobody
 * reaches it by accident, while the existing combos want the short one. Both
 * fire exactly once per press, so a long hold emits held() on the way past and
 * then heldLong(); a handler that only wants the long one ignores held().
 *
 * ⚠ The long one is in milliseconds and the short one in ticks, and that is
 * deliberate rather than an oversight. `loop()` paces updateControl() from the
 * audio driver's tick counter and **coalesces missed ticks** — it assigns
 * `sLastTick = tick` rather than stepping it — so whenever core 0 is busy, one
 * poll() covers several elapsed ticks. A tick count is therefore a lower bound
 * on elapsed time, not a measure of it.
 *
 * At debounce scale that drift is invisible. At three seconds it is not: the
 * BLE hold measured in ticks needed *over* three seconds of real holding
 * whenever USB MIDI or BLE had kept core 0 busy, so the gesture would
 * intermittently not fire and the release would cycle the voice mode instead —
 * which from the panel reads as a broken button. Wall-clock makes the gesture
 * take the time it says it does, whatever the tick rate is doing.
 */
class ButtonEngine
{
  public:
    explicit ButtonEngine(uint8_t  pin,
                          uint8_t  holdTicks  = 64,
                          uint32_t longHoldMs = 3000)
    : _pin(pin),
      _holdTicks(holdTicks),
      _longHoldMs(longHoldMs),
      _state(true),
      _debounceState(true),
      _debounceCount(0),
      _pressed(false),
      _released(false),
      _held(false),
      _heldLong(false),
      _downTicks(0),
      _downAtMs(0),
      _longFired(false)
    {
    }

    void begin() { pinMode(_pin, INPUT_PULLUP); }

    /** Call once per updateControl() tick (~128 Hz). */
    void poll()
    {
        _pressed  = false;
        _released = false;
        _held     = false;
        _heldLong = false;

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
                    _downAtMs  = millis();
                    _longFired = false;
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
            // Wall-clock, and latched, so it fires exactly once per press
            // however irregularly poll() happens to be called.
            if(!_longFired && (uint32_t)(millis() - _downAtMs) >= _longHoldMs)
            {
                _longFired = true;
                _heldLong  = true;
            }
        }
    }

    /** True for the one poll() call on button press (falling edge). */
    bool pressed() const { return _pressed; }
    /** True for the one poll() call on button release (rising edge). */
    bool released() const { return _released; }
    /** True for the one poll() call when held duration is reached. */
    bool held() const { return _held; }
    /** True for the one poll() call when the long-hold duration is reached. */
    bool heldLong() const { return _heldLong; }
    /** True while button is physically down (debounced). */
    bool isDown() const { return !_state; }

  private:
    static constexpr uint8_t kDebounce = 4; // ticks to accept stable transition

    uint8_t  _pin;
    uint8_t  _holdTicks;
    uint32_t _longHoldMs;
    bool     _state; // current stable state (true = not pressed, active-low)
    bool     _debounceState; // candidate new state accumulating
    uint8_t  _debounceCount;
    bool     _pressed;
    bool     _released;
    bool     _held;
    bool     _heldLong;
    uint16_t _downTicks;
    uint32_t _downAtMs;  // millis() at the debounced press
    bool     _longFired; // heldLong() already delivered for this press
};
