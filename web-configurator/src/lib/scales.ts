/**
 * Scale masks for the quantizer indicator.
 *
 * **Mirrored from `modules/alloyflux/include/scale_quantizer.h`.** That header
 * is the authority — the firmware quantises against `kScaleMasks` there — and
 * these must stay in step with it. They are duplicated rather than generated
 * because the masks live in a hand-written C++ header, not in `params.json`:
 * the CC only carries a `ScaleId`, so nothing in the generated pipeline ever
 * sees which semitones a scale contains.
 *
 * Bit layout matches the header exactly: bit 0 = C, bit 1 = C#, … bit 11 = B.
 * Order matches the `scale` parameter's option list in params.json, which is
 * `ScaleId` order — index here is the CC value.
 */

/** Semitone masks by ScaleId. Index = CC value of the `scale` parameter. */
export const SCALE_MASKS: readonly number[] = [
  0x0fff, // CHROMATIC       all 12 — quantiser bypassed
  0xab5, // MAJOR           0 2 4 5 7 9 11
  0x5ad, // NATURAL_MINOR   0 2 3 5 7 8 10
  0x9ad, // HARMONIC_MINOR  0 2 3 5 7 8 11
  0xaad, // MELODIC_MINOR   0 2 3 5 7 9 11
  0x295, // PENTATONIC_MAJ  0 2 4 7 9
  0x4a9, // PENTATONIC_MIN  0 3 5 7 10
  0x4e9, // BLUES           0 3 5 6 7 10
  0x6ad, // DORIAN          0 2 3 5 7 9 10
  0x5ab, // PHRYGIAN        0 1 3 5 7 8 10
  0xad5, // LYDIAN          0 2 4 6 7 9 11
  0x6b5, // MIXOLYDIAN      0 2 4 5 7 9 10
  0x56b, // LOCRIAN         0 1 3 5 6 8 10
  0x555, // WHOLE_TONE      0 2 4 6 8 10
  0x6db, // DIMINISHED      0 1 3 4 6 7 9 10  (half-whole)
];

/** Note names for one octave, indexed by semitone. */
export const NOTE_NAMES = [
  "C",
  "C♯",
  "D",
  "D♯",
  "E",
  "F",
  "F♯",
  "G",
  "G♯",
  "A",
  "A♯",
  "B",
] as const;

/** Semitones that are black keys on a keyboard. */
export const IS_BLACK_KEY = [
  false,
  true,
  false,
  true,
  false,
  false,
  true,
  false,
  true,
  false,
  true,
  false,
] as const;

/**
 * The twelve semitones as booleans for a scale's CC value.
 * Falls back to chromatic (everything passes) for an unknown id, which is what
 * the firmware does too.
 */
export function scaleNotes(ccValue: number): boolean[] {
  const mask = SCALE_MASKS[ccValue] ?? SCALE_MASKS[0];
  return Array.from({ length: 12 }, (_, i) => (mask & (1 << i)) !== 0);
}

/** How many semitones the scale lets through. */
export function scaleNoteCount(ccValue: number): number {
  return scaleNotes(ccValue).filter(Boolean).length;
}
