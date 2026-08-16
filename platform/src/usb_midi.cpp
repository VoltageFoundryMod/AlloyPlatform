#ifdef USE_TINYUSB

#include "io/usb_midi.h"
#include "ModuleHooks.h"
#include "config_store.h"
#include "io/param_map.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <string.h>

// ---------------------------------------------------------------------------
// SysEx patch dump — Alloy platform protocol
//
// Format (body between F0 and F7):
//   7D <id0> <id1> <cmd> [cc0 val0 cc1 val1 ...]
//   7D          = non-commercial manufacturer ID
//   <id0><id1>  = the module's device signature (AlloyFlux: 'A' 'F'),
//                 supplied by ModuleHooks so the Web Configurator can tell
//                 which parameter map to load
//   cmd:
//     01 = REQUEST_DUMP  (host → device: request full patch dump)
//     02 = PATCH_DUMP    (device → host: full patch as CC pairs)
//     03 = APPLY_PATCH   (host → device: load CC pairs into parameters)
//
// All CC and value bytes are 7-bit safe (0–127).
// ---------------------------------------------------------------------------

static constexpr uint8_t kSysExMfr            = 0x7D; // non-commercial
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

uint8_t gMidiChannel = 0; // 0 = omni

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

    // MIDI channel is the platform's own, not a module parameter.
    add(110, gMidiChannel);

    // Anything the module encodes outside the tables.
    if(n < kMaxPatchBytes)
        n += moduleHook_extraPatchPairs(buf + n, (uint8_t)(kMaxPatchBytes - n));

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

// Returns true if an incoming message on `ch` should be processed.
// gMidiChannel == 0 means omni (accept all); otherwise match exactly.
static inline bool channelMatches(uint8_t ch)
{ return gMidiChannel == 0 || ch == gMidiChannel; }

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

static void onNoteOn(byte channel, byte note, byte velocity)
{
    if(!channelMatches(channel))
        return;
    // NoteOn with velocity 0 is a NoteOff (running-status MIDI convention).
    // Normalised here so no module has to know that.
    if(velocity == 0)
        moduleHook_noteOff(note);
    else
        moduleHook_noteOn(note, velocity);
}

static void onNoteOff(byte channel, byte note, byte /*velocity*/)
{
    if(!channelMatches(channel))
        return;
    moduleHook_noteOff(note);
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

    // Continuous and discrete parameters — the generated manifest owns these.
    if(paramMap_dispatchCC(cc, value))
        return;

    // MIDI channel select is the platform's.
    if(cc == 110)
    {
        if(value <= 16)
            gMidiChannel = value;
        return;
    }

    // Everything else is an action rather than a parameter, and belongs to the
    // module. It cannot shadow a manifest CC — dispatchCC() already declined.
    moduleHook_controlChange(cc, value);
}

static void onProgramChange(byte channel, byte program)
{
    if(!channelMatches(channel))
        return;
    moduleHook_programChange(program);
}

// Build and transmit a PATCH_DUMP SysEx response.
// Must be placed after MIDI_CREATE_INSTANCE since it calls MidiUsb.sendSysEx.
void usbMidi_sendPatchDump()
{
    // 4-byte header plus whatever sBuildPatchPairs() can emit. Sized from
    // kMaxPatchBytes rather than a hand-counted total so it cannot fall behind
    // params.json.
    static uint8_t sBuf[4 + kMaxPatchBytes];
    sBuf[0]                 = kSysExMfr;
    sBuf[1]                 = kSysExDevId0;
    sBuf[2]                 = kSysExDevId1;
    sBuf[3]                 = kSysExCmdPatchDump;
    const uint8_t pairBytes = sBuildPatchPairs(sBuf + 4);
    MidiUsb.sendSysEx(4 + pairBytes, sBuf, false); // library adds F0/F7
    // The host now has every value; seed the outbound cache from the same
    // snapshot so the next feedback tick doesn't repeat the whole dump as
    // individual CCs (a preset load would otherwise emit ~30 of them).
    for(uint8_t i = 0; i + 1 < pairBytes; i += 2)
        sNoteHostCC(sBuf[4 + i], sBuf[4 + i + 1]);
}

// SysEx handler — Alloy platform patch dump protocol.
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

    // Validate 3-byte header: 7D <id0> <id1> <cmd>
    if(bodyLen < 4)
        return;
    if(body[0] != kSysExMfr || body[1] != kSysExDevId0
       || body[2] != kSysExDevId1)
        return;

    const uint8_t cmd  = body[3];
    const uint8_t arg0 = (bodyLen > 4) ? (body[4] & 0x7F) : 0;

    if(cmd == kSysExCmdRequestDump)
    {
        usbMidi_sendPatchDump();
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
        usbMidi_sendPatchDump(); // auto-refresh web UI after load
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
            usbMidi_sendPatchDump();
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
        usbMidi_sendPatchDump(); // echo back so UI confirms the new value
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void usbMidi_init()
{
    TinyUSBDevice.setManufacturerDescriptor(kModuleManufacturer);
    TinyUSBDevice.setProductDescriptor(kModuleProduct);
    sUsbMidiTransport.setStringDescriptor(kModuleMidiName);
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
