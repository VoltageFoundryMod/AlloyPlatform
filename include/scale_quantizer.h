#pragma once
#include <stdint.h>

/**
 * Scale quantizer — Milestone 49.
 *
 * Quantizes a MIDI note number to the nearest pitch belonging to a chosen
 * scale, then applies a semitone transpose.
 *
 * The chord-shape intervals used by CHORD mode (main.cpp kChordTable) and the
 * scales here are complementary: kChordTable maps four-voice voicings from a
 * root, while this quantizer snaps incoming *pitch* to a scale before it ever
 * reaches the voice engine.  The two tables share the same musical vocabulary
 * (minor third = 3 st, fifth = 7 st, etc.) but serve different purposes and
 * are intentionally kept separate.
 *
 * Scale masks: 12-bit value, bit 0 = C, bit 1 = C#/Db, …, bit 11 = B.
 * A '1' means that semitone is in the scale (relative to C root).
 *
 * Quantization rule: find the in-scale semitone closest to the input; ties
 * (equidistant up/down) resolve upward.
 *
 * Usage:
 *   uint8_t note = quantizeNote(rawNote, gQuantizeScale, gTranspose);
 *   float hz = midiNoteToHz(note);
 */

// ---------------------------------------------------------------------------
// ScaleId enum
// ---------------------------------------------------------------------------

enum class ScaleId : uint8_t {
    CHROMATIC = 0,  // off — all 12 semitones pass through (identity)
    MAJOR,          // Ionian      W W H W W W H
    NATURAL_MINOR,  // Aeolian     W H W W H W W
    HARMONIC_MINOR, // Aeolian + raised 7th
    MELODIC_MINOR,  // ascending only — C D Eb F G A B
    PENTATONIC_MAJ, // C D E G A
    PENTATONIC_MIN, // C Eb F G Bb
    BLUES,          // C Eb F Gb G Bb (minor penta + b5)
    DORIAN,         // W H W W W H W
    PHRYGIAN,       // H W W W H W W
    LYDIAN,         // W W W H W W H
    MIXOLYDIAN,     // W W H W W H W
    LOCRIAN,        // H W W H W W W
    WHOLE_TONE,     // W W W W W W
    DIMINISHED,     // half-whole octatonic: H W H W H W H W
    COUNT,          // sentinel — number of valid scales
};

// Human-readable names (lowercase, no spaces) — indexed by ScaleId.
// Used by cmd_scale parser and status output.
static const char *const kScaleNames[] = {
    "chromatic",
    "major",
    "minor",
    "harmonic_minor",
    "melodic_minor",
    "pentatonic_maj",
    "pentatonic_min",
    "blues",
    "dorian",
    "phrygian",
    "lydian",
    "mixolydian",
    "locrian",
    "whole_tone",
    "diminished",
};

// ---------------------------------------------------------------------------
// Scale masks
//
// Bit layout: bit 0 = C, bit 1 = C#, bit 2 = D, ..., bit 11 = B
//
// Verification — MAJOR (C D E F G A B = semitones 0,2,4,5,7,9,11):
//   (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11) = 1+4+16+32+128+512+2048 = 2741 = 0xAB5
// ---------------------------------------------------------------------------

static const uint16_t kScaleMasks[(uint8_t)ScaleId::COUNT] = {
    // CHROMATIC:       all 12 semitones
    0x0FFF,
    // MAJOR:           0  2  4  5  7  9  11   → 0xAB5
    (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 11),
    // NATURAL_MINOR:   0  2  3  5  7  8  10   → 0x5AD
    (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 10),
    // HARMONIC_MINOR:  0  2  3  5  7  8  11   → 0x9AD
    (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 11),
    // MELODIC_MINOR:   0  2  3  5  7  9  11   → 0xAAD
    (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 11),
    // PENTATONIC_MAJ:  0  2  4  7  9          → 0x295
    (1u << 0) | (1u << 2) | (1u << 4) | (1u << 7) | (1u << 9),
    // PENTATONIC_MIN:  0  3  5  7  10         → 0x4A9
    (1u << 0) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 10),
    // BLUES:           0  3  5  6  7  10      → 0x4E9
    (1u << 0) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10),
    // DORIAN:          0  2  3  5  7  9  10   → 0x6AD
    (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 10),
    // PHRYGIAN:        0  1  3  5  7  8  10   → 0x5AB
    (1u << 0) | (1u << 1) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 10),
    // LYDIAN:          0  2  4  6  7  9  11   → 0xAD5
    (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 11),
    // MIXOLYDIAN:      0  2  4  5  7  9  10   → 0x6B5
    (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 10),
    // LOCRIAN:         0  1  3  5  6  8  10   → 0x56B
    (1u << 0) | (1u << 1) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 8) | (1u << 10),
    // WHOLE_TONE:      0  2  4  6  8  10      → 0x555
    (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 8) | (1u << 10),
    // DIMINISHED (H-W):0  1  3  4  6  7  9  10 → 0x6DB
    (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 10),
};

// ---------------------------------------------------------------------------
// quantizeNote()
//
// Returns the quantized, transposed MIDI note number (0–127).
// Call this on rawNote (0–127) before midiNoteToHz().
//
// Parameters:
//   note      — raw MIDI note number from Note On message
//   scale     — scale to snap to; CHROMATIC = bypass
//   transpose — signed semitone offset applied after quantization (−24…+24)
// ---------------------------------------------------------------------------

static inline uint8_t quantizeNote(uint8_t note, ScaleId scale, int8_t transpose) {
    // 1. Apply transpose (clamp to 0–127).
    int16_t t = (int16_t)note + (int16_t)transpose;
    if (t < 0)
        t = 0;
    if (t > 127)
        t = 127;
    uint8_t q = (uint8_t)t;

    // 2. Chromatic = identity.
    if (scale == ScaleId::CHROMATIC || scale >= ScaleId::COUNT)
        return q;

    // 3. Look up the scale mask.
    const uint16_t mask = kScaleMasks[(uint8_t)scale];
    const uint8_t semi = q % 12;

    // 4. Short-circuit: already in scale.
    if (mask & (1u << semi))
        return q;

    // 5. Search outward: check up then down at each distance (1–6).
    //    Ties (dist == 6 and both ± exist) resolve upward.
    for (uint8_t dist = 1; dist <= 6; ++dist) {
        uint8_t up = (semi + dist) % 12;
        if (mask & (1u << up)) {
            int16_t n = (int16_t)q + (int16_t)dist;
            return (n > 127) ? 127u : (uint8_t)n;
        }
        uint8_t down = (semi + 12u - dist) % 12;
        if (mask & (1u << down)) {
            int16_t n = (int16_t)q - (int16_t)dist;
            return (n < 0) ? 0u : (uint8_t)n;
        }
    }

    // Fallback — unreachable for any mask with ≥ 2 bits set.
    return q;
}
