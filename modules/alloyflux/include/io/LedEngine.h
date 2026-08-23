#pragma once

#include "io/HardwareIO.h"
#include "SynthEngine.h" // SynthParams
#include "VoiceMode.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// LedEngine — the LED language (M30 / M37k), implemented once for both targets.
//
// This is the single place where module state is turned into colour.  It is
// platform-independent: no Arduino, no Rack, no libm.  The result is a small
// array of normalised RGB values that each platform pushes out through
// IHardwareIO::writeLight() — APA102/SK9822 Dotstars on hardware, Rack light
// widgets in VCV.
//
// Called by:
//   firmware/src/main.cpp           at control rate (128 Hz) in updateControl()
//   vcv-plugin/src/AlloyFlux.cpp    on the same 128 Hz control tick
//
// Usage:
//     LedEngine leds;
//     leds.init();                         // once, at startup
//     ...
//     LedSignals sig;                      // per control tick
//     sig.envLevel = engine.curveEng->level();
//     sig.peakL    = peakSinceLastTick;    // 0–1 normalised output peak
//     leds.update(params, sig, dt);
//     leds.writeTo(io);
//
// Animations (mode ripple, confirmation flashes, calibration) are driven by
// notify*() calls and advance on their own using the dt passed to update().
//
// Spec: references/AlloyFlux-hardware-design.md § "LED Language".
// ---------------------------------------------------------------------------

/** Normalised linear RGB, 0.0–1.0 per channel.  Plain aggregate (C++11). */
struct LedColor
{
    float r;
    float g;
    float b;
};

// ---------------------------------------------------------------------------
// Colour language (module-reference § "Colour Language").
//
// Values are hand-tuned so that each hue stays distinguishable at low
// brightness on both a diffused Dotstar and a Rack light widget.
// ---------------------------------------------------------------------------
namespace LedPalette
{
constexpr LedColor kOff       = {0.00f, 0.00f, 0.00f};
constexpr LedColor kWarmRed   = {1.00f, 0.16f, 0.05f}; // ROOT / left channel
constexpr LedColor kCoolBlue  = {0.10f, 0.35f, 1.00f}; // RELATION / right
constexpr LedColor kGreen     = {0.10f, 1.00f, 0.20f}; // motion / drift
constexpr LedColor kPurple    = {0.55f, 0.10f, 1.00f}; // STRING / chorus
constexpr LedColor kCyan      = {0.00f, 0.85f, 1.00f}; // CLOUD / ensemble
constexpr LedColor kAmber     = {1.00f, 0.50f, 0.04f}; // CHORD / harmonic stack
constexpr LedColor kMagenta   = {1.00f, 0.05f, 0.70f}; // CASCADE / FM
constexpr LedColor kLime      = {0.60f, 1.00f, 0.08f}; // POLY
constexpr LedColor kSoftWhite = {0.85f, 0.85f, 0.78f}; // PAIR / neutral
constexpr LedColor kWarmWhite = {1.00f, 0.72f, 0.42f}; // drone breathe
constexpr LedColor kWhite     = {1.00f, 1.00f, 1.00f}; // confirmation flash
} // namespace LedPalette

/**
 * Panel LED identifiers, in the physical layout of the module.
 *
 *      VOICE_L ·  ·  · VOICE_R      top:        voice activity
 *     MOD_L  ·  ·  ·  · MOD_R       mid-top:    motion / modulation
 *        MODE  ·   SHIFT            mid-bottom: mode / shift-drone
 *           · CENTRE ·              bottom:     heartbeat / global
 *
 * Hardware designators (schematic) and VCV light order:
 *   VOICE_L = D12 / LED1     VOICE_R = D22 / LED7
 *   MOD_L   = D13 / LED2     MOD_R   = D21 / LED6
 *   MODE    = D14 / LED3     SHIFT   = D16 / LED5
 *   CENTRE  = D15 / LED4
 */
enum class LedId : uint8_t
{
    VOICE_L = 0, ///< D12 — ROOT voice activity / left channel energy
    VOICE_R,     ///< D22 — RELATION voice activity / right channel energy
    MOD_L,       ///< D13 — motion and modulation depth
    MOD_R,       ///< D21 — secondary modulation / stereo position
    MODE,        ///< D14 — current voice mode (near MODE_SW)
    SHIFT,       ///< D16 — shift state / drone (near SHIFT_SW)
    CENTRE,      ///< D15 — heartbeat / global event confirmation
    LED_COUNT
};

