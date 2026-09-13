#include "io/midi_core.h"
#include "ModuleHooks.h"
#include "config_store.h"
#include "io/param_map.h"

#include <string.h>

// ---------------------------------------------------------------------------
// SysEx patch dump — Alloy platform protocol
//
// Format (body between F0 and F7):
//   7D <id0> <id1> <cmd> [cc0 val0 cc1 val1 ...]
//   7D          = non-commercial manufacturer ID
//   <id0><id1>  = the module's device signature (AlloyFlux: 'A' 'F'),
//                 supplied by ModuleHooks so the Alloy Controller can tell
//                 which parameter map to load
//   cmd:
//     01 = REQUEST_DUMP  (host -> device: request full patch dump)
//     02 = PATCH_DUMP    (device -> host: full patch as CC pairs)
//     03 = APPLY_PATCH   (host -> device: load CC pairs into parameters)
//
// All CC and value bytes are 7-bit safe (0-127).
//
// Discovery: 7F 7F in place of the signature is a wildcard meaning "any Alloy
// module".  A host that does not yet know what it is talking to — the Alloy
// Controller on connect — sends a broadcast REQUEST_DUMP, and the module
// answers with an ordinary PATCH_DUMP carrying its *real* signature.  That one
// round trip identifies the module and delivers its patch, so no separate
// identity command is needed.  Only REQUEST_DUMP is honoured on the wildcard:
// every module sharing the port sees a broadcast, so anything that changes
// state has to be addressed to one of them.
//
// M78a: none of this is USB's, so none of it lives in usb_midi.cpp any more.
// The wildcard reasoning now covers a second case it was not written for —
// several *transports* into one module, rather than several modules on one
// port — and needs no change for it: a broadcast asks a question, and the
// answer goes back to whichever port asked.
// ---------------------------------------------------------------------------

static constexpr uint8_t kSysExMfr            = 0x7D; // non-commercial
static constexpr uint8_t kSysExDevAny         = 0x7F; // wildcard signature byte
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

// ---------------------------------------------------------------------------
// Registered ports — an intrusive list, so registering allocates nothing and
// the count is bounded by how many transports were compiled in (one or two).
// ---------------------------------------------------------------------------

static MidiPort *sPorts = nullptr;

void midiCore_registerPort(MidiPort *port)
{
    if(!port)
        return;
    // Re-registering would loop the list onto itself.
    for(MidiPort *p = sPorts; p; p = p->next)
    {
        if(p == port)
            return;
    }
    port->next = sPorts;
    sPorts     = port;
}

static inline bool portReady(const MidiPort *p)
{ return p && p->sendCC && (!p->isReady || p->isReady()); }

// ---------------------------------------------------------------------------
// Outbound CC cache, per port. 0xFF = never sent, so the first feedback pass
// emits a full snapshot.  Also seeded from inbound host CCs so a value the
// host set is not immediately echoed back at it.
// ---------------------------------------------------------------------------

static void sResetLastSent(MidiPort *p)
{
    memset(p->lastSent, 0xFF, sizeof(p->lastSent));
    p->lastSentInit = true;
}

static void sNoteHostCC(MidiPort *p, uint8_t cc, uint8_t value)
{
    if(!p)
        return;
    if(!p->lastSentInit)
        sResetLastSent(p);
    p->lastSent[cc & 0x7F] = value & 0x7F;
}

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

// Returns true if an incoming message on `ch` should be processed.
// gMidiChannel == 0 means omni (accept all); otherwise match exactly.
static inline bool channelMatches(uint8_t ch)
{ return gMidiChannel == 0 || ch == gMidiChannel; }

// ---------------------------------------------------------------------------
// Inbound
// ---------------------------------------------------------------------------

void midiCore_handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
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

void midiCore_handleNoteOff(uint8_t channel, uint8_t note)
{
    if(!channelMatches(channel))
        return;
    moduleHook_noteOff(note);
}

void midiCore_handleControlChange(MidiPort *from,
                                  uint8_t   channel,
                                  uint8_t   cc,
                                  uint8_t   value)
{
    if(!channelMatches(channel))
        return;
    // Echo suppression: the host already knows the value it just sent us, so
    // record it as if we had emitted it.  Without this the next feedback tick
    // sends it straight back, and on log/wide-range params the 7-bit
    // round-trip can land a step off and visibly nudge the host's slider.
    //
    // Only on the port it came from — every *other* host still wants to be
    // told, which is what makes two transports usable at once.
    sNoteHostCC(from, cc, value);

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

void midiCore_handleProgramChange(uint8_t channel, uint8_t program)
{
    if(!channelMatches(channel))
        return;
    moduleHook_programChange(program);
}

// ---------------------------------------------------------------------------
// Outbound
// ---------------------------------------------------------------------------

void midiCore_sendPatchDump(MidiPort *to)
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

    for(MidiPort *p = sPorts; p; p = p->next)
    {
        if(to && p != to)
            continue;
        if(!portReady(p) || !p->sendSysEx)
            continue;
        p->sendSysEx(sBuf, (uint16_t)(4 + pairBytes));
        // This host now has every value; seed its cache from the same
        // snapshot so the next feedback tick doesn't repeat the whole dump as
        // individual CCs (a preset load would otherwise emit ~30 of them).
        for(uint8_t i = 0; i + 1 < pairBytes; i += 2)
            sNoteHostCC(p, sBuf[4 + i], sBuf[4 + i + 1]);
    }
}

