# Alloy Coil — User Manual

## Feedback Resonator · Stereo Voice · Eurorack

---

## What Is Alloy Coil?

Alloy Coil is a **feedback instrument**. At its heart is a plucked-string
resonator whose output is fed back into its own input, through a waveshaper and
a pair of filters. Turn the feedback up and the string stops decaying and starts
sustaining; turn it up further and it blooms into saturation and keeps going.

There is no envelope, no gate and no "note off". A note sets the pitch and the
string stays where you put it. What you play is the _loop_ — how much energy is
in it, how bright it is, and how long it takes to come back around.

Left alone with the feedback up, it will find something to do: the resonator
self-excites from its own noise floor at −90 dBFS. Patch anything into
**FM/EXCITER** and you drive it instead, which is where most of the sound design
lives.

The engine has been ported to the Alloy Platform which is a compact and powerful platform for Eurorack modules. The module is also published as a VCV Rack plugin under the Alloy Platform.

Alloy Coil's engine is the work of Nick Donaldson (concept and firmware) and
Roey Tsemah (original visual and hardware design) at
[Synthux Academy](https://github.com/Synthux-Academy/Audrey-II), where it powers
their module **Audrey II**. It is used here under MIT with its credits intact.

**Alloy Coil is not Audrey II.** The port was made with the original author's
blessing, and ships under its own name at their request so that support
questions and bug reports reach whoever actually wrote the code in front of you.
Anything about *this* module — the Alloy Platform port, the panel, the firmware,
the VCV build — belongs here. Anything about Audrey II itself belongs
[upstream](https://github.com/Synthux-Academy/Audrey-II).

See `CREDITS.md` and `LICENSE`, and `LICENSING.md` at the root of the
repository.

---

## Specs at a Glance

|         |                                                          |
| ------- | -------------------------------------------------------- |
| Format  | Eurorack, 14 HP                                          |
| Knobs   | 9, three of which carry a SHIFT secondary                |
| Jacks   | 10 — V/OCT, GATE, MIDI, CV 1–4, FM/EXCITER, OUT L, OUT R |
| Buttons | 2 — WARP + SHIFT                                         |
| LEDs    | 7× RGB                                                   |
| Audio   | Stereo 16-bit, 48 kHz, PCM5102A I²S DAC                  |
| MIDI    | USB MIDI + TRS MIDI                                      |
| Echo    | up to 4 s, stereo                                        |

---

## Quick Start

1. **OUT L / OUT R** to your mixer or eurorack module.
2. **FB GAIN** to about halfway. The string starts ringing on its own.
3. **PITCH** to set where it sits.
4. **FB LPF** down to darken it, **BODY** to change its character from a tight
   metallic ping to a longer, hollower tube.
5. Bring **REV MIX** and **ECHO SEND** up to taste.

To play it from a keyboard, connect USB or TRS MIDI and send notes — anything
from **MIDI note 16 to 72** sets the pitch. MIDI note on/off does not stop the string from sustaining, so you can play it like a drone.

---

## The Panel

Three rows of three. The row is the section.

|                          | left          | centre               | right                 |
| ------------------------ | ------------- | -------------------- | --------------------- |
| **top** — the resonator  | **PITCH**     | **BODY**             | **FB GAIN** ⇧ EXCITER |
| **mid** — the echo       | **ECHO TIME** | **ECHO SEND**        | **ECHO FBK**          |
| **low** — space and tone | **REV DECAY** | **REV MIX** ⇧ VOLUME | **FB LPF** ⇧ FB HPF   |

⇧ means hold **SHIFT** and turn that knob.

Read it in columns and the panel teaches itself: the **left** column is time
(pitch is 1/time, the other two are times), the **centre** column is how much of
that row you get, and the **right** column is feedback — what comes back round.

| Knob          | Range         | What it does                                                                                                             |
| ------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------ |
| **PITCH**     | note 16–72    | Where the string is tuned. Summed with V/OCT.                                                                            |
| **BODY**      | 1–100 ms      | Length of the feedback delay. Short is a tight metallic ping; long is a hollow, tube-like resonance.                     |
| **FB GAIN**   | −30…+12 dB    | **The main control.** Below about −15 dB the string decays; around 0 dB it sustains; above that it builds and saturates. |
| ⇧ **EXCITER** | 0–2×          | How hard the FM/EXCITER jack drives the string.                                                                          |
| **ECHO SEND** | 0–1           | How much goes into the echo. Tapped after the reverb.                                                                    |
| **ECHO TIME** | 0.05–4 s      | Delay time.                                                                                                              |
| **ECHO FBK**  | 0–1.2         | Echo repeats. **Goes past unity on purpose** — above ~0.83 of travel it builds instead of decaying.                      |
| **REV DECAY** | 0.2–1.0       | Reverb tail, from a short room to effectively frozen at the top.                                                         |
| **REV MIX**   | 0–1           | Dry/wet.                                                                                                                 |
| ⇧ **VOLUME**  | 0–1           | Output level. Same knob position Alloy Flux puts volume on.                                                              |
| **FB LPF**    | 100 Hz–18 kHz | Low-pass _inside_ the feedback loop. Sweeping it darkens the ring as it circulates.                                      |
| ⇧ **FB HPF**  | 10 Hz–4 kHz   | High-pass in the same loop. Together with LPF this is the loop's bandwidth.                                              |

### Buttons

**SHIFT** — hold to reach the secondaries above.

**WARP** — hold to halve the echo time. Because the echo line
is already full when you press it, the read head is dragged toward the write
head and everything in the delay is re-read faster: the whole tail pitches up
over about half a second, settles, then falls back when you let go. It is the
module's one real performance gesture, and it is at its best with the echo
feedback high and the send up.

This is the same control as the toggle switch on the original Synthux Audrey II
panel, made momentary so it can be played rhythmically rather than set.

### LEDs

|                 | Meaning                                                                                                                          |
| --------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| **top pair**    | Output level, left and right                                                                                                     |
| **side pair**   | **Loop danger** — green while the string decays, amber as it approaches unity, red once it is building. Breathes above unity.    |
| **lower left**  | Echo. Flashes once per repeat, so you can _see_ the delay time while setting it — and the flash rate doubles while WARP is held. |
| **lower right** | Reverb amount. Turns white while SHIFT is held.                                                                                  |
| **centre**      | The string is alive; brightens with the exciter input.                                                                           |

The side pair is the one to watch. A slowly building drone sounds like a drone
right up until it isn't, and that pair goes red before you can hear it.

---

## Jacks

Top row, left to right: **V/OCT**, **GATE**, **MIDI IN**, **CV 1**, **CV 2**.
Bottom row: **EXC IN**, **CV 3**, **CV 4**, **OUT L**, **OUT R**.

| Jack           | Range     | Controls                                             |
| -------------- | --------- | ---------------------------------------------------- |
| **V/OCT**      | −3…+7 V   | Pitch, 1 V/oct, summed with the PITCH knob           |
| **GATE**       | −0.8…+8 V | _Unused_ — reserved for a future VCA/envelope option |
| **MIDI IN**    | TRS       | MIDI, type A                                         |
| **CV 1**       | ±5 V      | Body                                                 |
| **CV 2**       | ±5 V      | Feedback gain                                        |
| **CV 3**       | ±5 V      | Echo send                                            |
| **CV 4**       | ±5 V      | Reverb mix                                           |
| **FM/EXCITER** | ±8 V      | Audio into the resonator                             |
| **OUT L / R**  |           | Stereo out                                           |

The four CV inputs add to whatever the knob is set to, full range at ±5 V.
Feedback gain gets one because sweeping it toward unity _is_ the instrument;
the other three go to body and the two wet/dry amounts. Echo feedback and reverb
decay are knob-only — they are the two controls that can run away, and there are
only four jacks. Both are still reachable over MIDI (CC 87 and CC 92).

**FM/EXCITER** takes audio, not control voltage, and it is injected ahead of the
string and then recirculated — so whatever you patch there gets comb-filtered by
the resonator and re-waveshaped on every pass. A noise source gives you a bowed,
breathy attack; an oscillator at a harmonic ratio adds overtones the string then
rings on; percussion gives you something to play. Use ⇧ **EXCITER** to set how
hard it hits. The jack takes ±8 V, hotter than the others, so it can accept a
raw modular-level signal without clipping the front end.

---

## MIDI

Appears as **Alloy Coil MIDI** over USB. TRS MIDI (type A) works at the same
time. Every parameter is mirrored to a CC in real time in both directions, so a
controller follows the knobs and vice versa.

### Notes

**Note On** sets the string pitch — notes 16–72, outside that range ignored.
**Note Off does nothing**, deliberately: releasing a key must not stop a drone
that is sustaining on its own feedback.

### Control Changes

| CC      | Parameter                                                                                            | Range                 |
| ------- | ---------------------------------------------------------------------------------------------------- | --------------------- |
| 16      | String Pitch                                                                                         | note 16–72            |
| 17      | Feedback Gain                                                                                        | −30…+12 dB            |
| 18      | Body                                                                                                 | 1–100 ms _(log)_      |
| 19      | Exciter Level                                                                                        | 0–2×                  |
| 74      | Feedback LPF                                                                                         | 100 Hz–18 kHz _(log)_ |
| 75      | Feedback HPF                                                                                         | 10 Hz–4 kHz _(log)_   |
| 86      | Echo Time                                                                                            | 0.05–4 s _(log)_      |
| 87      | Echo Feedback                                                                                        | 0–1.2                 |
| 91      | Reverb Mix                                                                                           | 0–1                   |
| 92      | Reverb Decay                                                                                         | 0.2–1.0               |
| 93      | Echo Send                                                                                            | 0–1                   |
| 7       | Volume                                                                                               | 0–1                   |
| **123** | **Panic** — collapses feedback gain and echo feedback to zero. The one thing here that can run away. |                       |

CC numbers line up with Alloy Flux wherever the meaning matches (7 volume,
91 reverb mix, 93 echo send, 74/75 filters), so a generic controller feels
familiar across both modules.

Several controls are tapered rather than linear so the useful range is spread
across the travel — reverb decay, volume, echo send and exciter level in
particular. The knob and the CC always land on the same value.

### SysEx and presets

Device signature **`'A' 'U'`**. Full patch dump and restore, plus **nine preset
slots** addressed by SysEx. A tenth slot auto-saves at startup, so the module
comes back the way you left it.

The easiest way in is the **Web Configurator** — connect over USB, no drivers
and no app to install. It reads the device signature and loads Alloy Coil's
parameter map automatically.

---

## A Few Things Worth Knowing

**It is meant to be loud and it is meant to build.** Echo feedback goes past
unity on purpose and so does the resonator. That is the instrument, not a fault.
The output has a soft clipper on it so the DAC never hard-clips, and CC 123 is
there when a patch gets away from you.

**FB LPF and FB HPF are inside the loop**, not on the output. They set the band
the feedback is allowed to live in, which is why sweeping the LPF sounds like
the sound decaying rather than a filter being closed.

**The echo runs at a quarter rate** and is stored as 16-bit, which is how 4
seconds of stereo delay fits on the chip. Measured cost: 0.1 dB of
signal-to-noise. There is a 24 dB/oct filter ahead of it doing the work.

---

## Current Firmware Status

Honest state of things, since this is a module under construction:

- **Panel controls are not wired yet.** The multiplexed ADC driver does not
  exist, so on hardware the knobs, CV jacks, buttons and LEDs read as inactive.
  Everything above is live in the VCV Rack build, and reachable on hardware over
  MIDI, SysEx and the Web Configurator.
- **FM/EXCITER is control-rate on hardware.** The firmware reads that jack at
  128 Hz, so it can poke and swell the string but cannot yet excite it with
  audio. In VCV Rack it is read every sample and is a true audio input. The jack
  is on a dedicated audio-rate ADC pin, so this is a firmware job, not a board
  change.
- **GATE is unassigned**, reserved for a VCA/envelope option.
