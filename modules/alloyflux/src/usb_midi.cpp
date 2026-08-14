#ifdef USE_TINYUSB

#include "io/usb_midi.h"
#include "SynthEngine.h"
#include "config_store.h"
#include "dsp/ChorusEngine.h"
#include "dsp/ReverbEngine.h"
#include "io/param_map.h"
#include "params.h"
#include "scale_quantizer.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <math.h>

// Scale quantizer globals (M49)
ScaleId gQuantizeScale = ScaleId::CHROMATIC;
int8_t  gTranspose     = 0;

// ---------------------------------------------------------------------------
// SysEx patch dump — AlloyFlux protocol
//
// Format (body between F0 and F7):
//   7D 41 46 <cmd> [cc0 val0 cc1 val1 ...]
//   7D = non-commercial manufacturer ID
//   41 46 = 'A' 'F' (AlloyFlux device signature)
//   cmd:
//     01 = REQUEST_DUMP  (host → device: request full patch dump)
//     02 = PATCH_DUMP    (device → host: full patch as CC pairs)
//     03 = APPLY_PATCH   (host → device: load CC pairs into parameters)
//
// All CC and value bytes are 7-bit safe (0–127).
// ---------------------------------------------------------------------------

static constexpr uint8_t kSysExMfr            = 0x7D; // non-commercial
static constexpr uint8_t kSysExDevA           = 'A';
static constexpr uint8_t kSysExDevF           = 'F';
static constexpr uint8_t kSysExCmdRequestDump = 0x01;
static constexpr uint8_t kSysExCmdPatchDump   = 0x02;
static constexpr uint8_t kSysExCmdApplyPatch  = 0x03;
static constexpr uint8_t kSysExCmdPresetSave  = 0x04; // payload[0] = slot 0-9
static constexpr uint8_t kSysExCmdPresetLoad
    = 0x05; // payload[0] = slot 0-9; responds with PATCH_DUMP
static constexpr uint8_t kSysExCmdPresetReset
    = 0x06; // payload[0] = slot 0-9, or 0x7F = all
static constexpr uint8_t kSysExCmdSetMidiChannel
    = 0x07; // payload[0] = 0 (omni) or 1-16

// Upper bound on the (cc, value) pairs a patch dump can carry. The tables are
// sized from params.json, so this must have room to spare — a dump that
// overruns its buffer would corrupt whatever follows rather than fail visibly.
// sBuildPatchPairs() stops at this limit regardless.
static constexpr uint8_t kMaxPatchPairs = 56;
static constexpr uint8_t kMaxPatchBytes = kMaxPatchPairs * 2;

// Fill buf[] with (cc, value) pairs for all patchable parameters.
// Returns the total number of bytes written (always even).
static uint8_t sBuildPatchPairs(uint8_t *buf)
{
    uint8_t n = 0;
    // Drops a pair rather than overrunning. Nothing should ever reach the
    // limit — kMaxPatchPairs has headroom over both generated tables — but the
    // tables grow whenever params.json does, and an overrun would corrupt
    // whatever follows instead of failing visibly.
    auto add = [&](uint8_t cc, uint8_t value)
    {
        if(n + 2 > kMaxPatchBytes)
            return;
        buf[n++] = cc;
        buf[n++] = value;
    };

    // Continuous params.
    for(uint8_t i = 0; i < kParamCount; i++)
        add(kParamTable[i].cc, kParamTable[i].toCC(*kParamTable[i].target));

    // Discrete params. toCC() returns the low edge of the option's band, which
    // the configurator resolves the same way as any other value in that band.
    for(uint8_t i = 0; i < kEnumCount; i++)
        add(kEnumTable[i].cc, kEnumTable[i].toCC(*kEnumTable[i].target));

    // Not table parameters.
    add(114, gRevFrozen ? 127 : 0); // reverb freeze
    // Transpose: 0–48 encodes −24…+24 semitones (offset 24).
    add(104, (uint8_t)constrain((int)gTranspose + 24, 0, 48));
    add(110, gMidiChannel); // MIDI receive channel, 0 = omni
    return n;
}

// ---------------------------------------------------------------------------
// Outbound CC cache — last value emitted to the host per CC number.
// 0xFF = never sent, so the first feedback pass emits a full snapshot.
// Also seeded from inbound host CCs (sNoteHostCC) so a value the host set is
// not immediately echoed back at it.
// ---------------------------------------------------------------------------
static uint8_t sLastSentCC[128];
static bool    sLastSentCCInit = false;

