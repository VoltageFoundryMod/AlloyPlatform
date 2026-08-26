#pragma once

#include "VoiceMode.h"        // VoiceMode enum
#include "dsp/ChorusEngine.h" // ChorusMode enum
#include "dsp/CurveEngine.h"  // EnvelopeType enum (M5x)
#include "dsp/FilterEngine.h" // FilterMode, FilterType enums (M26a / M5x)
#include "io/PotTakeover.h"   // PotTakeoverMode enum (M62)
#include "scale_quantizer.h"  // ScaleId enum (M49)

/**
 * Shared synthesis parameters — defined in main.cpp.
 *
 * Naming convention:
 *   gXxx  goal values: written by serial console / knobs / MIDI in updateControl()
 *   sXxx  smoothed values: read by renderAudio(), one-pole LPF applied in updateControl()
 */

// Voice pitch & colour
extern float gBaseFreq; // Hz, voice 1 root pitch (20–8000)
extern float
    gColor; // 0–1: FM depth in PAIR/CASCADE; Hz fine spread in ensemble modes
extern float
    gFmAmount; // 0–1: FM IN jack depth, both halves (SHIFT+ROOT). Not gColor —
               // that is the *internal* FM index and only in two modes.

// FM IN jack voltage, sampled at audio rate on Core 1 (M77).
//
// The one signal on the module that is not a control-rate value, and the reason
// FM IN sits on GP27 — a direct ADC pin, deliberately off the analogue mux that
// carries every other jack. renderAudio() hands it to
// SynthEngine::setFmInSample() once per frame.
//
// ⚠ Nothing writes this yet. The ADC conversion belongs to whatever drives
// GP27 free-running (DMA into a one-word landing pad is the cheap shape — a
// blocking adc_read() is ~2 µs against a 20.8 µs frame budget, 10% of the
// audio core for one jack). Until that lands it stays 0.0 and FM IN is
// silent on hardware, exactly as every other CV jack is: HardwarePicoIO's
// readCV() still returns 0 for everything but GATE. The engine side is
// complete and needs no change when the ADC arrives — only this global.
extern volatile float gFmInVolts;

// Voice mode and RELATION
extern VoiceMode gVoiceMode; // synthesis personality (default: PAIR)
extern float
    gRelation; // semitones above ROOT for voice 2: 0=unison, 7=fifth, 12=octave, 24=max (PAIR mode)

// Timbre
extern float
    gShape; // 0.0 = sine  0.25 = tri  0.50 = saw  0.75 = pulse  1.0 = hollow
extern float gFatness; // 0.0 = no sub osc  …  1.0 = sub at 50% of main level
extern uint8_t
    gSubOctave; // 1 = one octave below (÷2), 2 = two octaves below (÷4)

// Animation
extern float gMotion; // 0.0 = static  …  1.0 = full drift + chorus depth
extern float
    gDriftSpeed; // one-pole glide coeff: 0.001 (slow) … 0.10 (fast), default 0.025

// Chorus
extern ChorusMode gChorusMode; // OFF / I / II / I_II  (default: I_II)

// Space
extern float
    gSpace; // stereo width: 0.0 = mono, 1.0 = identity, 2.0 = hyper-wide (default: 1.0)

// Envelope / VCA (Milestone 15)
extern float gCurve; // 0.0 = pluck … 1.0 = swell
extern float
    gCurveTime; // envelope time scale: 0.25=4×faster  1.0=default  4.0=4×slower
// GATE jack note length, ms. 0 = follow the gate: the note is released when the
// gate falls, which is the classic behaviour. Above 0 the gate is treated as a
// trigger and the note lasts this long regardless of how wide the gate is.
//
// The point of it is POLY. Below CURVE 0.20 the AR envelope already ignores the
// gate (see CurveEngine.h), so short gates already stack into a chord — but at
// sustaining CURVE settings a gate holds its voice, and you cannot build a pad
// up from an arpeggio. A fixed length gives that, bounded, at any CURVE.
extern float         gGateLength;
extern volatile bool gGateHigh; // true while gate is asserted (attack phase)
extern volatile bool
    gGatePatched; // false = drone (bypass VCA); true = AR envelope active
