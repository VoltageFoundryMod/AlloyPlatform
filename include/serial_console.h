#pragma once

/**
 * Serial console — development REPL for real-time parameter changes.
 *
 * Compiled only when SERIAL_CONTROL is defined (set via platformio.ini
 * build_flags).  When the flag is absent the functions below become empty
 * inlines so call sites in main.cpp need no #ifdef guards.
 *
 * Supported commands (newline-terminated):
 *   pitch <hz>   — set base frequency (20–8000 Hz)
 *   detune <hz>  — symmetric detune spread in Hz (0–200)
 *   wave  <0-1>  — waveform blend: 0 = sine, 1 = saw
 *   vol   <0-1>  — master volume
 *   status       — print all current parameter values
 */

#ifdef SERIAL_CONTROL

void serialConsole_init();  // start serial, print banner
void serialConsole_ready(); // print help + prompt (call after all init is done)
void serialConsole_update();

#else

inline void serialConsole_init() {}
inline void serialConsole_ready() {}
inline void serialConsole_update() {}

#endif // SERIAL_CONTROL
