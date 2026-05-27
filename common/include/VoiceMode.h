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
 * | CLOUD   | 4      | Ensemble spread and voice density          | M22 ✅    |
 * | CHORD   | 4      | Chord shape — sweeps through chord table   | M23 ✅    |
 * | CASCADE | 2      | FM interaction depth and character         | M24       |
 * | STRING  | 4–8    | Ensemble width and microdetune             | M25       |
 * | POLY    | 4      | 4-voice polyphony — per-voice envelopes    | M2x ✅    |
 *
 * Modes share the same synthesis backbone (SHAPE, CURVE, MOTION, SPACE, FATNESS)
 * and differ only in how voices are pitched and panned.
 */
enum class VoiceMode : uint8_t
{
    PAIR    = 0, // ROOT + RELATION dual voice — default
    CLOUD   = 1, // multi-voice detuned ensemble (M22)
    CHORD   = 2, // interval stack from chord table (M23)
    CASCADE = 3, // restrained FM oscillator interaction (M24)
    STRING  = 4, // vintage string machine ensemble (M25)
    POLY    = 5, // 4-voice polyphonic — independent per-voice envelopes (M2x)
};

static constexpr uint8_t kVoiceModeCount = 6;

inline const char *voiceModeName(VoiceMode m)
{
    switch(m)
    {
        case VoiceMode::PAIR: return "PAIR";
        case VoiceMode::CLOUD: return "CLOUD";
        case VoiceMode::CHORD: return "CHORD";
        case VoiceMode::CASCADE: return "CASCADE";
        case VoiceMode::STRING: return "STRING";
        case VoiceMode::POLY: return "POLY";
        default: return "PAIR";
    }
}