// Active envelope engine pointer (concrete type: AREnvelope or ADSREnvelope).
// Arming setGate() on this from any gate source ensures the ISR never sees
// gGatePatched=true while the envelope is still in IDLE (which caused a click).
extern EnvelopeEngine *gCurveEng;

// CLOUD's oscillator pool (M78). The mode builds a supersaw stack per held
// note out of a shared pool, and per-note width is whatever divides — one note
// still gets the full seven-saw stack, four share out to three each.
//
// Runtime rather than compile-time because the ceiling is a *measurement*, not
// a derivation: twelve is what sits alongside POLY's measured cost with every
// effect running, but the only way to find out whether fifteen also fits is to
// set it on real hardware and watch `slow-blk` and `overruns` for a while.
// `cloud pool <n>` and `cloud notes <n>` on the console.
//
// Firmware storage only — one board, one engine, one pool. The VCV module keeps
// its own pair (_cloudPool/_cloudMaxNotes) so two AlloyFluxes in a rack can be
// budgeted separately and each patch remembers what it was set to; it passes
// them to polySlotLimit()'s two-argument overload below rather than touching
// these. Nothing in the Rack build defines them.
extern uint8_t gCloudPool;     // oscillators CLOUD may sound at once, 1–16
extern uint8_t gCloudMaxNotes; // simultaneous held notes in CLOUD, 1–4

// Level
extern float gVolume; // 0.0 – 1.0 master output
extern float
    gMidiVelocity; // 0.0 – 1.0 per-note MIDI velocity scale (1.0 = full, reset on drone return)
                   // Always 1.0 for CV / drone / button gate sources.
extern bool
    gVelocitySensitive; // true (default) = MIDI velocity scales output; false = fixed at 1.0

// Portamento / glide
extern float
    gGlideTime; // portamento slide time in seconds (CC 5); 0.0 = instant
extern bool
    gGlideEnabled; // portamento on/off (CC 65); false = instant transitions

// Scale quantizer (M49)
extern ScaleId gQuantizeScale; // CHROMATIC = bypass (default)
extern int8_t  gTranspose;     // semitone offset: −24…+24, default 0

// MIDI receive channel is platform-owned — see platform/include/io/usb_midi.h

// Knob takeover (M62) — how a physical pot regains control of a parameter that
// was last set from the Web Configurator, MIDI, the serial console or a preset.
// Firmware only: VCV knobs are virtual and are simply moved by incoming CC.
extern PotTakeoverMode gPotTakeoverMode; // default: SCALE (soft pickup)

// CPU profiling — defined in main.cpp, only present when CPU_PROFILE is set.
// gAudioElapsedUs : µs spent rendering and queueing the last audio block
// gAudioOverruns  : DMA underflows — blocks the hardware ran dry on, i.e.
//                   audible dropouts
// gAudioBudgetUs  : wall-clock µs one audio block represents; the budget
//                   gAudioElapsedUs is measured against
#ifdef CPU_PROFILE
extern volatile bool gPerformancePrintEnabled; // set by cmd_performance_print
extern volatile uint32_t gAudioElapsedUs;
extern volatile uint32_t gAudioOverruns;
extern volatile uint32_t gAudioBudgetUs;
#endif

// ---------------------------------------------------------------------------
// M26 Post-effects section
// ---------------------------------------------------------------------------

// Filter (M26a / M5x)
extern float
    gFilterCutoff; // Hz, 20–16000, default kDefaultFilterCutoff ≈ 983 Hz (CC 74 on log scale)
extern float
    gFilterRes; // 0.0 (flat) – 1.0 (near self-oscillation), default 0.0
