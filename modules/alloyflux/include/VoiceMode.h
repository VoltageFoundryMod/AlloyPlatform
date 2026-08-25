#pragma once

#include <stdint.h>

/**
 * VoiceMode — Alloy Flux synthesis personality selector (Milestone 21+).
 *
 * Mode is selected by tapping the panel button (Milestone 31 hardware UI).
 * For current firmware, use the `mode` serial command.
 *
 * | Mode    | Voices | RELATION controls                          | Milestone |
 * | ------- | ------ | ------------------------------------------ | --------- |
 * | PAIR    | 2      | Interval + fine detune (voice 2 offset)    | M21 ✅    |
 * | CLOUD   | 7 × 1–4| Supersaw detune spread — poly, drones idle | M22/M78 ✅|
 * | CHORD   | 4      | Chord shape — sweeps through chord table   | M23 ✅    |
 * | CASCADE | 2      | FM interaction depth and character         | M24       |
 * | STRING  | 4–8    | Ensemble width and microdetune             | M25       |
 * | PLASMA  | 2×2    | M:C ratio of the cross-modulating pair     | M72 ✅    |
 * | POLY    | 4      | 4-voice polyphony — per-voice envelopes    | M2x ✅    |
 *
 * Modes share the same synthesis backbone (SHAPE, CURVE, MOTION, SPACE, FATNESS)
 * and differ only in how voices are pitched and panned.
 *
 * **POLY sits last, on its own.** Everything above it drones: left alone in
 * PAIR through PLASMA the module keeps sounding, and those six are variations
 * on how an ensemble is voiced. POLY is the one mode that says nothing until a
 * note arrives, so the cycle runs the ensembles together and lands on the
 * keyboard mode at the end.
 *
 * **Seven is the ceiling.** The panel counts the mode out on its seven LEDs
 * when you tap MODE (see LedEngine::notifyModeChanged), so an eighth would
 * have nowhere to show itself.
 */
enum class VoiceMode : uint8_t
{
    PAIR    = 0, // ROOT + RELATION dual voice — default
    CLOUD   = 1, // seven-oscillator supersaw (M22)
    CHORD   = 2, // interval stack from chord table (M23)
    CASCADE = 3, // restrained FM oscillator interaction (M24)
    STRING  = 4, // vintage string machine ensemble (M25)
    PLASMA  = 5, // cross-modulating pair through a ring mod (M72)
    POLY    = 6, // 4-voice polyphonic — independent per-voice envelopes (M2x)
};

inline const char *voiceModeName(VoiceMode m)
{
    switch(m)
    {
        case VoiceMode::PAIR: return "PAIR";
        case VoiceMode::CLOUD: return "CLOUD";
        case VoiceMode::CHORD: return "CHORD";
        case VoiceMode::CASCADE: return "CASCADE";
        case VoiceMode::STRING: return "STRING";
        case VoiceMode::PLASMA: return "PLASMA";
        case VoiceMode::POLY: return "POLY";
        default: return "PAIR";
    }
}

/**
 * Does this mode take its notes from the sPolySlots[] allocator?
 *
 * POLY always has. CLOUD joined it at M78: its supersaw stack is now built per
 * held note out of a shared oscillator pool, so MIDI and the GATE jack have to
 * reach the same allocator rather than driving one mono pitch.
 *
 * The predicate exists because "is this mode polyphonic" is asked in eight
 * places across the firmware, the VCV module and the engine — note on, note
 * off, the CV-gate block, the LED voice count, the mono gate-edge handler and
 * the global VCA among them — and every one of them was spelled
 * `== VoiceMode::POLY`. Adding a second polyphonic mode by hand-editing eight
 * comparisons is how one of them gets missed.
 *
 * ⚠ This says *where the notes come from*, not *what is sounding*. CLOUD with
 * no notes held still drones, and that drone is not a slot — see the branch on
 * SynthParams::gatePatched in SynthEngine::control().
 */
inline bool modeUsesPolySlots(VoiceMode m)
{ return m == VoiceMode::POLY || m == VoiceMode::CLOUD; }

// ---------------------------------------------------------------------------
// CLOUD's oscillator pool — the bounds only (M78).
//
// These live here rather than in SynthEngine.h because the console clamps
// against them and commands.cpp has no business pulling the whole engine in
// for two integers. The pool's *behaviour* — how width is derived, how levels
// ramp — stays with the engine; this is just how big it may get.
// ---------------------------------------------------------------------------

/// Oscillators the CLOUD pool may ever contain. Array size, not the runtime
/// figure: sixteen leaves room to experiment past what currently measures safe,
/// and an oscillator is ~40 bytes, so raising the ceiling costs nothing. CPU is
/// what actually binds.
static constexpr uint8_t kCloudOscMax = 16;

/// Simultaneous held notes CLOUD will allocate. Four against POLY's six —
/// every CLOUD note costs a whole stack, not one oscillator.
static constexpr uint8_t kCloudMaxNotes = 4;

/// Default pool. Twelve mains plus the mode's one sub is thirteen reads a
/// sample, alongside POLY's measured twelve — the most expensive thing on the
/// module that is known to fit with every effect running. It puts one note at
/// the full seven-saw stack, two at five each, three and four at three.
static constexpr uint8_t kCloudPoolDefault = 16;
