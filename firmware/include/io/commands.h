#pragma once
#include <Arduino.h> // for Print

/**
 * Global command dispatch table — shared by all control interfaces:
 *   Serial console, MIDI SysEx, I2C (Teletype), USB configurator.
 *
 * Each handler receives:
 *   args — everything after "name " (empty string if no arguments)
 *   out  — the transport's Print sink (Serial, I2C stream, MIDI buffer, ...)
 *
 * To add a new command:
 *   1. Write a handler in src/commands.cpp:
 *        static void cmd_xxx(const char* args, Print& out) { ... }
 *   2. Add one row to kCommands[] in src/commands.cpp.
 *      No changes required in any other file.
 *
 * Command names are matched case-sensitively.
 * A command with arguments is invoked as:  name<space>args
 * A command without arguments is invoked as: name
 */

struct CommandEntry
{
    const char *name;
    const char *help; // shown as "  name help" in the help listing
    void (*handler)(const char *args, Print &out);
};

// Defined in src/commands.cpp — iterable by any transport layer.
extern const CommandEntry kCommands[];
extern const uint8_t      kCommandCount;

/**
 * Fire a gate pulse of the given duration on the current voice mode.
 * In POLY mode a free voice slot is allocated at gBaseFreq and released
 * when the pulse expires.  In all other modes it drives gGateHigh for the
 * same duration.  Call from the SHIFT button handler and from I2C triggers
 * as well as the serial cmd_trig handler.
 */
void                 doTrig(uint32_t durMs);
extern const uint8_t kCommandCount;

/**
 * Dispatch a null-terminated command string to the matching handler.
 * Writes the response to `out`.
 * On no match: prints "unknown: <cmd>" then the full help listing.
 */
void commands_dispatch(const char *cmd, Print &out);

/** Print the full command help listing to `out`. */
void commands_printHelp(Print &out);