static void sResetLastSentCC()
{
    memset(sLastSentCC, 0xFF, sizeof(sLastSentCC));
    sLastSentCCInit = true;
}

static void sNoteHostCC(uint8_t cc, uint8_t value)
{
    if(!sLastSentCCInit)
        sResetLastSentCC();
    sLastSentCC[cc & 0x7F] = value & 0x7F;
}

// ---------------------------------------------------------------------------
// USB MIDI transport + MIDI interface
// ---------------------------------------------------------------------------

static Adafruit_USBD_MIDI sUsbMidiTransport;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, sUsbMidiTransport, MidiUsb);

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

uint8_t sActiveNote
    = 255; // 255 = no note currently held (exported via params.h)

// Returns true if an incoming message on `ch` should be processed.
// gMidiChannel == 0 means omni (accept all); otherwise match exactly.
static inline bool channelMatches(uint8_t ch)
{ return gMidiChannel == 0 || ch == gMidiChannel; }

// Convert MIDI note number (0–127) to frequency in Hz.
// Standard equal-temperament: A4 (note 69) = 440 Hz.
static inline float midiNoteToHz(uint8_t note)
{ return 440.0f * powf(2.0f, ((int8_t)note - 69) / 12.0f); }

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

static void onNoteOn(byte channel, byte note, byte velocity)
{
    if(!channelMatches(channel))
        return;
    if(velocity == 0)
    {
        // NoteOn with velocity 0 is a NoteOff (running-status MIDI convention).
        if(gVoiceMode == VoiceMode::POLY)
        {
            for(uint8_t i = 0; i < 6; i++)
            {
                if(sPolySlots[i].midiNote == note)
                {
                    sPolyEnvs[i]->setGate(false);
                    sPolySlots[i].midiNote = 255;
                }
            }
        }
        else if(sActiveNote == note)
        {
            gGateHigh = false;
            gCurveEng->setGate(false);
            sActiveNote = 255;
        }
        return;
    }

    if(gVoiceMode == VoiceMode::POLY)
    {
        // POLY voice allocation: search for a free slot starting at sPolyRR so
        // voices are assigned in rotation (same as CV/gate path).  If all busy,
        // steal the round-robin next slot.
        uint8_t slot = 255;
        for(uint8_t i = 0; i < 6; i++)
        {
            uint8_t idx = (sPolyRR + i) % 6;
            if(sPolySlots[idx].midiNote == 255)
            {
                slot = idx;
                break;
            }
        }
        if(slot == 255)
        {
            // All slots occupied — steal round-robin
            slot = sPolyRR % 6;
        }
        sPolyRR          = (sPolyRR + 1) % 6;
        const float freq = constrain(
            midiNoteToHz(quantizeNote(note, gQuantizeScale, gTranspose)),
            20.0f,
            8000.0f);
        sPolySlots[slot].freq = freq;
        sPolySlots[slot].velocity
            = gVelocitySensitive ? (velocity / 127.0f) : 1.0f;
        sPolySlots[slot].midiNote = note;
        // subMult depends on voice sub-octave param — use 0.5 (default, -1 oct)
        // as a safe approximation; control() will correct the sub freq next tick.
        const float subMult = 0.5f;
        gSynthEngine.polyRetrigger(slot, freq, subMult);
        return;
    }

    sActiveNote = note;
    gBaseFreq   = constrain(
        midiNoteToHz(quantizeNote(note, gQuantizeScale, gTranspose)),
        20.0f,
        8000.0f);
    gMidiVelocity = gVelocitySensitive ? (velocity / 127.0f) : 1.0f;
    gGatePatched  = true; // arm envelope — MIDI is now the gate source
    gGateHigh     = true;
    gCurveEng->setGate(true); // arm attack immediately — closes ISR window
}

static void onNoteOff(byte channel, byte note, byte /*velocity*/)
{
    if(!channelMatches(channel))
        return;
    if(gVoiceMode == VoiceMode::POLY)
    {
        for(uint8_t i = 0; i < 6; i++)
        {
            if(sPolySlots[i].midiNote == note)
            {
                sPolyEnvs[i]->setGate(false);
                sPolySlots[i].midiNote = 255;
            }
        }
        return;
    }
    // Monophonic last-note priority: only release if this is the active note.
    if(sActiveNote == note)
    {
        gGateHigh = false;
        gCurveEng->setGate(
            false); // arm release immediately — closes ISR window
        sActiveNote = 255;
    }
}

