#ifdef USE_TINYUSB

#include "io/usb_midi.h"
#include "ModuleHooks.h"
#include "io/midi_core.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

// ---------------------------------------------------------------------------
// USB MIDI — a transport, and since M78a nothing but a transport.
//
// The Alloy SysEx protocol, parameter dispatch and the CC feedback diff used
// to live here; they are in platform/src/midi_core.cpp now, because none of
// them are USB's.  What is left is the part that genuinely is: TinyUSB
// descriptors, the Arduino MIDI Library instance, and turning its callbacks
// into midiCore_handle*() calls.
// ---------------------------------------------------------------------------

static Adafruit_USBD_MIDI sUsbMidiTransport;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, sUsbMidiTransport, MidiUsb);

// --- MidiPort implementation ------------------------------------------------

static void usbSendCC(uint8_t cc, uint8_t value, uint8_t channel)
{ MidiUsb.sendControlChange(cc, value, channel); }

static void usbSendSysEx(const uint8_t *body, uint16_t len)
{
    // `false` = the array does *not* contain F0/F7; the library adds them.
    // midi_core's contract is the same, so this passes straight through.
    MidiUsb.sendSysEx(len, body, false);
}

static bool usbReady()
{ return TinyUSBDevice.mounted(); }

static MidiPort sUsbPort(usbSendCC, usbSendSysEx, usbReady, "usb");

// --- Arduino MIDI Library callbacks ----------------------------------------

static void onNoteOn(byte channel, byte note, byte velocity)
{ midiCore_handleNoteOn(channel, note, velocity); }

static void onNoteOff(byte channel, byte note, byte /*velocity*/)
{ midiCore_handleNoteOff(channel, note); }

static void onControlChange(byte channel, byte cc, byte value)
{ midiCore_handleControlChange(&sUsbPort, channel, cc, value); }

static void onProgramChange(byte channel, byte program)
{ midiCore_handleProgramChange(channel, program); }

// The Arduino MIDI Library v5 passes data[] with F0 at [0] and F7 at
// [length-1]; midiCore_handleSysEx() tolerates both boundaries.
static void onSysEx(uint8_t *data, unsigned int length)
{ midiCore_handleSysEx(&sUsbPort, data, (uint16_t)length); }

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

    midiCore_registerPort(&sUsbPort);

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
// messages/second — which is below what a dragged slider in the Alloy
// Controller emits. The excess sits in the USB FIFO and plays out at 128/s,
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

#endif // USE_TINYUSB
