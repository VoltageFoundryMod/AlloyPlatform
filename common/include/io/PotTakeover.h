#pragma once

#include "HardwareIO.h" // PotId, POT_COUNT

#include <math.h> // fabsf
#include <stdint.h>

// ---------------------------------------------------------------------------
// PotTakeover — resolves the conflict between a physical knob and a value that
// was set from somewhere else (Web Configurator, MIDI CC, serial console,
// preset recall).
//
// A physical pot cannot move itself.  The moment the web sets SHAPE to 0.80
// while the panel knob sits at 0.20 the two disagree, and something has to
// decide when the knob is allowed to win again.  This class owns that decision
// and nothing else: pure state plus arithmetic in normalised 0–1 space, with
// no knowledge of any particular ADC, parameter range, or transport.
//
// Modes — runtime-selectable, persisted in AlloyConfig:
//
//   SCALE  (default) Knob movement is applied proportionally toward the end of
//                    travel: the parameter moves the instant the knob moves,
//                    in the same direction, and converges exactly on the knob
//                    position at either extreme.  No jump and no dead knob —
//                    the right default for a module you perform with.
//   PICKUP           The knob is inert until it crosses the current value,
//                    then takes over.  No jump, but the knob can feel dead for
//                    up to a full turn.
//   JUMP             First movement takes over instantly.  Simplest; steps the
//                    parameter audibly.
//
// Contract with the caller:
//
//   - `physical` must already be de-noised (slew-limited / dead-banded by the
//     ADC layer).  kMoveThreshold decides *when a knob was touched*; it is not
//     a substitute for filtering a raw ADC read.
//   - The caller writes the returned value back to the parameter and passes
//     that parameter's present value back in as `current` on the next tick.
//     External writes are detected by diffing `current` against what we
//     returned last time, so every transport is covered without any of them
//     having to announce itself — MIDI CC, SysEx apply, serial console and
//     preset load all behave identically and for free.
//   - Pots start detached.  At power-on the parameters come from the auto-save
//     slot, not from wherever the knobs happen to be parked; each knob claims
//     its parameter when it is first moved.
//
// Shared knobs (SHIFT-secondary parameters): one physical pot drives two
// PotIds.  The parameter that was *not* being addressed has necessarily gone
// out of sync while the other was being turned, so the SHIFT edge must call
// detach() on both ids of the pair — see HardwarePicoIO::updatePots().
// ---------------------------------------------------------------------------

enum class PotTakeoverMode : uint8_t
{
    JUMP   = 0, ///< first movement takes over immediately
    PICKUP = 1, ///< inert until the knob crosses the current value
    SCALE  = 2, ///< proportional toward end of travel (default)
};

class PotTakeover
{
  public:
    /// Movement needed before a detached knob counts as "touched", as a
    /// fraction of full travel.  Must sit above the ADC noise floor — at 12
    /// bits with ±2 LSB of jitter that is ~0.0005, so 1.5% is comfortable.
    static constexpr float kMoveThreshold = 0.015f;

    /// How close the scaled value must come to the knob position before the
    /// knob is considered back in sync and re-attaches.
    static constexpr float kConvergeEps = 0.002f;

    /// A parameter change larger than this, that we did not write ourselves,
    /// is an external write.  Must be smaller than one MIDI CC step (1/127 ≈
    /// 0.0079) and larger than the float round-trip error of the caller's
    /// normalise/denormalise pair.
    static constexpr float kExternalEps = 0.002f;

    void            setMode(PotTakeoverMode m) { _mode = m; }
    PotTakeoverMode mode() const { return _mode; }

    /**
     * Mark one pot as out of sync with its parameter.  The knob goes inert
     * until it is moved, then re-takes the parameter per the active mode.
     * Called on SHIFT edges for shared knobs; external writes are detected
     * automatically and need no call.
     */
    void detach(PotId id)
    {
        if((uint8_t)id < (uint8_t)PotId::POT_COUNT)
            _s[(uint8_t)id].pendingDetach = true;
    }

    /**
     * Declare every knob to be the truth right now, whatever the parameters
     * currently say.  Backs the `pot sync` console command: the next tick
     * snaps all parameters to their knob positions.
     */
    void reattachAll()
    {
        for(uint8_t i = 0; i < (uint8_t)PotId::POT_COUNT; i++)
        {
            _s[i].detached      = false;
            _s[i].armed         = false;
            _s[i].pendingDetach = false;
        }
    }