static void onControlChange(byte channel, byte cc, byte value)
{
    if(!channelMatches(channel))
        return;
    // Echo suppression: the host already knows the value it just sent us, so
    // record it as if we had emitted it.  Without this the next feedback tick
    // sends it straight back, and on log/wide-range params the 7-bit
    // round-trip can land a step off and visibly nudge the host's slider.
    sNoteHostCC(cc, value);
    // Continuous parameters — delegated to the central CC map (param_map.cpp).
    if(paramMap_dispatchCC(cc, value))
        return;

    // Special cases: not simple float parameters.
    switch(cc)
    {
        case 64: // Sustain pedal — arms gate; release only on pedal-up (value < 64)
            gGatePatched = true;
            gGateHigh    = (value >= 64);
            break;
        case 104: // Transpose (M49) — 0–48 encodes −24…+24 semitones
            gTranspose = (int8_t)constrain((int)value - 24, -24, 24);
            break;
        case 114: // Reverb freeze — M41: ≥64 = freeze on, <64 = freeze off
            gRevFrozen = (value >= 64);
            break;
        case 119: // Drone return — clears gGatePatched, module returns to continuous drone
            gGatePatched  = false;
            gGateHigh     = false;
            gMidiVelocity = 1.0f; // restore full volume on return to drone/CV
            sActiveNote   = 255;
            break;
        case 123: // All Notes Off / panic
            gGateHigh   = false;
            sActiveNote = 255;
            for(uint8_t i = 0; i < 6; i++)
            {
                if(sPolyEnvs[i])
                {
                    sPolyEnvs[i]->setGate(false);
                    sPolyEnvs[i]->reset();
                }
                sPolySlots[i].midiNote = 255;
            }
            sPolyRR = 0;
            break;
        default: break;
    }
}

static void onProgramChange(byte channel, byte program)
{
    if(!channelMatches(channel))
        return;
    // Programs 1–6 map to VoiceMode PAIR/CLOUD/CHORD/CASCADE/STRING/POLY.
    if(program >= 1 && program <= 6)
        gVoiceMode = static_cast<VoiceMode>(program - 1);
}

// Build and transmit a PATCH_DUMP SysEx response.
// Must be placed after MIDI_CREATE_INSTANCE since it calls MidiUsb.sendSysEx.
static void sSendPatchDump()
{
    // 4-byte header plus whatever sBuildPatchPairs() can emit. Sized from
    // kMaxPatchBytes rather than a hand-counted total so it cannot fall behind
    // params.json.
    static uint8_t sBuf[4 + kMaxPatchBytes];
    sBuf[0]                 = kSysExMfr;
    sBuf[1]                 = kSysExDevA;
    sBuf[2]                 = kSysExDevF;
    sBuf[3]                 = kSysExCmdPatchDump;
    const uint8_t pairBytes = sBuildPatchPairs(sBuf + 4);
    MidiUsb.sendSysEx(4 + pairBytes, sBuf, false); // library adds F0/F7
    // The host now has every value; seed the outbound cache from the same
    // snapshot so the next feedback tick doesn't repeat the whole dump as
    // individual CCs (a preset load would otherwise emit ~30 of them).
    for(uint8_t i = 0; i + 1 < pairBytes; i += 2)
        sNoteHostCC(sBuf[4 + i], sBuf[4 + i + 1]);
}

