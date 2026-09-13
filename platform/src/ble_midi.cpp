#include "io/ble_midi.h"

// ---------------------------------------------------------------------------
// Milestone 78b — BLE MIDI.
//
// Compiled to nothing unless the module opted in with -DALLOY_BLE.  That is
// the primary guarantee that wireless is additive: the non-W envs never see a
// line of this file, so a plain Pico 2 image is byte-for-byte what it was.
// The runtime check further down is defence in depth for the one case the
// compile-time gate cannot cover — a 2W image running on a board whose radio
// does not answer.
// ---------------------------------------------------------------------------

#ifndef ALLOY_BLE

// --- Not built.  Every entry point is a no-op. -----------------------------

void bleMidi_init() {}
void bleMidi_update() {}
void bleMidi_startPairing() {}
void bleMidi_stopPairing() {}
void bleMidi_setPolling(bool) {}
bool bleMidi_polling()
{ return false; }
BleMidiState bleMidi_state()
{ return BleMidiState::Unavailable; }
const char *bleMidi_stateName()
{
    // Deliberately distinct from the radio-absent string in the real build.
    // "this image has no BLE in it" and "this image has BLE but found no
    // radio" are indistinguishable from the panel — a long MODE hold just
    // cycles the voice mode either way — and they need completely different
    // fixes. One word in `status` is the cheapest place to separate them.
    return "not-built";
}

#else // ALLOY_BLE

#if !defined(PICO_CYW43_SUPPORTED)
#error \
    "ALLOY_BLE requires a CYW43 board target — build env:alloyflux_w (board = rpipico2w)."
#endif
#if !defined(ENABLE_BLE)
#error \
    "ALLOY_BLE requires -DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH in build_flags."
#endif

#include "ModuleHooks.h"
#include "debug.h"
#include "io/midi_core.h"

#include <Arduino.h>
#include <BLE.h>
#include <BluetoothLock.h>
#include <btstack.h>
#include <cyw43.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Standard BLE MIDI service and its single I/O characteristic. Both UUIDs are
/// fixed by the MIDI Manufacturers Association spec — every BLE MIDI host on
/// every platform looks for exactly these, which is why no driver is needed.
static constexpr const char *kMidiServiceUuid
    = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static constexpr const char *kMidiCharUuid
    = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

/// How long the pairing window stays open after the MODE hold.
///
/// Advertising is gated rather than the radio.  A module that is not
/// advertising cannot be found at all, which is what keeps a rack on a stage
/// from being discoverable by strangers all night — and it costs the player
/// nothing, because there is no PIN to re-enter afterwards.  An already-
/// connected host stays connected when the window closes.
static constexpr uint32_t kAdvertiseMs = 60000;

/// Outbound packet size.  The default ATT MTU is 23, leaving 20 bytes of
/// payload, and there is no portable way to read back what a central
/// negotiated — so every packet we *send* is sized for the floor.  A patch
/// dump is ~88 bytes and therefore spans five packets.  Inbound packets may be
/// larger (see kRxSlotBytes); this bound is ours alone.
static constexpr uint8_t kMaxTxPacket = 20;

/// Inbound slot size.  A central that negotiated a large MTU may write more
/// than 20 bytes in one go; 96 covers anything a keyboard app or the Alloy
/// Controller sends without reserving a kilobyte for the pathological case.
/// An oversized write is dropped whole rather than truncated — a truncated
/// SysEx is worse than a missing one.
static constexpr uint16_t kRxSlotBytes = 96;
static constexpr uint8_t  kRxSlots     = 8;
static constexpr uint8_t  kTxSlots     = 12;

/// Reassembly buffer for a SysEx spanning several packets. A full patch dump
/// is ~88 bytes; 192 leaves room for params.json to grow.
static constexpr uint16_t kSysExMax = 192;

/// Bound on packets parsed per control tick, for the same reason
/// usbMidi_update() bounds its drain: a misbehaving sender must not be able to
/// hold the control tick.
static constexpr uint8_t kMaxPacketsPerTick = 16;


