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

static void cmd_shape(const char *args, Print &out) {
    gShape = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("shape -> "));
    out.println(gShape, 3);
}

static void cmd_fat(const char *args, Print &out) {
    gFatness = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("fat -> "));
    out.println(gFatness, 3);
}

static void cmd_motion(const char *args, Print &out) {
    gMotion = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("motion -> "));
    out.println(gMotion, 3);
}

static void cmd_curve(const char *args, Print &out) {
    gCurve = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("curve -> "));
    out.println(gCurve, 3);
}

static void cmd_curvetime(const char *args, Print &out) {
    gCurveTime = constrain((float)atof(args), 0.25f, 4.0f);
    out.print(F("curvetime -> "));
    out.println(gCurveTime, 2);
}

static void cmd_gate(const char *args, Print &out) {
    if (strcmp(args, "free") == 0) {
        gGatePatched = false;
        out.println(F("gate -> free (drone)"));
    } else {
        gGatePatched = true;
        gGateHigh = (atoi(args) != 0);
        out.print(F("gate -> "));
        out.println(gGateHigh ? F("high") : F("low"));
    }
}

extern uint32_t sTrigReleaseAt; // defined in main.cpp

static void cmd_trig(const char *args, Print &out) {
    const uint32_t dur = (*args != '\0') ? (uint32_t)atoi(args) : 100u;
    gGatePatched = true;
    gGateHigh = true;
    sTrigReleaseAt = millis() + dur;
    out.print(F("trig -> "));
    out.print(dur);
    out.println(F("ms"));
}

static void cmd_driftspeed(const char *args, Print &out) {
    gDriftSpeed = constrain((float)atof(args), 0.001f, 0.10f);
    out.print(F("driftspeed -> "));
    out.println(gDriftSpeed, 4);
}

static void cmd_chorus(const char *args, Print &out) {
    if (strcmp(args, "off") == 0 || strcmp(args, "0") == 0) {
        gChorusMode = ChorusMode::OFF;
        out.println(F("chorus -> off"));
    } else if (strcmp(args, "I") == 0 || strcmp(args, "1") == 0) {
        gChorusMode = ChorusMode::I;
        out.println(F("chorus -> I (slow, 0.51 Hz)"));
    } else if (strcmp(args, "II") == 0 || strcmp(args, "2") == 0) {
        gChorusMode = ChorusMode::II;
        out.println(F("chorus -> II (fast, 0.62 Hz)"));
    } else if (strcmp(args, "I+II") == 0 || strcmp(args, "3") == 0) {
        gChorusMode = ChorusMode::I_II;
        out.println(F("chorus -> I+II (L=slow, R=fast)"));
    } else {
        out.println(F("usage: chorus <off|I|II|I+II>"));
    }
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
    out.print(F(" shape="));
    out.print(gShape, 3);
    out.print(F(" fat="));
    out.print(gFatness, 3);
    out.print(F(" motion="));
    out.print(gMotion, 3);
    out.print(F(" dspeed="));
    out.print(gDriftSpeed, 4);
    out.print(F(" curve="));
    out.print(gCurve, 3);
    out.print(F(" ctime="));
    out.print(gCurveTime, 2);
    out.print(F(" gate="));
    out.print(gGatePatched ? (gGateHigh ? F("high") : F("low")) : F("free"));
    out.print(F(" chorus="));
    switch (gChorusMode) {
    case ChorusMode::OFF:
        out.print(F("off"));
        break;
    case ChorusMode::I:
        out.print(F("I"));
        break;
    case ChorusMode::II:
        out.print(F("II"));
        break;
    case ChorusMode::I_II:
        out.print(F("I+II"));
        break;
    }
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
    {"pitch",     "<hz>        base frequency (20-8000 Hz)",                              cmd_pitch},
    {"detune",    "<hz>        symmetric spread (0-200 Hz)",                              cmd_detune},
    {"shape",     "<0-1>       waveform: 0=sine  0.25=tri  0.5=saw  0.75=pulse  1=hollow", cmd_shape},
    {"fat",       "<0-1>       sub osc level: 0=off  1=full (50% of main)",               cmd_fat},
    {"motion",    "<0-1>       drift + chorus depth: 0=dry/static  1=full",              cmd_motion},
    {"dspeed",    "<0.001-0.1> drift glide speed: 0.001=glacial  0.04=default  0.1=fast", cmd_driftspeed},
    {"chorus",    "<off|I|II|I+II>  Juno chorus mode (default: I+II)",                   cmd_chorus},
    {"curve",     "<0-1>       envelope shape: 0=pluck  0.5=natural  1=swell",            cmd_curve},
    {"curvetime", "<0.25-4>    envelope time scale: 0.25=4x faster  1=default  4=4x slower", cmd_curvetime},
    {"gate",      "<1|0|free>  gate: 1=high  0=low  free=drone (bypass envelope)",        cmd_gate},
    {"trig",      "[ms]        trigger a note pulse (default 100ms gate)",                cmd_trig},
    {"vol",       "<0-1>       master volume",                                             cmd_vol},
    {"status",    "            print all current parameters",                              cmd_status},
    {"perf",      "            enable or disable CPU profiling printing",                  cmd_performance_print},
    {"cpu",       "            audio ISR µs, headroom, overrun count",                    cmd_cpu},
    {"help",      "            show this help",                                            cmd_help},
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
