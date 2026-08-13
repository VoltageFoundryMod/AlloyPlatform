#pragma once

/**
 * Diagnostic helpers — only active when SERIAL_CONTROL is defined.
 *
 * DLOG(x) / DLOGLN(x)
 *   Lightweight logging macros that map to Serial.print() when SERIAL_CONTROL
 *   is set and compile away to nothing in production builds.
 */

#ifdef SERIAL_CONTROL

#include <Arduino.h>
#include <stdint.h>

#define DLOG(x) Serial.print(x)
#define DLOGLN(x) Serial.println(x)

#else

#define DLOG(x) ((void)0)
#define DLOGLN(x) ((void)0)

#endif // SERIAL_CONTROL
