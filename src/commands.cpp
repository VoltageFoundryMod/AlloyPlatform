#include "commands.h"
#include "params.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Command handlers
//
// Use out.print() / out.println() — never Serial directly.
// This keeps handlers transport-agnostic: serial, MIDI SysEx, I2C, etc.
// atof() used instead of sscanf(%f) — avoids pulling full sscanf float impl.
// ---------------------------------------------------------------------------

static void cmd_pitch(const char *args, Print &out) {
    gBaseFreq = constrain((float)atof(args), 20.0f, 8000.0f);
    out.print(F("pitch -> "));
    out.println(gBaseFreq, 2);
}

static void cmd_detune(const char *args, Print &out) {
    gDetune = constrain((float)atof(args), 0.0f, 200.0f);
    out.print(F("detune -> "));
    out.println(gDetune, 2);
}

static void cmd_wave(const char *args, Print &out) {
    gWaveform = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("wave -> "));
    out.println(gWaveform, 3);
}

static void cmd_vol(const char *args, Print &out) {
    gVolume = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("vol -> "));
    out.println(gVolume, 3);
}

static void cmd_status(const char * /*args*/, Print &out) {
    out.print(F("pitch="));
    out.print(gBaseFreq, 2);
    out.print(F(" detune="));
    out.print(gDetune, 2);
    out.print(F(" wave="));
    out.print(gWaveform, 3);
    out.print(F(" vol="));
    out.println(gVolume, 3);
}

static void cmd_performance_print(const char *args, Print &out) {
#ifdef CPU_PROFILE
    if (strcmp(args, "on") == 0) {
        gPerformancePrintEnabled = true;
        out.println(F("CPU profiling print enabled"));
    } else if (strcmp(args, "off") == 0) {
        gPerformancePrintEnabled = false;
        out.println(F("CPU profiling print disabled"));
    } else {
        out.println(F("usage: performance on|off"));
    }
#else
    out.println(F("CPU_PROFILE not active — add -DCPU_PROFILE to build_flags"));
#endif
}

static void
cmd_cpu(const char * /*args*/, Print &out) {
#ifdef CPU_PROFILE
    const uint32_t us = gAudioElapsedUs;
    const uint32_t over = gAudioOverruns;
    const float headroom = (30.0f - (float)us) / 30.0f * 100.0f;
    out.print(F("audio ISR: "));
    out.print(us);
    out.print(F("us / 30us  headroom: "));
    out.print(headroom, 1);
    out.print(F("%  overruns: "));
    out.println(over);
#else
    out.println(F("CPU_PROFILE not active — add -DCPU_PROFILE to build_flags"));
#endif
}

// Forward declaration — cmd_help calls commands_printHelp which needs the table,
// and the table needs cmd_help.  Define cmd_help after the table is declared.
static void cmd_help(const char *args, Print &out);

// ---------------------------------------------------------------------------
// Command table — the single source of truth for all transports.
//
// Columns: name | help text shown in listing | handler
// ---------------------------------------------------------------------------

// clang-format off
const CommandEntry kCommands[] = {
    {"pitch",  "<hz>   base frequency (20-8000 Hz)", cmd_pitch},
    {"detune", "<hz>   symmetric spread (0-200 Hz)", cmd_detune},
    {"wave",   "<0-1>  waveform blend: 0=sine, 1=saw", cmd_wave},
    {"vol",    "<0-1>  master volume", cmd_vol},
    {"status", "       print all current parameters", cmd_status},
    {"perf",   "       enable or disable CPU profiling printing", cmd_performance_print},
    {"cpu",    "       audio ISR µs, headroom, overrun count", cmd_cpu},
    {"help",   "       show this help", cmd_help},
};
// clang-format on

const uint8_t kCommandCount =
    (uint8_t)(sizeof(kCommands) / sizeof(kCommands[0]));

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void commands_printHelp(Print &out) {
    out.println(F("Commands:"));
    for (uint8_t i = 0; i < kCommandCount; i++) {
        out.print(F("  "));
        out.print(kCommands[i].name);
        out.print(' ');
        out.println(kCommands[i].help);
    }
}

static void cmd_help(const char * /*args*/, Print &out) {
    commands_printHelp(out);
}

void commands_dispatch(const char *cmd, Print &out) {
    // Skip leading whitespace
    while (*cmd == ' ')
        cmd++;

    for (uint8_t i = 0; i < kCommandCount; i++) {
        const char *name = kCommands[i].name;
        const size_t nlen = strlen(name);
        if (strncmp(cmd, name, nlen) == 0 &&
            (cmd[nlen] == ' ' || cmd[nlen] == '\0')) {
            const char *args = (cmd[nlen] == ' ') ? cmd + nlen + 1 : "";
            kCommands[i].handler(args, out);
            return;
        }
    }
    out.print(F("unknown: "));
    out.println(cmd);
    commands_printHelp(out);
}