extern FilterMode gFilterMode; // OFF by default — zero CPU cost when bypassed
extern FilterType
    gFilterType; // SVF (default) or LADDER — runtime-selectable (M5x)

// Abstract filter instance pointer — dereferences to SVFFilter or OTALadder.
// Defined in main.cpp.  commands.cpp uses this to avoid including the concrete headers.
class FilterEngine;
extern FilterEngine *gFilterInst;

// Envelope / VCA type (M5x — runtime-selectable)
extern EnvelopeType gEnvelopeType; // AR (default) or ADSR

// Abstract envelope instance pointer — dereferences to AREnvelope or ADSREnvelope.
class EnvelopeEngine;
extern EnvelopeEngine *gCurveEng;

// ADSR envelope parameters (used when gEnvelopeType == ADSR)
extern float gAdsrAttack;  // seconds, 0.001–10.0
extern float gAdsrDecay;   // seconds, 0.001–10.0
extern float gAdsrSustain; // 0.0–1.0
extern float gAdsrRelease; // seconds, 0.001–10.0
extern bool
    gAdsrLoop; // loop mode: envelope restarts automatically after release

// Reverb (M26b) — active when gRevEnabled
extern volatile float
    gRevSize; // 0.0 (small room) – 1.0 (long plate), default 0.5
extern volatile float
    gRevDamping; // 0.0 (bright) – 1.0 (dark HF loss), default 0.5
extern volatile float
    gRevModSpeed; // 0.1 (glacial) – 4.0 (fast shimmer), default 1.0 (M40)
extern volatile float
    gRevModDepth; // 0.0 (static) – 1.0 (full ±8 sample swing), default 1.0 (M40)
extern volatile bool
    gRevFrozen; // false = normal; true = infinite sustain (M41)
// gRevMix / gRevEnabled are volatile — declared in ReverbEngine.h

// Delay (M26c — pass-through stub; parameters stored ready for implementation)
extern float gDelayTime; // ms, 10–DELAY_MAX_MS, default 100
extern float
    gDelayFeedback;     // 0.0 (single echo) – 0.95 (long decay), default 0.5
extern float gDelayMix; // 0.0 (off) – 1.0 (full wet), default 0.0

// ---------------------------------------------------------------------------
// POLY mode voice allocator (M2x)
//
// 6 independent voice slots — each carries its own MIDI note number, frequency,
// and velocity.  Written by MIDI handlers inside usbMidi_update(), read by
// updateControl() for freq/velocity and by renderAudio() via sPolyEnvs[].
// All fields accessed from a single core (Core 0), so no mutex is needed; 32-bit
// aligned float writes are atomic on Cortex-M33.
//
// MIDI, the serial `trig` command and the GATE jack all draw from this one
// pool. `midiNote` doubles as the slot's occupancy state, which is why the
// sentinels below sit above the 0..127 MIDI range — a real note number can
// never collide with one.
// ---------------------------------------------------------------------------

/// Slot is silent and free to claim.
static constexpr uint8_t kPolySlotFree = 255;
/// Claimed by a CV gate that is still high. Released on the falling edge.
static constexpr uint8_t kPolySlotCvHeld = 128;
/// CV gate has fallen and the voice is ringing out. Still audible, so it is
/// reaped only once its envelope reaches silence — but it is the first thing
/// a new note takes if no slot is genuinely free.
static constexpr uint8_t kPolySlotReleasing = 129;

struct PolySlot
{
    float freq;     // Hz — voice frequency set by Note On
    float velocity; // 0.0–1.0 — MIDI velocity scale
    uint8_t
        midiNote; // MIDI note number, or one of the kPolySlot* sentinels above
};

// Defined in main.cpp — accessed by usb_midi.cpp for note allocation.
extern PolySlot sPolySlots[6];
// Round-robin slot counter — incremented on each Note On in POLY mode.
extern uint8_t sPolyRR;