/**
 * The seven LEDs in **silkscreen order** — LED1..LED7, which is the physical
 * daisy chain D12 → D13 → D14 → D15 → D16 → D21 → D22, running
 * counter-clockwise from the upper left.
 *
 * `LedId` is ordered by *role*, which pairs left with right and therefore
 * jumps across the panel. Any animation that has to read as a sequence — the
 * mode count in `_applyModeCount()` — walks this table instead, so the eye
 * follows one continuous path and the lit LEDs can actually be counted.
 */
static constexpr LedId kLedPanelOrder[int(LedId::LED_COUNT)] = {
    LedId::VOICE_L, // LED1 / D12 — upper left
    LedId::MOD_L,   // LED2 / D13 — left
    LedId::MODE,    // LED3 / D14 — lower left
    LedId::CENTRE,  // LED4 / D15 — bottom centre
    LedId::SHIFT,   // LED5 / D16 — lower right
    LedId::MOD_R,   // LED6 / D21 — right
    LedId::VOICE_R, // LED7 / D22 — upper right
};

/** V/Oct calibration routine phases — drives the D14/D15/D16 visual. */
enum class LedCalPhase : uint8_t
{
    OFF = 0, ///< not calibrating
    WAIT_1V, ///< awaiting the 1 V reference — D14 white, D15 slow pulse
    WAIT_3V, ///< 1 V accepted — D12 full, awaiting 3 V
    DONE     ///< both points accepted — ripple + three confirm flashes
};

/**
 * Per-tick runtime state that cannot be derived from SynthParams.
 *
 * peakL/peakR are the peak absolute output level of each channel *since the
 * last update()* (normalised 0–1).  The caller accumulates the peak in its
 * audio path; the attack/release smoothing that turns it into a usable
 * brightness lives here so both platforms behave identically.
 */
struct LedSignals
{
    float   envLevel     = 0.0f;  ///< main envelope level 0–1
    float   peakL        = 0.0f;  ///< left output peak since last tick 0–1
    float   peakR        = 0.0f;  ///< right output peak since last tick 0–1
    uint8_t activeVoices = 0;     ///< POLY voices currently sounding (0–6)
    bool    droneMode    = false; ///< free-running, no gate
    bool    shiftHeld    = false; ///< SHIFT_SW currently held
    bool    gateHigh     = false; ///< gate / MIDI note currently active
};

