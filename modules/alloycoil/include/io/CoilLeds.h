#pragma once

#include "io/HardwareIO.h"
#include "io/PanelMap.h" // Led:: — Alloy Coil's LED roles
#include "params.h"      // CoilParams — feedbackGain, echoSend, echoTime, …

#include <stdint.h>

// ---------------------------------------------------------------------------
// CoilLeds — the LED language for Alloy Coil, implemented once for both
// targets exactly as fillCoilParams() is.
//
// Platform-independent: no Arduino, no Rack, no libm. update() reads a module's
// goal values and produces seven normalised RGB colours; writeTo()
// pushes them through IHardwareIO, so a Dotstar chain and a Rack light widget
// get the same picture.
//
// It takes the params struct for the same reason io/IOBridge.h writes into one:
// a rack may hold more than one Alloy Coil, and a panel must be lit from the
// parameters of the module it is bolted to.
//
// What each LED says
// ------------------
// Alloy Coil is a feedback instrument. The one thing a player needs to see that the
// sound alone does not tell them in time is **how close the loop is to running
// away**, so that gets the two largest, most symmetric LEDs and the only colour
// ramp anyone has to learn:
//
//        LEVEL_L ·  ·  · LEVEL_R      LED1/LED7  output level, per channel
//      LOOP_L  ·  ·  ·  · LOOP_R      LED2/LED6  loop danger: green→amber→red
//         ECHO  ·   SPACE             LED3/LED5  echo pulse / reverb, shift
//            · CENTRE ·               LED4       string alive / exciter
//
// LOOP_* is green while the loop decays, amber as it approaches unity, red once
// it is building. The hue comes from the feedback gain, which is where the
// setting is; the *red* is also forced by a measured rise in output level, which
// is where the truth is — the loop's real gain depends on the feedback filters
// and the string, not only on the knob.
//
// Not wired on hardware yet: Alloy Coil's firmware has no IHardwareIO implementation
// at all (no ADC, no button engine — see modules/alloycoil/src/main.cpp). When it
// gets one, `leds.update(sig, dt, p); leds.writeTo(io);` in updateControl() is the
// whole integration, and the colours will match the plugin by construction.
// ---------------------------------------------------------------------------

