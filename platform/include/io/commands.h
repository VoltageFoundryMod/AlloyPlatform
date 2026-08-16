#pragma once
#include <Arduino.h> // for Print

/**
 * Command dispatch table — shared by all control interfaces:
 *   Serial console, MIDI SysEx, I2C (Teletype), USB configurator.
 *
 * The dispatcher is the platform's; the table is the module's. Each handler
 * receives:
 *   args — everything after "name " (empty string if no arguments)
 *   out  — the transport's Print sink (Serial, I2C stream, MIDI buffer, ...)
 *
 * To add a command, write a handler in the module's commands.cpp and add one
 * row to its kCommands[]. No changes are needed anywhere else.
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

/// Defined by the module — iterable by any transport layer.
extern const CommandEntry kCommands[];
extern const uint8_t      kCommandCount;

/**
 * Dispatch a null-terminated command string to the matching handler.
 * Writes the response to `out`.
 * On no match: prints "unknown: <cmd>" then the full help listing.
 */
void commands_dispatch(const char *cmd, Print &out);

/** Print the full command help listing to `out`. */
void commands_printHelp(Print &out);
