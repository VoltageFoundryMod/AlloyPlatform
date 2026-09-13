#pragma once

#include <stdint.h>

/**
 * BLE MIDI transport — Milestone 78b.
 *
 * A second `MidiPort` alongside USB, speaking the standard BLE MIDI service
 * (`03B80E5A-…C700`).  Any BLE-MIDI host talks to it with no driver and no
 * PIN: a phone or tablet running a keyboard app, the Alloy Controller over
 * Web Bluetooth, or a BLE MIDI controller straight into the rack.
 *
 * **Wireless is an addition, never a dependency.** Every function here is
 * safe to call on a board with no radio, and on a build with no BLE compiled
 * in — the stubs return `Unavailable` and do nothing.  Three independent
 * things have to be true before a single byte moves:
 *
 *   1. `ALLOY_BLE` defined         — the module opted in (AlloyFlux, not Coil:
 *                                    Coil has no RAM to spare, see M63i)
 *   2. `PICO_CYW43_SUPPORTED`      — the board target is a 2W
 *   3. `cyw43_is_initialized()`    — the radio actually answered at boot
 *
 * Miss any of them and the module behaves exactly as it does today: USB MIDI,
 * USB CDC and the panel, with no error, no stall and no branch in the audio
 * path.  (3) is the one that matters in the field — it is what makes a 2W
 * image safe to flash onto a plain Pico 2.
 *
 * Threading: everything here runs on **core 0 only**, from `updateControl()`.
 * BTstack's callbacks arrive in a low-priority IRQ, so inbound packets are
 * queued there and parsed in `bleMidi_update()` rather than dispatched on the
 * spot — a SysEx preset command can write flash, which must never happen from
 * an interrupt.
 */

enum class BleMidiState : uint8_t
{
    /// Not compiled in, not a 2W, or the radio did not come up.
    Unavailable = 0,
    /// Radio up, not discoverable, nobody connected.
    Idle,
    /// Discoverable — the pairing window opened by the MODE hold.
    Advertising,
    /// A host is connected; MIDI flows.
    Connected,
};

/** Bring up the radio and register the BLE MidiPort.  Call once from setup(),
 *  **after** the audio driver has started: the cyw43 bus is PIO-SPI and claims
 *  a state machine, so I2S must get its own first. */
void bleMidi_init();

/** Drain inbound packets and run the advertising timeout.
 *  Call from updateControl(), on core 0, next to usbMidi_update(). */
void bleMidi_update();

/** Open the pairing window: advertise for `kBleAdvertiseMs`, then stop.
 *  Bound to a long hold of MODE. Calling it while already advertising
 *  restarts the window rather than cancelling it. */
void bleMidi_startPairing();

/** Close the pairing window early.  Does not drop a live connection. */
void bleMidi_stopPairing();

/** Current state — drives the SHIFT backlight and the console `status` line. */
BleMidiState bleMidi_state();

/** `bleMidi_state()` as a short string, for the serial console. */
const char *bleMidi_stateName();

/**
 * Cost bisect: stop bleMidi_update() doing any work, without a reflash.
 *
 * Same tool as M63i's `smooth` and `limiter` — the point is to attribute block
 * time on a live board rather than by reflashing per hypothesis. The radio
 * stays up and BTstack keeps running; only this module's per-tick polling,
 * draining and flushing stops.
 *
 * So it splits the question exactly in half: if `ble poll 0` restores the audio
 * then the cost is in what we do every tick, and if it does not, the cost is
 * simply having the radio and BTstack up at all — which is not something this
 * file can fix.
 *
 * ⚠ MIDI over BLE stops working while polling is off. Diagnostic only.
 */
void bleMidi_setPolling(bool on);
bool bleMidi_polling();