// ---------------------------------------------------------------------------
// Packet rings
//
// Both are written on one side and read on the other, and *every* access is
// made under BluetoothLock — the BT callbacks run in a low-priority IRQ that
// holds the same async-context lock, so taking it is what makes these safe
// without hand-rolled barriers.  Holding it costs microseconds and only ever
// blocks core 0; core 1 and the audio path never touch any of this.
// ---------------------------------------------------------------------------

template <uint8_t Slots, uint16_t Bytes>
struct PacketRing
{
    uint8_t  data[Slots][Bytes];
    uint16_t len[Slots];
    uint8_t  head    = 0; // next slot to write
    uint8_t  tail    = 0; // next slot to read
    uint16_t dropped = 0;

    bool empty() const { return head == tail; }

    bool push(const uint8_t *src, uint16_t n)
    {
        if(n == 0 || n > Bytes)
        {
            dropped++;
            return false;
        }
        const uint8_t next = (uint8_t)((head + 1) % Slots);
        if(next == tail)
        {
            dropped++; // full — drop the newest rather than overwrite
            return false;
        }
        memcpy(data[head], src, n);
        len[head] = n;
        head      = next;
        return true;
    }

    /// Copy the oldest packet out. Returns 0 when empty.
    uint16_t pop(uint8_t *dst, uint16_t maxBytes)
    {
        if(empty())
            return 0;
        const uint16_t n = len[tail];
        if(n > maxBytes)
        {
            tail = (uint8_t)((tail + 1) % Slots); // cannot deliver it; drop
            dropped++;
            return 0;
        }
        memcpy(dst, data[tail], n);
        tail = (uint8_t)((tail + 1) % Slots);
        return n;
    }

    /// Look at the oldest packet without removing it.
    uint16_t peek(const uint8_t **out) const
    {
        if(empty())
            return 0;
        *out = data[tail];
        return len[tail];
    }

    void drop() { tail = (uint8_t)((tail + 1) % Slots); }
};

static PacketRing<kRxSlots, kRxSlotBytes> sRx;
static PacketRing<kTxSlots, kMaxTxPacket> sTx;

// ---------------------------------------------------------------------------
// GATT objects
// ---------------------------------------------------------------------------

/// Drop everything queued for a connection that has gone. Defined with the
/// outbound state further down; declared here because the service class below
/// calls it from disconnected().
static void bleTxReset();

/**
 * The library's BLECharacteristic::setValue() cannot be used for MIDI.
 *
 * It keeps *one* pending notification — a single `_notifyInfo` pointing at a
 * `_charData` it reallocs on every call — so a second setValue() before the
 * stack's can-send callback fires overwrites the first, silently losing a MIDI
 * message, and re-registers an already-queued btstack callback.  With a patch
 * dump spanning five packets that is not an edge case, it is the normal path.
 *
 * So this subclass exists only to reach the three protected members needed to
 * drive att_server_notify() directly, against a proper queue.
 */
class BleMidiChar : public BLECharacteristic
{
  public:
    BleMidiChar()
    : BLECharacteristic(BLEUUID(kMidiCharUuid),
                        BLERead | BLEWriteWithoutResponse | BLENotify,
                        "Alloy MIDI I/O")
    {
    }

    bool     canNotify() const { return _notificationEnabled && con_handle; }
    uint16_t conHandle() const { return con_handle; }
    uint16_t attHandle() const { return _valueHandle; }
};

class BleMidiService : public BLEService, public BLECharacteristicCallbacks
{
  public:
    BleMidiService() : BLEService(BLEUUID(kMidiServiceUuid))
    {
        _io.setCallbacks(this);
        addCharacteristic(&_io);
    }

    BleMidiChar &io() { return _io; }

    // A queue belongs to the connection that was going to receive it. Without
    // this, CC values captured for a host that has walked away are delivered
    // to the next one that connects, ahead of its own patch dump.
    //
    // Defined below with the rest of the outbound state, which is declared
    // after this class — see bleTxReset().
    void disconnected() override
    {
        bleTxReset();
        BLEService::disconnected();
    }