namespace CoilLed
{

/** Normalised linear RGB, 0.0–1.0 per channel. Plain aggregate (C++11). */
struct Color
{
    float r;
    float g;
    float b;
};

// Hand-picked to stay distinguishable at low brightness on both a diffused
// Dotstar and a Rack light widget.
constexpr Color kOff   = {0.00f, 0.00f, 0.00f};
constexpr Color kGreen = {0.10f, 1.00f, 0.25f}; ///< loop decaying — safe
constexpr Color kAmber = {1.00f, 0.48f, 0.03f}; ///< loop near unity
constexpr Color kRed   = {1.00f, 0.07f, 0.03f}; ///< loop building — runaway
constexpr Color kWarm  = {1.00f, 0.62f, 0.30f}; ///< output level
constexpr Color kCyan  = {0.00f, 0.80f, 1.00f}; ///< echo
constexpr Color kBlue  = {0.18f, 0.32f, 1.00f}; ///< reverb
constexpr Color kWhite = {1.00f, 1.00f, 1.00f}; ///< shift / exciter

static inline float clamp01(float v)
{ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static inline Color scale(const Color &c, float k)
{ return Color{c.r * k, c.g * k, c.b * k}; }

static inline Color lerp(const Color &a, const Color &b, float t)
{
    return Color{
        a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

/**
 * Unipolar S-curve oscillator: 0–1 phase in, 0–1 raised-cosine-ish out.
 * A smoothstepped triangle — indistinguishable from a sine at LED resolution
 * and it keeps this header free of libm.
 */
static inline float wave01(float phase)
{
    phase -= (float)(int)phase;
    if(phase < 0.0f)
        phase += 1.0f;
    const float t = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    return t * t * (3.0f - 2.0f * t);
}

/**
 * Per-tick state that is not in CoilParams.
 *
 * peakL/peakR/exciter are peak absolute values *since the last update()*,
 * normalised to 0–1. The caller accumulates them in its audio path; the
 * attack/release smoothing that turns them into a usable brightness lives here
 * so both platforms behave identically.
 */
struct Signals
{
    float peakL          = 0.0f;
    float peakR          = 0.0f;
    float exciter        = 0.0f;
    bool  exciterPatched = false;
    bool  shiftHeld      = false;
    bool  warpHeld       = false;
};

// ---------------------------------------------------------------------------
class Engine
{
  public:
    Engine() { init(); }

    void init()
    {
        for(int i = 0; i < Led::kCount; ++i)
            _led[i] = kOff;
        _lvlL = _lvlR = _exc = 0.0f;
        _slow = 0.0f;
        _echoPhase = _breathePhase = 0.0f;
    }

    /** Global dimmer, 0.0 (dark) to 1.0 (full). */
    void setMasterBrightness(float b) { _master = clamp01(b); }

    /**
     * Recompute all seven colours. Call once per control tick.
     * @param s   peaks and button state since the previous call
     * @param dt  seconds since the previous call
     * @param p   the goal values this module is playing. Passed rather than
     *            reached for, so a host running several Alloy Coils lights
     *            each panel from its own parameters.
     */
    void update(const Signals &s, float dt, const CoilParams &p)
    {
        if(dt <= 0.0f)
            dt = 1.0f / 128.0f;

        _advance(s, dt, p);
        _renderLevel();
        _renderLoop(p);
        _renderEcho(s, p);
        _renderSpace(s, p);
        _renderCentre(s);

        if(_master < 0.999f)
            for(int i = 0; i < Led::kCount; ++i)
                _led[i] = scale(_led[i], _master);
    }

    const Color &color(LightId id) const { return _led[(int)id]; }

    /** Pushes all seven out through the platform, then latches the frame. */
    void writeTo(IHardwareIO &io) const
    {
        for(int i = 0; i < Led::kCount; ++i)
            io.writeLight((LightId)i, _led[i].r, _led[i].g, _led[i].b);
        io.showLights();
    }

  private:
    static constexpr float kAtkTau     = 0.004f;
    static constexpr float kRelTau     = 0.20f;
    static constexpr float kSlowTau    = 1.60f; ///< the "is it building?" ref
    static constexpr float kBreatheHz  = 0.16f;
    /// Feedback gain, in dB, at which the loop is at unity — see params.json.
    static constexpr float kGainMinDb  = -30.0f;
    static constexpr float kGainMaxDb  = 12.0f;

    static float _follow(float cur, float target, float dt, float tau)
    { return cur + (target - cur) * (dt / (tau + dt)); }

    void _advance(const Signals &s, float dt, const CoilParams &p)
    {
        const float pk = clamp01(s.peakL) > clamp01(s.peakR) ? clamp01(s.peakL)
                                                             : clamp01(s.peakR);

        _lvlL = _follow(_lvlL,
                        clamp01(s.peakL),
                        dt,
                        clamp01(s.peakL) > _lvlL ? kAtkTau : kRelTau);
        _lvlR = _follow(_lvlR,
                        clamp01(s.peakR),
                        dt,
                        clamp01(s.peakR) > _lvlR ? kAtkTau : kRelTau);
        _exc  = _follow(_exc,
                       clamp01(s.exciter),
                       dt,
                       clamp01(s.exciter) > _exc ? kAtkTau : kRelTau);

        // A second, much slower follower. The loop is *building* when the fast
        // level has pulled away from the slow one and stayed there — which is
        // exactly the condition a player cannot hear until it is too late,
        // because a slowly growing drone sounds like a drone.
        _slow = _follow(_slow, pk, dt, kSlowTau);

        // Echo pulse runs at one cycle per repeat, so the LED *shows the delay
        // time* — a knob whose effect is otherwise inaudible until you play.
        const float echoHz = p.echoTime > 0.001f ? 1.0f / p.echoTime : 1.0f;
        _echoPhase += echoHz * dt;
        _echoPhase -= (float)(int)_echoPhase;

        _breathePhase += kBreatheHz * dt;
        _breathePhase -= (float)(int)_breathePhase;
    }

    /** Loop danger, 0 (decaying) … 1 (running away). */
    float _danger(const CoilParams &p) const
    {
        // Where the knob is, over its full range.
        float d = clamp01((p.feedbackGain - kGainMinDb)
                          / (kGainMaxDb - kGainMinDb));

        // Where the sound actually is. A sustained rise of the fast follower
        // over the slow one means energy is accumulating in the loop; that
        // overrides the knob, because the true loop gain includes the feedback
        // filters and the string, not just this setting.
        const float rise = _lvlL > _lvlR ? _lvlL : _lvlR;
        if(rise > _slow + 0.02f)
        {
            const float growth = clamp01((rise - _slow) * 6.0f);
            if(growth > d)
                d = growth;
        }
        return d;
    }

    // LED1 / LED7 — plain output level, one per channel. No interpretation.
    void _renderLevel()
    {
        _led[(int)Led::LEVEL_L] = scale(kWarm, clamp01(_lvlL));
        _led[(int)Led::LEVEL_R] = scale(kWarm, clamp01(_lvlR));
    }

    // LED2 / LED6 — the loop-danger meter, mirrored so it reads as the whole
    // ring heating up rather than as two separate indicators.
    void _renderLoop(const CoilParams &p)
    {
        const float d = _danger(p);

        // Unity gain sits at 0 dB, which is not the middle of the knob's range.
        // The green→amber knee is put there rather than at 0.5 so that "amber"
        // means the one thing it should mean.
        constexpr float kUnity = (0.0f - kGainMinDb) / (kGainMaxDb - kGainMinDb);

        Color c;
        if(d <= kUnity)
            c = lerp(kGreen, kAmber, d / kUnity);
        else
            c = lerp(kAmber, kRed, (d - kUnity) / (1.0f - kUnity));

        // Brightness follows danger, with a floor so the pair is never fully
        // dark — an unlit ring would be indistinguishable from a dead module.
        // Above unity it also breathes, which is hard to ignore in peripheral
        // vision and is the point.
        float b = 0.10f + 0.90f * d;
        if(d > kUnity)
            b *= 0.55f + 0.45f * wave01(_breathePhase * 5.0f);

        const Color out          = scale(c, clamp01(b));
        _led[(int)Led::LOOP_L] = out;
        _led[(int)Led::LOOP_R] = out;
    }

    // LED3 — echo. Brightness is the send amount; it flashes once per repeat,
    // so the delay time is visible while setting it. WARP (doppler warp) shows
    // here rather than on the centre LED, because what it does is halve the
    // echo time — and the flash rate doubling under your finger is the clearest
    // possible readout of that.
    void _renderEcho(const Signals &s, const CoilParams &p)
    {
        const float send = clamp01(p.echoSend);
        if(send <= 0.001f)
        {
            // Still acknowledge the button when the echo is silent, or holding
            // WARP with the send down looks like a dead panel.
            _led[(int)Led::ECHO] = s.warpHeld ? scale(kWhite, 0.25f) : kOff;
            return;
        }
        // Short flash rather than a sine: a repeat is an event, not a swell.
        const float ph    = _echoPhase;
        const float flash = ph < 0.18f ? wave01(ph * (0.5f / 0.18f)) : 0.0f;
        const float fb    = clamp01(p.echoFeedback / 1.2f);
        const float b     = send * (0.18f + 0.82f * flash);

        // Feedback pushes the hue toward the loop colours: a long echo tail is
        // the same kind of accumulation the feedback ring does.
        Color c = lerp(kCyan, kAmber, fb);
        if(s.warpHeld)
            c = lerp(c, kWhite, 0.7f);
        _led[(int)Led::ECHO] = scale(c, clamp01(b));
    }

    // LED5 — reverb presence, and SHIFT while it is held. SHIFT wins: knowing
    // which parameter layer the knobs address matters more than the reverb.
    void _renderSpace(const Signals &s, const CoilParams &p)
    {
        if(s.shiftHeld)
        {
            _led[(int)Led::SPACE] = scale(kWhite, 0.85f);
            return;
        }
        const float mix   = clamp01(p.reverbMix);
        const float decay = clamp01((p.reverbDecay - 0.2f) / 0.8f);
        _led[(int)Led::SPACE]
            = scale(kBlue, clamp01(mix * (0.25f + 0.75f * decay)));
    }

    // LED4 — is the string alive, and is anything driving it. White with the
    // exciter when one is patched; otherwise a slow breathe scaled by output
    // energy, so a self-oscillating drone still shows a pulse.
    void _renderCentre(const Signals &s)
    {
        const float energy = _lvlL > _lvlR ? _lvlL : _lvlR;

        float b = (0.04f + 0.30f * wave01(_breathePhase))
                  * (0.30f + 0.70f * energy);
        Color c = scale(kWarm, clamp01(b));

        if(s.exciterPatched && _exc > 0.001f)
            c = lerp(c, kWhite, clamp01(_exc));

        _led[(int)Led::CENTRE] = c;
    }

    Color _led[Led::kCount];

    float _master = 1.0f;
    float _lvlL   = 0.0f;
    float _lvlR   = 0.0f;
    float _exc    = 0.0f;
    float _slow   = 0.0f;

    float _echoPhase    = 0.0f;
    float _breathePhase = 0.0f;
};

} // namespace CoilLed
