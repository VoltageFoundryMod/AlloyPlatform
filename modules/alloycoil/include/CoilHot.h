#pragma once

/**
 * COIL_HOT — put a function in RAM instead of XIP flash (Milestone 81).
 *
 * Alloy Coil's per-sample path lives in flash: only `renderAudio()` and
 * `loop1()` carry `.time_critical`, and everything they call — the engine, the
 * resonator, the echo, the smoother — is fetched over XIP and reached from RAM
 * through a long-branch veneer.
 *
 * That made the module's block time depend on *where its code landed*. Adding
 * ~300 bytes anywhere in the image shifted the hot path and cost **172 µs a
 * block, 585 → 767 µs**, from four integer compares that could not possibly
 * account for it. The whole hot set is only ~3 KB against a 16 KB XIP cache,
 * so it is not a capacity problem — a handful of hot functions aliasing into
 * the same cache sets is enough, and which ones alias changes every time
 * anything before them changes size.
 *
 * Marking a function COIL_HOT copies it to RAM at boot, where there is no
 * cache to miss and placement costs nothing. The point is as much the
 * *stability* as the speed: a module whose block time moves 30 % when an
 * unrelated string grows cannot be tuned, because every measurement is against
 * a different layout.
 *
 * Cost is SRAM, and Alloy Coil has little to spare — check the RAM figure
 * after adding one. Apply it to the per-sample path and to what the block
 * calls once, not to setters and Init() paths that run when a knob moves.
 *
 * ⚠ Guarded on ARDUINO because these headers are shared with the VCV plugin,
 * where `.time_critical` does not exist and the attribute would not link.
 */

/**
 * Takes a unique name, like pico-sdk's `__not_in_flash_func`, and for the same
 * reason: inline and template functions are emitted into COMDAT groups, and
 * GCC refuses to put two of those in one named section — "causes a section
 * type conflict". One section per function is also what lets the map file say
 * which of them is costing the RAM.
 *
 *     COIL_HOT(engine_process) void Engine::Process(...)
 */
#ifdef ARDUINO
#define COIL_HOT(name) __attribute__((section(".time_critical.coil_" #name)))
#else
#define COIL_HOT(name)
#endif