  private:
    // Runs in BT context. Queue only — never dispatch here: a SysEx preset
    // command writes flash, which must not happen from an interrupt.
    void onWrite(BLECharacteristic *c) override
    {
        if(c != &_io)
            return;
        sRx.push((const uint8_t *)c->valueData(), (uint16_t)c->valueLen());
    }

    BleMidiChar _io;
};

static BleMidiService sService;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

/// `ble poll 0` — diagnostic only. See bleMidi_setPolling() in the header for
/// what it is for and why it splits the cost question in half.
static volatile bool sPollEnabled = true;

static bool     sRadioUp      = false;
static bool     sStarted      = false;
static uint32_t sAdvertiseEnd = 0;
static bool     sAdvertising  = false;

// Outbound serialisation: at most one notification in flight at a time.
static btstack_context_callback_registration_t sCanSend;
static volatile bool                           sSendArmed = false;

// SysEx reassembly, driven by the inbound parser.
static uint8_t  sSysEx[kSysExMax];
static uint16_t sSysExLen = 0;
static bool     sInSysEx  = false;

// Running status for inbound channel messages.
static uint8_t sRunningStatus = 0;

static MidiPort *sPortSelf = nullptr; // set once registered, for handleSysEx

// ---------------------------------------------------------------------------
// BLE MIDI timestamps — 13 bits of milliseconds, split across the header byte
// and a timestamp byte. Hosts use them to de-jitter; we only have to emit
// something monotonic.
// ---------------------------------------------------------------------------

static inline uint8_t headerByte()
{ return (uint8_t)(0x80 | ((millis() >> 7) & 0x3F)); }

static inline uint8_t timestampByte()
{ return (uint8_t)(0x80 | (millis() & 0x7F)); }

// ---------------------------------------------------------------------------
// Outbound
// ---------------------------------------------------------------------------

/// Hand the oldest queued packet to btstack. Runs in BT context.
static void canSendCB(void *)
{
    sSendArmed         = false;
    const uint8_t *pkt = nullptr;
    const uint16_t n   = sTx.peek(&pkt);
    if(n == 0)
        return;
    const uint8_t err = att_server_notify(
        sService.io().conHandle(), sService.io().attHandle(), pkt, n);
    if(err != 0)
    {
        // ⚠ Do NOT re-arm here. Re-registering the can-send callback after a
        // failed notify is an unbounded loop: btstack calls straight back, the
        // same packet fails again, and the callback re-registers — an IRQ storm
        // on core 0 for as long as the condition lasts. The retry belongs on
        // the 128 Hz tick in bleMidi_update(), which bounds it by construction.
        return;
    }
    sTx.drop();
    if(!sTx.empty() && sService.io().canNotify())
    {
        sSendArmed        = true;
        sCanSend.callback = canSendCB;
        sCanSend.context  = nullptr;
        att_server_register_can_send_now_callback(&sCanSend,
                                                  sService.io().conHandle());
    }
}

/// Ask the stack to call us back when there is room. Caller holds the lock.
static void armSend()
{
    if(sSendArmed || sTx.empty() || !sService.io().canNotify())
        return;
    sSendArmed        = true;
    sCanSend.callback = canSendCB;
    sCanSend.context  = nullptr;
    att_server_register_can_send_now_callback(&sCanSend,
                                              sService.io().conHandle());
}

// ---------------------------------------------------------------------------
// Coalescing buffer for channel messages.
//
// ⚠ This exists because the obvious implementation — one packet per CC —
// measurably broke the audio on Alloy Coil, and the measurement is worth
// keeping. A BLE MIDI packet holds several messages, and the feedback diff
// fires every 250 ms with every CC that changed since the last one, so
// dragging a control produced a burst of separate GATT notifications back to
// back. On a module with 10% block headroom that burst turned ~124 slow blocks
// per second (harmless, absorbed by the DMA's 2.7 ms cushion) into ~5 DMA
// underruns per second — audible, evenly spaced clicks at the feedback rate.
//
// Average CPU barely moved either way: the fault was never throughput, it was
// a burst of radio events draining a cushion that had no slack to give. One
// packet per tick instead of one per CC removes the burst rather than the work.
// ---------------------------------------------------------------------------