// Active monophonic MIDI note (non-POLY modes). 255 = no note held.
// Written by usb_midi.cpp Note On/Off; read by main.cpp to override
// p.baseFreq so MIDI pitch wins over V/OCT CV when a note is held.
extern uint8_t sActiveNote;

// Per-voice AR envelopes for POLY mode — defined in main.cpp.
// setCurve() called in updateControl(); next() called in renderAudio().
// EnvelopeEngine forward-declared above; concrete type AREnvelope<>.
extern EnvelopeEngine *sPolyEnvs[6]; // pointers so ISR can call virtual next()

/**
 * Claim a POLY voice slot and start a note on it.  Implemented in main.cpp,
 * which owns the engine instance.
 *
 * This is the seam that keeps the engine out of the generic mechanisms: both
 * MIDI Note On and the `trig` console command need a voice, and neither should
 * have to know what a SynthEngine is — or duplicate the allocator, which is
 * what they did before.
 *
 * Picks the first free slot from the round-robin cursor, stealing the oldest
 * when all six are busy.
 *
 * @param freq     voice frequency in Hz
 * @param velocity 0.0–1.0 output scale
 * @param subMult  sub-oscillator multiplier (0.5 = −1 oct, 0.25 = −2 oct)
 * @param noteTag  MIDI note number, for Note Off matching; 254 = owned by a
 *                 trig pulse rather than a held note; 255 is reserved for free
 * @param maxSlots how many of the six slots this mode may use. POLY takes all
 *                 six; CLOUD is bounded by gCloudMaxNotes, because every note
 *                 it plays costs a whole stack of oscillators out of a pool
 *                 that a seventh note would leave nothing for.
 * @return the slot claimed, 0–5
 */
uint8_t polyNoteOn(float   freq,
                   float   velocity,
                   float   subMult,
                   uint8_t noteTag,
                   uint8_t maxSlots = 6);

/**
 * How many poly slots the given mode may allocate. One line, but it is the
 * difference between CLOUD stealing its own notes correctly and CLOUD
 * allocating a fifth note into a pool sized for four.
 *
 * Two overloads because the two targets store the ceiling in different places
 * and neither should have to know the other's. The firmware has one engine and
 * keeps it in gCloudMaxNotes; VCV has one per module and keeps it in the
 * module, so it passes its own. The clamp lives here either way — a caller that
 * reached for the global directly is a caller that can forget to clamp.
 */
inline uint8_t polySlotLimit(VoiceMode m, uint8_t cloudMaxNotes)
{
    if(m != VoiceMode::CLOUD)
        return 6u;
    return (cloudMaxNotes < 1u)   ? 1u
           : (cloudMaxNotes > 6u) ? 6u
                                  : cloudMaxNotes;
}

inline uint8_t polySlotLimit(VoiceMode m)
{ return polySlotLimit(m, gCloudMaxNotes); }

/**
 * Hand the module back to the drone: clear the gate latch, release every poly
 * slot and restore full volume. Both routes to it — MIDI CC 119 and the
 * MODE+SHIFT panel combo — call this rather than each clearing their own
 * subset of the state. Defined in main.cpp.
 */
void returnToDrone();

/**
 * Fire a gate pulse of the given duration on the current voice mode.
 * In POLY mode a free voice slot is allocated at gBaseFreq and released when
 * the pulse expires; in all other modes it drives gGateHigh for the same
 * duration.  Called from the SHIFT button handler and the `trig` command.
 * Defined in commands.cpp.
 */
void doTrig(uint32_t durMs);

/**
 * Re-attach every knob to its parameter immediately, without waiting for it to
 * be moved (M62).  Backs `pot sync`: the next control tick snaps all
 * knob-owned parameters to the physical knob positions.  Defined in main.cpp,
 * which owns the HardwarePicoIO instance.
 */
void potsReattach();