// ---------------------------------------------------------------------------
// CC feedback — emit changed parameters to every attached host at control
// rate.  Builds one full CC snapshot, diffs it against each port's cache, and
// sends individual CC messages only for values that have changed.
// Cost: ~25 comparisons per port + only the changed CCs over the wire
// (0-3 typical).
// ---------------------------------------------------------------------------
void midiCore_sendFeedback()
{
    if(!sPorts)
        return;

    static uint8_t buf[kMaxPatchBytes];
    uint8_t        n        = 0;
    bool           builtYet = false;

    for(MidiPort *p = sPorts; p; p = p->next)
    {
        if(!portReady(p))
            continue;
        // Built lazily: with nothing attached this whole function is a walk
        // of a one- or two-element list.
        if(!builtYet)
        {
            n        = sBuildPatchPairs(buf);
            builtYet = true;
        }
        if(!p->lastSentInit)
            sResetLastSent(p);

        for(uint8_t i = 0; i + 1 < n; i += 2)
        {
            const uint8_t cc  = buf[i] & 0x7F;
            const uint8_t val = buf[i + 1] & 0x7F;
            if(p->lastSent[cc] != val)
            {
                p->lastSent[cc] = val;
                p->sendCC(cc, val, gMidiChannel == 0 ? 1 : gMidiChannel);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// SysEx — Alloy platform patch dump protocol.
//
// Callers may hand over the body with or without its F0/F7 boundaries: the
// Arduino MIDI Library passes data[] with F0 at [0] and F7 at [length-1],
// while a BLE transport has already stripped them during reassembly.
// ---------------------------------------------------------------------------
void midiCore_handleSysEx(MidiPort *from, const uint8_t *data, uint16_t length)
{
    const uint8_t *body    = data;
    uint16_t       bodyLen = length;
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
    if(body[0] != kSysExMfr)
        return;
    const bool addressed = (body[1] == kSysExDevId0 && body[2] == kSysExDevId1);
    const bool broadcast = (body[1] == kSysExDevAny && body[2] == kSysExDevAny);
    if(!addressed && !broadcast)
        return;

    const uint8_t cmd  = body[3];
    const uint8_t arg0 = (bodyLen > 4) ? (body[4] & 0x7F) : 0;

    // A broadcast reaches every module on the port, so it may only ask a
    // question — never change anything.  REQUEST_DUMP is the discovery probe;
    // the PATCH_DUMP that answers it carries this module's real signature.
    if(broadcast && cmd != kSysExCmdRequestDump)
        return;

    if(cmd == kSysExCmdRequestDump)
    {
        midiCore_sendPatchDump(from);
    }
    else if(cmd == kSysExCmdApplyPatch)
    {
        // Payload: interleaved (cc, value) pairs starting at body[4]
        for(uint16_t i = 4; i + 1 < bodyLen; i += 2)
        {
            const uint8_t cc  = body[i] & 0x7F;
            const uint8_t val = body[i + 1] & 0x7F;
            midiCore_handleControlChange(from, 1, cc, val);
        }
    }
    else if(cmd == kSysExCmdPresetSave)
    {
        configStore_save(arg0);
    }
    else if(cmd == kSysExCmdPresetLoad)
    {
        configStore_load(arg0);
        // Every attached host, not just the asking one: a preset recall
        // changes the whole patch, and a second host left showing the old one
        // would fight the first the next time either moved a control.
        midiCore_sendPatchDump(nullptr);
    }
    else if(cmd == kSysExCmdPresetReset)
    {
        const uint8_t fwSlot = (arg0 == 0x7F) ? 255 : arg0;
        configStore_reset(fwSlot);
        // For live slot (0) or full reset, apply defaults immediately and
        // respond with a dump so the UI syncs without a page reload.
        if(arg0 == 0 || arg0 == 0x7F)
        {
            configStore_applyDefaults();
            midiCore_sendPatchDump(nullptr);
        }
    }
    else if(cmd == kSysExCmdSetMidiChannel)
    {
        const uint8_t ch = arg0 & 0x7F;
        // Accept 0 (omni) or 1-16; ignore invalid values silently.
        if(ch <= 16)
        {
            gMidiChannel = ch;
            // Persist; the rate limit may throttle it, which is fine.
            configStore_save(0);
        }
        // Echo back so the UI confirms the new value — and to every host,
        // since the receive channel is shared by all of them.
        midiCore_sendPatchDump(nullptr);
    }
}
