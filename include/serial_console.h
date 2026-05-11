#pragma once

/**
 * Serial console — development REPL for real-time parameter changes.
 *
 * Compiled only when SERIAL_CONTROL is defined (platformio.ini build_flags).
 * Stubs are provided so call sites in main.cpp need no #ifdef guards.
 *
 * Command dispatch is handled by commands.h / commands.cpp, which is shared
 * with MIDI, I2C, and USB interfaces. Add commands there, not here.
 *
 * UX features:
 *   - Device-side echo (set monitor_echo = false in platformio.ini)
 *   - Backspace / DEL supported
 *   - CR, LF, and CR+LF line endings all handled correctly (no double prompt)
 *   - Empty Enter or '?' alone reprints the command list
 */

#ifdef SERIAL_CONTROL

void serialConsole_init();   // start Serial, print banner
void serialConsole_ready();  // print help + prompt (call after all init done)
void serialConsole_update(); // call from updateControl() every control cycle

#else

inline void serialConsole_init() {}
inline void serialConsole_ready() {}
inline void serialConsole_update() {}

#endif // SERIAL_CONTROL