static uint8_t sPend[kMaxTxPacket];
static uint8_t sPendLen = 0; // 0 = empty; otherwise includes the header byte

/// Append one complete channel message to the pending packet, starting a new
/// one if it will not fit. Caller holds the lock.
static void pendMessage(const uint8_t *msg, uint8_t n)
{
    // Each message costs a timestamp byte plus its own length.
    if(sPendLen != 0 && (uint8_t)(sPendLen + 1 + n) > kMaxTxPacket)
    {
        sTx.push(sPend, sPendLen);
        sPendLen = 0;
    }
    if(sPendLen == 0)
        sPend[sPendLen++] = headerByte();
    sPend[sPendLen++] = timestampByte();
    for(uint8_t i = 0; i < n; i++)
        sPend[sPendLen++] = msg[i];
}

/// Queue whatever has accumulated. Caller holds the lock.
static void pendFlush()
{
    if(sPendLen == 0)
        return;
    sTx.push(sPend, sPendLen);
    sPendLen = 0;
}

static void bleTxReset()
{
    sPendLen   = 0;
    sTx.head   = 0;
    sTx.tail   = 0;
    sSendArmed = false;
}

static void bleSendCC(uint8_t cc, uint8_t value, uint8_t channel)
{
    const uint8_t ch
        = (uint8_t)((channel >= 1 && channel <= 16) ? channel - 1 : 0);
    const uint8_t msg[3]
        = {(uint8_t)(0xB0 | ch), (uint8_t)(cc & 0x7F), (uint8_t)(value & 0x7F)};
    BluetoothLock lock;
    pendMessage(msg, sizeof(msg));
    // Deliberately no armSend() here. The packet is flushed once per control
    // tick in bleMidi_update(), so a feedback pass that changes five CCs is one
    // radio event rather than five. Worst-case added latency is one tick,
    // 7.8 ms — well inside BLE's own connection-interval jitter, and nothing
    // next to the 250 ms period of the diff that produced it.
}

/**
 * Fragment a SysEx across as many packets as it takes.
 *
 * `body` carries neither F0 nor F7 — that is midi_core's contract, and it is
 * this function that adds them.  The BLE MIDI framing is particular about
 * where they go: F0 follows a timestamp in the first packet, continuation
 * packets carry a header byte and nothing but data, and F7 gets a timestamp
 * byte of its own immediately before it in the last packet.
 */
static void bleSendSysEx(const uint8_t *body, uint16_t len)
{
    BluetoothLock lock;

    // Anything coalescing behind us goes first, or a patch dump would overtake
    // the CC feedback that was queued before it and the host would apply them
    // out of order.
    pendFlush();

    uint8_t  pkt[kMaxTxPacket];
    uint16_t src   = 0;
    bool     first = true;

    for(;;)
    {
        uint8_t n = 0;
        pkt[n++]  = headerByte();
        if(first)
        {
            pkt[n++] = timestampByte();
            pkt[n++] = 0xF0;
            first    = false;
        }
        while(src < len && n < kMaxTxPacket)
            pkt[n++] = (uint8_t)(body[src++] & 0x7F);

        if(src >= len && n + 2 <= kMaxTxPacket)
        {
            // Body finished and the trailer fits in this packet.
            pkt[n++] = timestampByte();
            pkt[n++] = 0xF7;
            sTx.push(pkt, n);
            break;
        }

        sTx.push(pkt, n);

        if(src >= len)
        {
            // Body finished but the trailer did not fit — send it alone.
            n        = 0;
            pkt[n++] = headerByte();
            pkt[n++] = timestampByte();
            pkt[n++] = 0xF7;
            sTx.push(pkt, n);
            break;
        }
    }

    armSend();
}

static bool bleReady()
{ return sRadioUp && sService.io().canNotify(); }

static MidiPort sBlePort(bleSendCC, bleSendSysEx, bleReady, "ble");