// ---------------------------------------------------------------------------
// Small float helpers — no libm, so this header is safe on every target.
// ---------------------------------------------------------------------------
static inline float ledClamp01(float v)
{ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static inline LedColor ledScale(const LedColor &c, float k)
{ return LedColor{c.r * k, c.g * k, c.b * k}; }

static inline LedColor ledLerp(const LedColor &a, const LedColor &b, float t)
{
    return LedColor{
        a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

/**
 * Unipolar S-curve oscillator: 0–1 phase in, 0–1 raised-cosine-ish out.
 * A smoothstepped triangle — visually indistinguishable from a sine at LED
 * resolution and costs three multiplies instead of a sinf() call.
 */
static inline float ledWave01(float phase)
{
    phase -= (float)(int)phase; // wrap into 0–1
    if(phase < 0.0f)
        phase += 1.0f;
    float t = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    return t * t * (3.0f - 2.0f * t);
}

/** Colour of a voice mode — the one thing the user learns once (D14). */
inline LedColor ledModeColor(VoiceMode m)
{
    switch(m)
    {
        case VoiceMode::PAIR: return LedPalette::kSoftWhite;
        case VoiceMode::CLOUD: return LedPalette::kCyan;
        case VoiceMode::CHORD: return LedPalette::kAmber;
        case VoiceMode::CASCADE: return LedPalette::kMagenta;
        case VoiceMode::STRING: return LedPalette::kPurple;
        case VoiceMode::POLY: return LedPalette::kLime;
        default: return LedPalette::kSoftWhite;
    }
}

// ---------------------------------------------------------------------------
class LedEngine
{
  public:
    LedEngine() { init(); }

    /** Clears all state and turns every LED off. */
    void init()
    {
        for(int i = 0; i < int(LedId::LED_COUNT); ++i)
            _led[i] = LedPalette::kOff;
        _lvlL = _lvlR = _act = 0.0f;
        _breathePhase = _pulsePhase = 0.0f;
        _rippleT                    = -1.0f;
        _noteFlash                  = 0.0f;
        _confirmT                   = -1.0f;
        _modeFlash                  = 0.0f;
        _countT                     = -1.0f;
        _countN                     = 0;
        _countColor                 = LedPalette::kOff;
        _cal                        = LedCalPhase::OFF;
    }

    /** Global dimmer applied to every LED — 0.0 (dark) to 1.0 (full). */
    void setMasterBrightness(float b) { _master = ledClamp01(b); }

    /**
     * MODE tap — centre ignites and ripples out, then the panel *counts* the
     * new mode: LED1..LEDn fill in along the silkscreen chain, n being the
     * mode's position in the cycle (PAIR = 1 … POLY = 6), and hold together
     * before fading back to the normal display.
     *
     * The ripple alone says "something changed"; the count says *which*, and
     * says it without the user having to remember what cyan means. The colour
     * is still the mode's own, so the two readings reinforce each other.
     *
     * @param m the mode just switched to — its ordinal is the count.
     */
    void notifyModeChanged(VoiceMode m)
    {
        _rippleT  = 0.0f;
        _countT   = 0.0f;
        uint8_t n = (uint8_t)((uint8_t)m + 1u);
        if(n > (uint8_t)LedId::LED_COUNT)
            n = (uint8_t)LedId::LED_COUNT;
        _countN     = n;
        _countColor = ledModeColor(m);
    }

    /** Gate / MIDI note-on — bright attack flash on the centre LED. */
    void notifyNoteOn() { _noteFlash = 1.0f; }

    /** Global confirmation (preset save, calibration) — 3 quick flashes. */
    void notifyConfirm() { _confirmT = 0.0f; }

    /**
     * Drives the calibration visual.  Advancing the phase flashes D14 to
     * acknowledge the accepted reference point; DONE plays the ripple plus the
     * three-flash "saved" confirmation and then returns to OFF on its own.
     */
    void setCalibration(LedCalPhase phase)
    {
        if(phase == _cal)
            return;
        if(phase == LedCalPhase::WAIT_3V || phase == LedCalPhase::DONE)
            _modeFlash = 1.0f; // D14 acknowledges the accepted point
        if(phase == LedCalPhase::DONE)
        {
            _rippleT  = 0.0f;
            _confirmT = 0.0f;
        }
        _cal = phase;
    }

    /** True while a calibration routine is being displayed. */
    bool calibrating() const { return _cal != LedCalPhase::OFF; }

    /**
     * Recompute every LED colour.  Call once per control tick.
     * @param p   the same SynthParams snapshot handed to SynthEngine::control()
     * @param s   runtime signal state (envelope, output peaks, drone/shift)
     * @param dt  seconds since the previous update (1/128 on hardware)
     */
    void update(const SynthParams &p, const LedSignals &s, float dt)
    {
        _advance(p, s, dt);

        if(_cal != LedCalPhase::OFF)
            _renderCalibration();
        else
        {
            _renderVoiceActivity(p, s);
            _renderModulation(p);
            _renderMode(p);
            _renderShift(s);
            _renderCentre(p, s);
        }

        _applyRipple();
        _applyModeCount();
        _applyMaster();
    }

    /** Current colour of one LED (post brightness/animation). */
    const LedColor &color(LedId id) const { return _led[int(id)]; }

    /**
     * Pushes all seven colours out through the platform's IHardwareIO, then
     * latches them as one frame (a no-op on platforms that do not need it).
     */
    void writeTo(IHardwareIO &io) const
    {
        for(int i = 0; i < int(LedId::LED_COUNT); ++i)
            io.writeLight((LightId)i, _led[i].r, _led[i].g, _led[i].b);
        io.showLights();
    }

  private:
    // -----------------------------------------------------------------------
    // Animation timing constants (seconds / Hz)
    // -----------------------------------------------------------------------
    static constexpr float kRippleDur  = 0.30f; ///< mode-change ripple time
    static constexpr float kConfirmDur = 0.60f; ///< three quick flashes

    // Mode count — starts as the ripple ends, so the two read as one gesture.
    static constexpr float kCountStep   = 0.08f; ///< interval between LEDs
    static constexpr float kCountHold   = 0.24f; ///< all n lit and steady
    static constexpr float kCountFade   = 0.20f; ///< dissolve back to normal
    static constexpr float kBreatheHz   = 0.14f; ///< drone / idle breathe
    static constexpr float kPeakAtkTau  = 0.004f;
    static constexpr float kPeakRelTau  = 0.22f;
    static constexpr float kFlashRelTau = 0.16f;

    // -----------------------------------------------------------------------
    // Timers, level followers, derived activity
    // -----------------------------------------------------------------------
    void _advance(const SynthParams &p, const LedSignals &s, float dt)
    {
        if(dt <= 0.0f)
            dt = 1.0f / 128.0f;

        // Output peak followers: fast attack, slow musical release.
        _lvlL = _follow(_lvlL, ledClamp01(s.peakL), dt);
        _lvlR = _follow(_lvlR, ledClamp01(s.peakR), dt);

        // Overall activity — what the "voice" LEDs breathe with.  In drone the
        // envelope is bypassed, so a slow breathe stands in for it.
        _breathePhase += kBreatheHz * dt;
        _breathePhase -= (float)(int)_breathePhase;
        float breathe = ledWave01(_breathePhase);

        if(s.droneMode && !s.gateHigh)
            _act = 0.55f + 0.30f * breathe;
        else
            _act = ledClamp01(s.envLevel);
        // Blend in real output energy so the LEDs track what is audible, not
        // just what the envelope generator thinks.
        float energy = _lvlL > _lvlR ? _lvlL : _lvlR;
        _act         = _act * (0.55f + 0.45f * energy);

        // MOTION pulse — rate follows the drift target rate, scaled by
        // DRIFTSPEED so the visual matches how fast the sound actually moves.
        float rate = (0.35f + p.motion * 2.60f) * (0.55f + p.driftSpeed * 9.0f);
        _pulsePhase += rate * dt;
        _pulsePhase -= (float)(int)_pulsePhase;

        // One-shot decays.
        _noteFlash -= _noteFlash * (dt / (kFlashRelTau + dt));
        _modeFlash -= _modeFlash * (dt / (kFlashRelTau + dt));
        if(_noteFlash < 0.002f)
            _noteFlash = 0.0f;
        if(_modeFlash < 0.002f)
            _modeFlash = 0.0f;

        if(_rippleT >= 0.0f)
        {
            _rippleT += dt;
            if(_rippleT > kRippleDur)
                _rippleT = -1.0f;
        }
        if(_countT >= 0.0f)
        {
            _countT += dt;
            if(_countT > _countDur())
                _countT = -1.0f;
        }
        if(_confirmT >= 0.0f)
        {
            _confirmT += dt;
            if(_confirmT > kConfirmDur)
            {
                _confirmT = -1.0f;
                // "Saved" confirmation over — leave the calibration display.
                if(_cal == LedCalPhase::DONE)
                    _cal = LedCalPhase::OFF;
            }
        }
    }

    static float _follow(float cur, float target, float dt)
    {
        float tau = target > cur ? kPeakAtkTau : kPeakRelTau;
        return cur + (target - cur) * (dt / (tau + dt));
    }

    // -----------------------------------------------------------------------
    // D12 / D22 — voice activity.  Always readable without mode awareness:
    // both LEDs breathe with the audio envelope in every mode; only the hue
    // and the meaning of the right-hand LED change per mode.
    // -----------------------------------------------------------------------
    void _renderVoiceActivity(const SynthParams &p, const LedSignals &s)
    {
        const float rel   = ledClamp01(p.relation / 24.0f);
        const float color = ledClamp01(p.color);

        // Stereo energy gradient — the secondary role of both LEDs.
        const float gL = 0.55f + 0.45f * _lvlL;
        const float gR = 0.55f + 0.45f * _lvlR;

        LedColor cl = LedPalette::kWarmRed, cr = LedPalette::kCoolBlue;
        float    bl = _act, br = _act;

        switch(p.voiceMode)
        {
            case VoiceMode::PAIR:
                // Left: envelope + gate.  Right: same envelope, scaled by how
                // far RELATION has been detuned away from the root.
                br *= 0.35f + 0.65f * rel;
                break;

            case VoiceMode::CLOUD:
                // Supersaw: left is the centre voice, steady. Right carries
                // the detune spread, so how wide the stack is set is legible
                // without touching the knob to find out.
                cl = LedPalette::kCyan;
                cr = ledLerp(LedPalette::kCyan, LedPalette::kCoolBlue, rel);
                br *= 0.35f + 0.65f * rel;
                break;

            case VoiceMode::CHORD:
                // Root note envelope; right shows the interval spread amount.
                cl = cr = LedPalette::kAmber;
                br *= 0.30f + 0.55f * rel;
                break;

            case VoiceMode::CASCADE:
                // Carrier left, modulator right — right brightens with depth.
                cl = cr = LedPalette::kMagenta;
                br *= 0.40f + 0.75f * color;
                break;

            case VoiceMode::STRING:
                // Slow drift breathe, right offset half a cycle from left.
                cl = cr = LedPalette::kPurple;
                bl *= 0.55f + 0.45f * ledWave01(_breathePhase);
                br *= 0.55f + 0.45f * ledWave01(_breathePhase + 0.5f);
                break;

            case VoiceMode::POLY:
            {
                // Left brightness tracks the active voice count (M38),
                // right shows the chord spread / detune width.
                float voices = (float)s.activeVoices * (1.0f / 6.0f);
                bl *= 0.25f + 0.75f * ledClamp01(voices);
                br *= 0.30f + 0.70f * rel;
                break;
            }
        }

        _led[int(LedId::VOICE_L)] = ledScale(cl, ledClamp01(bl * gL));
        _led[int(LedId::VOICE_R)] = ledScale(cr, ledClamp01(br * gR));
    }

    // -----------------------------------------------------------------------
    // D13 / D21 — the "motion" layer.  D13 is always MOTION depth; D21 is
    // always COLOR, and is deliberately dimmer.  Both follow the
    // voice-activity gradient so the flow of sound reads across the panel.
    //
    // D21 used to show a different parameter in each mode — detune here,
    // stereo width there — which meant the LED could not be read without
    // first remembering which mode you were in. It shows COLOR everywhere
    // now, which is only honest because COLOR finally *does* something
    // distinct in every mode: FM depth in PAIR and CASCADE, supersaw MIX in
    // CLOUD, chord voicing in CHORD, timbre spread in STRING and POLY.
    //
    // That gives the top two rows a clean pairing: VOICE_R shows what
    // RELATION is doing, MOD_R shows what COLOR is doing.
    // -----------------------------------------------------------------------
    void _renderModulation(const SynthParams &p)
    {
        const float color = ledClamp01(p.color);

        LedColor    c         = LedPalette::kGreen;
        const float secondary = color;

        switch(p.voiceMode)
        {
            case VoiceMode::PAIR: break;
            case VoiceMode::CLOUD: c = LedPalette::kCyan; break;
            case VoiceMode::CHORD: c = LedPalette::kAmber; break;
            case VoiceMode::CASCADE: c = LedPalette::kMagenta; break;
            case VoiceMode::STRING: c = LedPalette::kPurple; break;
            case VoiceMode::POLY: break;
        }

        // Rhythmic pulse at the drift rate, gated by MOTION depth.
        const float pulse    = 0.45f + 0.55f * ledWave01(_pulsePhase);
        const float gradient = 0.45f + 0.55f * _act;

        float bl = (0.06f + 0.94f * ledClamp01(p.motion)) * pulse * gradient;
        float br = 0.60f * (0.06f + 0.94f * secondary) * gradient;

        _led[int(LedId::MOD_L)] = ledScale(c, ledClamp01(bl));
        _led[int(LedId::MOD_R)] = ledScale(c, ledClamp01(br));
    }

    // -----------------------------------------------------------------------
    // D14 — one LED, one job: the current voice mode, as a steady colour.
    // PAIR (the default) sits dim so "no mode chosen" reads as neutral.
    // -----------------------------------------------------------------------
    void _renderMode(const SynthParams &p)
    {
        float    b = p.voiceMode == VoiceMode::PAIR ? 0.30f : 0.75f;
        LedColor c = ledScale(ledModeColor(p.voiceMode), b);
        if(_modeFlash > 0.0f)
            c = ledLerp(c, LedPalette::kWhite, _modeFlash);
        _led[int(LedId::MODE)] = c;
    }

    // -----------------------------------------------------------------------
    // D16 — dark in normal operation.  Lights only when something
    // state-level is active: SHIFT held, or drone mode running.
    // -----------------------------------------------------------------------
    void _renderShift(const LedSignals &s)
    {
        LedColor c = LedPalette::kOff;
        if(s.droneMode)
        {
            // Warm white slow breathe — drone is always visible.
            float b = 0.22f + 0.30f * ledWave01(_breathePhase);
            c       = ledScale(LedPalette::kWarmWhite, b);
        }
        if(s.shiftHeld)
        {
            // Steady white while held; brighter when it lands over drone.
            float b = s.droneMode ? 1.00f : 0.80f;
            c       = ledScale(LedPalette::kWhite, b);
        }
        _led[int(LedId::SHIFT)] = c;
    }

    // -----------------------------------------------------------------------
    // D15 — the module's pulse.  Even with no knowledge of the other six,
    // this LED says whether the module is doing something.
    //
    // Layered, weakest first:  MOTION drift pulse (mode-tinted) → gate/note
    // white attack → confirmation flashes.
    // -----------------------------------------------------------------------
    void _renderCentre(const SynthParams &p, const LedSignals &s)
    {
        const float motion = ledClamp01(p.motion);
        const float color  = ledClamp01(p.color);

        // Base layer: green motion pulse, tinted by whichever engine is
        // dominating the sound.
        LedColor base = LedPalette::kGreen;
        float    amt  = motion;

        if(p.voiceMode == VoiceMode::CASCADE && color > 0.05f)
        {
            base = LedPalette::kMagenta; // FM active — pulses with FM depth
            amt  = motion * 0.5f + color * 0.5f;
        }
        else if(p.voiceMode == VoiceMode::STRING
                && p.chorusMode != ChorusMode::OFF)
        {
            base = LedPalette::kPurple; // chorus heavy — slow movement
            amt  = motion * 0.5f + 0.35f;
        }

        float b;
        if(motion <= 0.001f && amt <= 0.001f)
            b = 0.0f; // static module — dark
        else if(s.droneMode && motion <= 0.001f)
            b = 0.05f + 0.07f * ledWave01(_breathePhase); // alive but still
        else
            b = amt * (0.20f + 0.80f * ledWave01(_pulsePhase));

        LedColor c = ledScale(base, ledClamp01(b));

        // Gate / MIDI note: bright white on attack, fading with the envelope.
        float noteAmt = _noteFlash;
        if(s.gateHigh || (!s.droneMode && _act > 0.01f))
        {
            float env = ledClamp01(_act);
            if(env > noteAmt)
                noteAmt = env;
        }
        if(noteAmt > 0.0f)
            c = ledLerp(c, LedPalette::kWhite, ledClamp01(noteAmt));

        // Confirmation: three quick white flashes over whatever is showing.
        if(_confirmT >= 0.0f)
        {
            float f = ledWave01(_confirmT * (3.0f / kConfirmDur));
            c       = ledLerp(c, LedPalette::kWhite, f);
        }

        _led[int(LedId::CENTRE)] = c;
    }

    // -----------------------------------------------------------------------
    // Calibration display (module-reference § "Calibration Routine Visual").
    // Overrides the normal LED language for the duration of the routine.
    // -----------------------------------------------------------------------
    void _renderCalibration()
    {
        for(int i = 0; i < int(LedId::LED_COUNT); ++i)
            _led[i] = LedPalette::kOff;

        // D15 pulses white for the whole routine.
        float pulse = 0.15f + 0.85f * ledWave01(_breathePhase * 6.0f);
        _led[int(LedId::CENTRE)] = ledScale(LedPalette::kWhite, pulse);

        // D14 steady white — awaiting input; flashes when a point is accepted.
        float modeB = 0.70f + 0.30f * _modeFlash;
        _led[int(LedId::MODE)]
            = ledScale(LedPalette::kWhite, ledClamp01(modeB));

        // Confirmed reference points latch full brightness.
        if(_cal == LedCalPhase::WAIT_3V || _cal == LedCalPhase::DONE)
            _led[int(LedId::VOICE_L)] = LedPalette::kWhite;
        if(_cal == LedCalPhase::DONE)
        {
            _led[int(LedId::SHIFT)] = LedPalette::kWhite;
            if(_confirmT >= 0.0f)
            {
                float f = ledWave01(_confirmT * (3.0f / kConfirmDur));
                _led[int(LedId::CENTRE)] = ledScale(LedPalette::kWhite, f);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Mode-change animation — ~300 ms, centre ignites first and ripples out:
    //   ring 0: D15            ring 1: D13/D14/D16/D21     ring 2: D12/D22
    // Each ring gets a white overlay that rises and falls, then everything
    // settles into the newly-rendered colours.
    // -----------------------------------------------------------------------
    void _applyRipple()
    {
        if(_rippleT < 0.0f)
            return;

        const float        t            = _rippleT / kRippleDur;
        static const float kRingStart[] = {0.00f, 0.18f, 0.34f};
        const float        kRingWidth   = 0.48f;

        for(int i = 0; i < int(LedId::LED_COUNT); ++i)
        {
            int ring;
            switch((LedId)i)
            {
                case LedId::CENTRE: ring = 0; break;
                case LedId::VOICE_L:
                case LedId::VOICE_R: ring = 2; break;
                default: ring = 1; break;
            }
            float local = (t - kRingStart[ring]) / kRingWidth;
            if(local <= 0.0f || local >= 1.0f)
                continue;
            _led[i] = ledLerp(_led[i], LedPalette::kWhite, ledWave01(local));
        }
    }

    // -----------------------------------------------------------------------
    // Mode count — the panel says which mode you landed on by lighting that
    // many LEDs, walking the silkscreen chain LED1..LED7 (kLedPanelOrder).
    //
    //   PAIR → LED1        CLOUD → LED1-2      CHORD   → LED1-3
    //   CASCADE → LED1-4   STRING → LED1-5     POLY    → LED1-6
    //
    // The LEDs *fill* rather than chase: each one lights in turn and stays
    // lit, so at the end of the sweep there are n LEDs burning that can be
    // counted at a glance. The newest one carries a white leading edge that
    // decays over one step, which is what makes the advance visible. Every
    // LED past n is forced dark for the duration — a half-lit neighbour is
    // the one thing that would make the count ambiguous.
    //
    // Runs after _applyRipple() and starts where the ripple ends, so a MODE
    // tap plays as one gesture: ignite, ripple out, count, settle.
    // -----------------------------------------------------------------------
    void _applyModeCount()
    {
        if(_countT < 0.0f || _cal != LedCalPhase::OFF)
            return;

        const float t = _countT - kRippleDur;
        if(t < 0.0f)
            return; // ripple still playing

        // Overlay strength: full through the fill and the hold, then eased
        // out so the normal display comes back rather than snapping in.
        const float fadeStart = (float)_countN * kCountStep + kCountHold;
        float       mix       = 1.0f;
        if(t > fadeStart)
            mix = 1.0f - (t - fadeStart) / kCountFade;
        mix = ledClamp01(mix);
        if(mix <= 0.0f)
            return;

        for(int pos = 0; pos < int(LedId::LED_COUNT); ++pos)
        {
            LedColor   &led = _led[int(kLedPanelOrder[pos])];
            const float lit = t - (float)pos * kCountStep;

            if(pos >= (int)_countN || lit <= 0.0f)
            {
                // Not part of the count, or not its turn yet.
                led = ledLerp(led, LedPalette::kOff, mix);
                continue;
            }

            LedColor    c    = _countColor;
            const float head = 1.0f - lit / kCountStep;
            if(head > 0.0f)
                c = ledLerp(c, LedPalette::kWhite, head * 0.75f);
            led = ledLerp(led, c, mix);
        }
    }

    /** Wall time one full count occupies, ripple included. */
    float _countDur() const
    {
        return kRippleDur + (float)_countN * kCountStep + kCountHold
               + kCountFade;
    }

    void _applyMaster()
    {
        if(_master >= 0.999f)
            return;
        for(int i = 0; i < int(LedId::LED_COUNT); ++i)
            _led[i] = ledScale(_led[i], _master);
    }

    // -----------------------------------------------------------------------
    LedColor _led[int(LedId::LED_COUNT)];

    float _master = 1.0f;
    float _lvlL   = 0.0f;
    float _lvlR   = 0.0f;
    float _act    = 0.0f;

    float _breathePhase = 0.0f;
    float _pulsePhase   = 0.0f;

    float _noteFlash = 0.0f; // gate attack flash    (D15)
    float _modeFlash = 0.0f; // mode-accept flash    (D14)
    float _rippleT   = -1.0f;
    float _confirmT  = -1.0f;

    float    _countT     = -1.0f; // mode count timer, ripple included
    uint8_t  _countN     = 0;     // LEDs to light — mode ordinal + 1
    LedColor _countColor = LedPalette::kOff;

    LedCalPhase _cal = LedCalPhase::OFF;
};