    /** True while the knob is not in control of its parameter. */
    bool isDetached(PotId id) const
    {
        return ((uint8_t)id < (uint8_t)PotId::POT_COUNT)
               && _s[(uint8_t)id].detached;
    }

    /**
     * One control tick for one pot.
     *   physical — de-noised knob position, 0–1
     *   current  — the parameter's present value, normalised to 0–1
     * Returns the value the parameter should now hold.  Returning `current`
     * unchanged means "the knob has no claim on this yet".
     */
    float process(PotId id, float physical, float current)
    {
        if((uint8_t)id >= (uint8_t)PotId::POT_COUNT)
            return current;
        State &st = _s[(uint8_t)id];
        physical  = clamp01(physical);

        // First tick after boot: adopt the stored parameter value and wait for
        // the knob to be moved before letting it overwrite the preset.
        if(!st.init)
        {
            st.init = true;
            beginDetach(st, physical, current);
            st.lastWritten = current;
            return current;
        }

        // Someone other than us moved the parameter — web, MIDI, serial,
        // preset load.  The knob is now stale by definition.
        if(st.pendingDetach || fabsf(current - st.lastWritten) > kExternalEps)
            beginDetach(st, physical, current);

        if(!st.detached)
        {
            st.lastWritten = physical;
            return physical;
        }

        // Detached: nothing happens until the knob is actually turned.
        if(!st.armed)
        {
            if(fabsf(physical - st.anchorPhys) <= kMoveThreshold)
            {
                st.lastWritten = current;
                return current;
            }
            st.armed = true;
            // Re-anchor at the position where movement was detected so the
            // parameter starts from exactly where it was — no step of one
            // threshold's worth of value as the knob is picked up.
            st.anchorPhys  = physical;
            st.anchorVal   = current;
            st.pickupAbove = (physical >= current);
        }

        float v = current;
        switch(_mode)
        {
            case PotTakeoverMode::JUMP:
                st.detached = false;
                v           = physical;
                break;

            case PotTakeoverMode::PICKUP:
                // Take over on the tick the knob passes through the value.
                if((physical >= current) != st.pickupAbove
                   || fabsf(physical - current) <= kConvergeEps)
                {
                    st.detached = false;
                    v           = physical;
                }
                break;

            case PotTakeoverMode::SCALE:
            default:
                v = scaleMap(st, physical);
                // Converges at whichever end of travel the knob reaches (and
                // immediately if it was already sitting on the value).
                if(fabsf(v - physical) <= kConvergeEps)
                {
                    st.detached = false;
                    v           = physical;
                }
                break;
        }

        st.lastWritten = v;
        return v;
    }

  private:
    struct State
    {
        float lastWritten   = 0.f;   ///< value we handed back last tick
        float anchorPhys    = 0.f;   ///< knob position when it went out of sync
        float anchorVal     = 0.f;   ///< parameter value at that moment
        bool  init          = false; ///< first process() call seen
        bool  detached      = false; ///< knob is not in control
        bool  armed         = false; ///< knob has moved since detaching
        bool  pendingDetach = false; ///< detach() requested by the caller
        bool  pickupAbove   = false; ///< knob sat above the value at detach
    };

    static float clamp01(float v)
    { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    static void beginDetach(State &st, float physical, float current)
    {
        st.detached      = true;
        st.armed         = false;
        st.pendingDetach = false;
        st.anchorPhys    = physical;
        st.anchorVal     = current;
        st.pickupAbove   = (physical >= current);
        st.lastWritten   = current;
    }

    /**
     * SCALE mapping — knob travel from the anchor is spread across the value
     * range that remains on that side, so the two converge at the extremes:
     *
     *   phys:  0 ────────── anchorPhys ────────── 1
     *   value: 0 ────────── anchorVal  ────────── 1
     *
     * Turning up covers [anchorVal, 1] over [anchorPhys, 1]; turning down
     * covers [0, anchorVal] over [0, anchorPhys].
     */
    static float scaleMap(const State &st, float physical)
    {
        if(physical >= st.anchorPhys)
        {
            const float head = 1.f - st.anchorPhys;
            if(head <= 1e-6f)
                return 1.f;
            return clamp01(st.anchorVal
                           + (physical - st.anchorPhys) / head
                                 * (1.f - st.anchorVal));
        }
        const float tail = st.anchorPhys;
        if(tail <= 1e-6f)
            return 0.f;
        return clamp01(st.anchorVal
                       - (st.anchorPhys - physical) / tail * st.anchorVal);
    }

    State           _s[(uint8_t)PotId::POT_COUNT];
    PotTakeoverMode _mode = PotTakeoverMode::SCALE;
};
