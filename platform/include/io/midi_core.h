#pragma once

#include <stdint.h>

/**
 * midi_core — the MIDI protocol with no transport in it (Milestone 78a).
 *
 * Everything here used to live inside `usb_midi.cpp`, mixed in with TinyUSB.
 * None of it is actually USB's: the Alloy SysEx protocol, the parameter
 * dispatch and the CC feedback diff are the same work on any wire.  Splitting
 * them out is what lets BLE MIDI (M78b) be a second transport rather than a
 * second copy of the protocol.
 *
 * A transport supplies a `MidiPort` — three function pointers and a name —
 * registers it once, and then only has to do two things: turn inbound bytes
 * into calls on the `midiCore_handle*` functions, and implement the sends.
 *
 * Every port is optional and every port is independent.  A module with no
 * radio registers one port; a Pico 2W with a phone attached registers two.
 * Nothing in the protocol, the preset format or the audio path changes either
 * way — that is the whole point.
 */

/**
 * One MIDI wire.  Transports declare these at file scope; `midi_core` owns
 * the `next`/`lastSent` members and nothing else may touch them.
 */
struct MidiPort
{
    MidiPort(void (*cc)(uint8_t, uint8_t, uint8_t),
             void (*sx)(const uint8_t *, uint16_t),
             bool (*ready)(),
             const char *portName)
    : sendCC(cc),
      sendSysEx(sx),
      isReady(ready),
      name(portName),
      next(nullptr),
      lastSentInit(false)
    {
    }

    /** Send one Control Change on `channel` (1–16). */
    void (*sendCC)(uint8_t cc, uint8_t value, uint8_t channel);

    /** Send one SysEx message.  `body` carries neither F0 nor F7 — adding
     *  them, and any fragmenting the wire needs, is the transport's job. */
    void (*sendSysEx)(const uint8_t *body, uint16_t len);

    /** True when a host is actually attached.  nullptr means "always". */
    bool (*isReady)();

    /** Short name, for the serial console. */
    const char *name;

    // --- owned by midi_core ------------------------------------------------

    MidiPort *next;

    /**
     * Last value emitted to *this* host, per CC number; 0xFF = never sent, so
     * the first feedback pass emits a full snapshot.
     *
     * Deliberately per-port rather than one shared cache.  The cache exists
     * for echo suppression — "the host that sent this already knows the
     * value", which matters because a 7-bit round-trip on a log parameter can
     * land a step away and visibly nudge the sender's control.  That is true
     * of the port the CC arrived on and false of every other one, so a knob
     * moved from the Controller over USB still reaches a phone on BLE.
     * 128 bytes per port is not worth being clever about.
     */
    bool    lastSentInit;
    uint8_t lastSent[128];
};

/** Add a port.  Call once per transport, from its own init(). */
void midiCore_registerPort(MidiPort *port);

// --- Inbound: transports call these ----------------------------------------

/** Note On.  Velocity 0 is routed to noteOff per the running-status
 *  convention, so no module has to know about that. */
void midiCore_handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);

/** Note Off. */
void midiCore_handleNoteOff(uint8_t channel, uint8_t note);

/** Control Change.  `from` is the port it arrived on — used only to suppress
 *  the echo back at that one host.  May be nullptr for a synthetic CC. */
void midiCore_handleControlChange(MidiPort *from,
                                  uint8_t   channel,
                                  uint8_t   cc,
                                  uint8_t   value);

/** Program Change. */
void midiCore_handleProgramChange(uint8_t channel, uint8_t program);

/** A complete SysEx message.  Leading F0 and trailing F7 are tolerated but
 *  not required, so a transport may hand over whatever shape it reassembled.
 *  Replies (a PATCH_DUMP answering REQUEST_DUMP) go back to `from`. */
void midiCore_handleSysEx(MidiPort *from, const uint8_t *data, uint16_t length);

// --- Outbound ---------------------------------------------------------------

/** Transmit a full PATCH_DUMP.  `to` = nullptr sends it to every ready port.
 *  Exposed so a module can push state after something the platform cannot
 *  see has changed it. */
void midiCore_sendPatchDump(MidiPort *to);

/** Emit a CC for every parameter that changed since this port last heard
 *  about it.  Builds one snapshot and diffs it against each port's cache, so
 *  a quiet patch costs a comparison per parameter and nothing on the wire. */
void midiCore_sendFeedback();

/**
 * MIDI receive channel: 0 = omni (accept all), 1–16 = that channel only.
 *
 * Platform-owned rather than module-owned — it is a property of the
 * transport, every module needs exactly the same behaviour from it, and the
 * SysEx command that sets it lives here.  Modules read it only to persist it.
 */
extern uint8_t gMidiChannel;