// ---------------------------------------------------------------------------
// Inbound parsing
//
// A BLE MIDI packet is a header byte followed by a stream of
// [timestamp][status][data…] groups, with running status allowed and SysEx
// spanning packets.  Status bytes and timestamp bytes are both 0x80-or-above,
// so they can only be told apart by position: after the header, and after a
// completed message, the next high byte is a timestamp and the one after it is
// the status.
// ---------------------------------------------------------------------------

static inline uint8_t dataBytesFor(uint8_t status)
{
    switch(status & 0xF0)
    {
        case 0xC0: // Program Change
        case 0xD0: // Channel Pressure
            return 1;
        default: return 2;
    }
}

static void dispatchChannelMessage(uint8_t status, uint8_t d1, uint8_t d2)
{
    // midi_core takes 1-16; the wire carries 0-15.
    const uint8_t ch = (uint8_t)((status & 0x0F) + 1);
    switch(status & 0xF0)
    {
        case 0x80: midiCore_handleNoteOff(ch, d1); break;
        case 0x90: midiCore_handleNoteOn(ch, d1, d2); break;
        case 0xB0: midiCore_handleControlChange(&sBlePort, ch, d1, d2); break;
        case 0xC0: midiCore_handleProgramChange(ch, d1); break;
        default: break; // aftertouch, pitch bend: nothing consumes them yet
    }
}