// SysEx handler — AlloyFlux patch dump protocol.
// The Arduino MIDI Library v5 passes data[] with F0 at [0] and F7 at [length-1].
// We skip boundaries so the body always starts at [1] and ends before F7.
static void onSysEx(uint8_t *data, unsigned int length)
{
    // Skip leading F0 if the library includes it
    uint8_t     *body    = data;
    unsigned int bodyLen = length;
    if(bodyLen > 0 && body[0] == 0xF0)
    {
        body++;
        bodyLen--;
    }
    if(bodyLen > 0 && body[bodyLen - 1] == 0xF7)
    {
        bodyLen--;
    }

    // Validate 3-byte header: 7D 41('A') 46('F') <cmd>
    if(bodyLen < 4)
        return;
    if(body[0] != kSysExMfr || body[1] != kSysExDevA || body[2] != kSysExDevF)
        return;

    const uint8_t cmd  = body[3];
    const uint8_t arg0 = (bodyLen > 4) ? (body[4] & 0x7F) : 0;

    if(cmd == kSysExCmdRequestDump)
    {
        sSendPatchDump();
    }
    else if(cmd == kSysExCmdApplyPatch)
    {
        // Payload: interleaved (cc, value) pairs starting at body[4]
        for(unsigned int i = 4; i + 1 < bodyLen; i += 2)
        {
            const uint8_t cc  = body[i] & 0x7F;
            const uint8_t val = body[i + 1] & 0x7F;
            onControlChange(1, cc, val); // reuse existing dispatch
        }
    }
    else if(cmd == kSysExCmdPresetSave)
    {
        configStore_save(arg0);
    }
    else if(cmd == kSysExCmdPresetLoad)
    {
        configStore_load(arg0);
        sSendPatchDump(); // auto-refresh web UI after load
    }
    else if(cmd == kSysExCmdPresetReset)
    {
        const uint8_t fwSlot = (arg0 == 0x7F) ? 255 : arg0;
        configStore_reset(fwSlot);
        // For live slot (0) or full reset, apply defaults immediately and
        // respond with a dump so the web UI syncs without a page reload.
        if(arg0 == 0 || arg0 == 0x7F)
        {
            configStore_applyDefaults();
            sSendPatchDump();
        }
    }
    else if(cmd == kSysExCmdSetMidiChannel)
    {
        const uint8_t ch = arg0 & 0x7F;
        // Accept 0 (omni) or 1-16; ignore invalid values silently.
        if(ch <= 16)
        {
            gMidiChannel = ch;
            configStore_save(
                0); // persist; rate-limit may throttle but that's fine
        }
        sSendPatchDump(); // echo back so UI confirms the new value
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void usbMidi_init()
{
    TinyUSBDevice.setManufacturerDescriptor("Voltage Foundry Modular");
    TinyUSBDevice.setProductDescriptor("Alloy Flux");
    sUsbMidiTransport.setStringDescriptor("AlloyFlux MIDI");
    MidiUsb.begin(MIDI_CHANNEL_OMNI);
    MidiUsb.setHandleNoteOn(onNoteOn);
    MidiUsb.setHandleNoteOff(onNoteOff);
    MidiUsb.setHandleControlChange(onControlChange);
    MidiUsb.setHandleProgramChange(onProgramChange);
    MidiUsb.setHandleSystemExclusive(onSysEx);
    MidiUsb.turnThruOff(); // no MIDI echo back to host

    // Wait for the USB device to fully enumerate with both CDC + MIDI interfaces.
    // Timeout after 2 s so the module boots standalone without USB host.
    const uint32_t t0 = millis();
    while(!TinyUSBDevice.mounted() && (millis() - t0) < 2000)
    {
        delay(1);
    }
}

// Drain the inbound queue, don't sip from it.
//
// MidiUsb.read() dispatches exactly one message per call. Called once per
// control tick that caps inbound throughput at the control rate — 128
// messages/second — which is below what a dragged slider in the Web
// Configurator emits. The excess sits in the USB FIFO and plays out at 128/s,
// so the lag grows for as long as you keep dragging and continues after you
// let go.
//
// The bound exists so a misbehaving or malicious sender cannot hold the control
// tick indefinitely. 64 messages is ~8000/s of headroom, far more than any real
// controller produces, and costs well under 100 µs against a 7.8 ms tick.
static constexpr uint8_t kMaxMidiPerTick = 64;

void usbMidi_update()
{
    for(uint8_t i = 0; i < kMaxMidiPerTick && MidiUsb.read(); i++) {}
}

// ---------------------------------------------------------------------------
// CC feedback — emit changed parameters to the USB host at control rate.
// Builds a full CC snapshot, diffs against the previous one, and sends
// individual CC messages only for values that have changed.
// Cost: ~25 comparisons + only the changed CCs over the wire (0–3 typical).
// ---------------------------------------------------------------------------
void usbMidi_sendFeedback()
{
    if(!sLastSentCCInit)
        sResetLastSentCC();

    // Build full snapshot into a temp buffer, then diff and send.
    static uint8_t buf[kMaxPatchBytes];
    uint8_t        n = sBuildPatchPairs(buf);
    for(uint8_t i = 0; i + 1 < n; i += 2)
    {
        uint8_t cc  = buf[i] & 0x7F;
        uint8_t val = buf[i + 1] & 0x7F;
        if(sLastSentCC[cc] != val)
        {
            sLastSentCC[cc] = val;
            MidiUsb.sendControlChange(
                cc, val, gMidiChannel == 0 ? 1 : gMidiChannel);
        }
    }
}

#endif // USE_TINYUSB