static void parsePacket(const uint8_t *pkt, uint16_t len)
{
    if(len < 1)
        return;
    uint16_t i = 1; // [0] is the header byte

    while(i < len)
    {
        if(sInSysEx)
        {
            const uint8_t b = pkt[i];
            if(b & 0x80)
            {
                // A high byte inside SysEx is the timestamp before F7.
                i++;
                if(i < len && pkt[i] == 0xF7)
                {
                    i++;
                    midiCore_handleSysEx(&sBlePort, sSysEx, sSysExLen);
                }
                // Anything else is malformed: abandon the message rather than
                // apply half a patch.
                sInSysEx  = false;
                sSysExLen = 0;
            }
            else
            {
                if(sSysExLen < kSysExMax)
                    sSysEx[sSysExLen++] = b;
                else
                    sInSysEx = false; // overrun — drop it whole
                i++;
            }
            continue;
        }

        if(!(pkt[i] & 0x80))
        {
            // Running status: data bytes with no timestamp of their own.
            if(sRunningStatus == 0)
            {
                i++; // no status to attach them to
                continue;
            }
            const uint8_t need = dataBytesFor(sRunningStatus);
            if(i + need > len)
                break;
            const uint8_t d1 = pkt[i];
            const uint8_t d2 = (need == 2) ? pkt[i + 1] : 0;
            i += need;
            dispatchChannelMessage(sRunningStatus, d1, d2);
            continue;
        }

        // A timestamp byte. The status byte follows it — unless the sender is
        // using running status, in which case data follows directly.
        i++;
        if(i >= len)
            break;

        if(pkt[i] & 0x80)
        {
            const uint8_t status = pkt[i++];
            if(status == 0xF0)
            {
                sInSysEx  = true;
                sSysExLen = 0;
                continue;
            }
            if(status >= 0xF8)
                continue; // realtime — clock, start, stop; no data bytes
            if(status == 0xF7)
                continue; // stray terminator
            if(status >= 0xF1 && status <= 0xF6)
            {
                // System common. Skip its data rather than misread it as a
                // channel message.
                const uint8_t skip
                    = (status == 0xF2)
                          ? 2
                          : ((status == 0xF1 || status == 0xF3) ? 1 : 0);
                i += skip;
                sRunningStatus = 0;
                continue;
            }
            sRunningStatus = status;
        }

        if(sRunningStatus == 0)
            continue;
        const uint8_t need = dataBytesFor(sRunningStatus);
        if(i + need > len)
            break;
        const uint8_t d1 = pkt[i];
        const uint8_t d2 = (need == 2) ? pkt[i + 1] : 0;
        i += need;
        dispatchChannelMessage(sRunningStatus, d1, d2);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void bleMidi_init()
{
    // The one check that makes a 2W image safe on a board whose radio is not
    // there.  It has to happen before BLE.begin(), because BLEClass::begin()
    // ends in `while (!_addr) delay(10);` — an unbounded wait for an HCI
    // address that would never arrive, hanging the module at boot with no
    // audio and no console.
    //
    // ⚠ cyw43_is_initialized() alone is NOT that check, which is not obvious
    // from its name: cyw43_init() sets `initted = true` having only configured
    // GPIOs and driver state, and never touches the chip — the real bring-up
    // is deferred to cyw43_ensure_up() on first use.  So on a plain Pico 2
    // running this image it returns true, and the guard it was written to be
    // would wave us straight into the hang it exists to prevent.
    //
    // cyw43_wifi_pm() is the cheapest public call that forces the bring-up and
    // returns its status, and the value written is the one the driver already
    // defaults to, so on a real board it changes nothing.  With no chip there,
    // cyw43_ll_bus_init() gives up after ten 1 ms reads of a test register and
    // returns an error rather than panicking — bounded, which is the whole
    // point.  On a real 2W this downloads the firmware a moment earlier than
    // BLE would have; ensure_up() is idempotent, so nothing happens twice.
    //
    // Still defence in depth rather than the supported fallback: the supported
    // way to run without a radio is to build env:alloyflux, which contains
    // none of this file at all.
    // ⚠ This check used to also call cyw43_wifi_pm(), to force
    // cyw43_ensure_up() and read its status — a genuine probe, because
    // cyw43_is_initialized() only reports that cyw43_init() ran and never
    // touches the chip. It worked, and it cost far too much: ensure_up brings
    // the **WiFi** interface up on a module that only wants Bluetooth. That
    // leaves cyw43_poll non-NULL, so the async context polls the driver
    // continuously, and puts WiFi into PM2 powersave — periodic SPI and DMA
    // traffic for the life of the module, connected or not.
    //
    // Measured on Alloy Coil: the per-block cost went from ~595 us to ~1036 us
    // against a 666 us budget, with nothing moving and no host connected.
    // Every block late. A probe is not worth two thirds of the audio budget.
    //
    // So the cheap check is back, and the hang it does not catch — a 2W image
    // on a board with no radio, where BLEClass::begin() waits forever for an
    // HCI address — is handled where it always should have been: by not
    // building that image for that board. `make firmware WIRELESS=0` produces
    // an image with none of this file in it, and platformio.ini says so.
    // A certain cost on every wireless module is worse than a hypothetical
    // hang on a configuration we tell people not to build.
    sRadioUp = cyw43_is_initialized(&cyw43_state);
    if(!sRadioUp)
    {
        DLOGLN("BLE: no radio — USB MIDI only");
        return;
    }

    // No pairing, no PIN, no bond: BLE MIDI does not require encryption, and
    // every host we care about connects without it.  If one ever refuses,
    // BLESecurityJustWorks is the one-line change — it is still PIN-free.
    BLE.setSecurity(BLESecurityNone);
    BLE.begin(kModuleProduct);
    BLE.server()->addService(&sService);

    // startAdvertising() is what builds the ATT database and starts the ATT
    // server, so it has to run once even though we do not want to be
    // discoverable yet.  gap_advertisements_enable(0) then goes quiet without
    // tearing any of that down — which is why the pairing window is toggled
    // with the gap_* call directly rather than BLE.stopAdvertising(), which
    // also calls att_server_deinit() and would drop a live connection.
    BLE.startAdvertising();

    // Put the *complete* name in the scan response.
    //
    // The advertising packet has no room for it: 31 bytes, of which the flags
    // take 3 and the 128-bit BLE MIDI service UUID takes 18, leaving 10 — two
    // of those are the AD header, so exactly **8 characters** of name survive.
    // The library truncates and marks it SHORTENED_LOCAL_NAME, which is why a
    // chooser shows "Alloy Co" rather than "Alloy Coil". Nothing is broken;
    // the packet is simply full.
    //
    // Dropping the service UUID would free the room and break discovery — the
    // Alloy Controller filters on that service, and so does every BLE MIDI
    // host. Shortening kModuleProduct would fix the symptom by making the name
    // worse everywhere else, USB descriptors included.
    //
    // The scan response is the second 31-byte packet the spec provides for
    // exactly this case, and an active scan — which Chrome and both phone
    // operating systems perform — reads it. 29 characters fit, against the
    // 10 either module needs.
    //
    // ⚠ btstack keeps this pointer rather than copying, so the buffer is
    // static and must stay that way.
    {
        static uint8_t sScanResp[31];
        const size_t   nameLen = strnlen(kModuleProduct, sizeof(sScanResp) - 2);
        sScanResp[0]           = (uint8_t)(nameLen + 1);
        sScanResp[1]           = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
        memcpy(&sScanResp[2], kModuleProduct, nameLen);
        BluetoothLock lock;
        gap_scan_response_set_data((uint8_t)(nameLen + 2), sScanResp);
    }

    {
        BluetoothLock lock;
        gap_advertisements_enable(0);
    }
    sAdvertising = false;
    sStarted     = true;

    midiCore_registerPort(&sBlePort);
    sPortSelf = &sBlePort;

    // Never discoverable until asked. Both modules bind that to a 3 s hold on
    // the one button they have free — MODE on AlloyFlux, SHIFT on Alloy Coil —
    // and `ble pair` on the console does the same thing without the panel.
    DLOGLN("BLE: MIDI ready — hold the panel button 3 s to make discoverable");
}

void bleMidi_startPairing()
{
    if(!sStarted)
        return;
    {
        BluetoothLock lock;
        gap_advertisements_enable(1);
    }
    sAdvertising  = true;
    sAdvertiseEnd = millis() + kAdvertiseMs;
    DLOGLN("BLE: advertising");
}

void bleMidi_stopPairing()
{
    if(!sStarted || !sAdvertising)
        return;
    {
        BluetoothLock lock;
        gap_advertisements_enable(0);
    }
    sAdvertising = false;
    DLOGLN("BLE: advertising off");
}

void bleMidi_update()
{
    if(!sStarted || !sPollEnabled)
        return;

    // Close the pairing window. A host that connected during it stays
    // connected — only discoverability expires.
    if(sAdvertising && (int32_t)(millis() - sAdvertiseEnd) >= 0)
        bleMidi_stopPairing();

    // Drain inbound. The lock is taken and released around each copy rather
    // than held across the parse: dispatching a SysEx can write flash, and
    // holding the BT lock across that would stall the stack for ~10 ms.
    static uint8_t pkt[kRxSlotBytes];
    for(uint8_t i = 0; i < kMaxPacketsPerTick; i++)
    {
        uint16_t n = 0;
        {
            BluetoothLock lock;
            n = sRx.pop(pkt, sizeof(pkt));
        }
        if(n == 0)
            break;
        parsePacket(pkt, n);
    }

    // One flush, one arm, one lock per tick. Anything the drain queued — a
    // PATCH_DUMP answering a REQUEST_DUMP — goes out in this same tick; CC
    // feedback runs after this in updateControl() and so leaves on the next
    // one, 7.8 ms later. That single flush point is what keeps a feedback pass
    // from becoming a burst of radio events; see pendMessage().
    {
        BluetoothLock lock;
        pendFlush();
        armSend();
    }
}

void bleMidi_setPolling(bool on)
{ sPollEnabled = on; }

bool bleMidi_polling()
{ return sPollEnabled; }

BleMidiState bleMidi_state()
{
    if(!sStarted)
        return BleMidiState::Unavailable;
    if(sService.io().conHandle())
        return BleMidiState::Connected;
    return sAdvertising ? BleMidiState::Advertising : BleMidiState::Idle;
}

const char *bleMidi_stateName()
{
    switch(bleMidi_state())
    {
        case BleMidiState::Connected: return "connected";
        case BleMidiState::Advertising: return "advertising";
        case BleMidiState::Idle: return "idle";
        default:
            // Which *kind* of unavailable, because the panel cannot say and
            // the two have nothing to do with each other: no radio answered
            // at boot, or the stack was there and failed to come up.
            return sRadioUp ? "init-failed" : "no-radio";
    }
}

#endif // ALLOY_BLE
