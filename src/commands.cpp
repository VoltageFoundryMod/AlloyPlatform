#include "commands.h"
#include "config_store.h"
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

static void cmd_note(const char *args, Print &out) {
    // Simple note name parser: letter (A-G), optional accidental (# or b), octave number.
    // C4 = 261.63 Hz, A4 = 440 Hz, etc.
    if (strlen(args) < 2 || strlen(args) > 4) {
        out.println(F("invalid note format"));
        return;
    }
    char letter = args[0];
    int semitone = 0;
    if (letter >= 'A' && letter <= 'G') {
        semitone = letter - 'C';
        if (semitone < 0)
            semitone += 7; // wrap around so C=0, D=2, E=4, F=5, G=7
    } else {
        out.println(F("invalid note letter"));
        return;
    }
    int idx = 1;
    if (args[idx] == '#' || args[idx] == 'b') {
        semitone += (args[idx] == '#') ? 1 : -1;
        idx++;
    }
    int octave = atoi(&args[idx]);
    float freq = 16.35f * powf(2.0f, octave + semitone / 12.0f);
    gBaseFreq = constrain(freq, 20.0f, 8000.0f);
    out.print(F("note -> "));
    out.print(args);
    out.print(F(" ("));
    out.print(gBaseFreq, 2);
    out.println(F(" Hz)"));
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

static void cmd_rel(const char *args, Print &out) {
    gRelation = constrain((float)atof(args), 0.0f, 24.0f);
    out.print(F("rel -> "));
    out.print(gRelation, 1);
    out.println(F(" st"));
}

static void cmd_mode(const char *args, Print &out) {
    const struct {
        const char *name;
        VoiceMode mode;
        bool active;
    } modes[] = {
        {"pair", VoiceMode::PAIR, true},
        {"cloud", VoiceMode::CLOUD, false},
        {"chord", VoiceMode::CHORD, true},
        {"cascade", VoiceMode::CASCADE, false},
        {"string", VoiceMode::STRING, false},
    };
    for (uint8_t i = 0; i < 5; i++) {
        if (strcasecmp(args, modes[i].name) == 0) {
            if (!modes[i].active) {
                out.print(F("mode -> "));
                out.print(modes[i].name);
                out.println(F(" (not yet active)"));
                // Still store it so it shows in status for planning purposes
            } else {
                out.print(F("mode -> "));
                out.println(modes[i].name);
            }
            gVoiceMode = modes[i].mode;
            return;
        }
    }
    out.println(F("usage: mode <pair|cloud|chord|cascade|string>"));
    out.print(F("active modes: pair chord  current: "));
    out.println(voiceModeName(gVoiceMode));
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

static void cmd_space(const char *args, Print &out) {
    gSpace = constrain((float)atof(args), 0.0f, 2.0f);
    out.print(F("space -> "));
    out.println(gSpace, 3);
}

static void cmd_status(const char * /*args*/, Print &out) {
    out.print(F("pitch="));
    out.print(gBaseFreq, 2);
    out.print(F(" mode="));
    out.print(voiceModeName(gVoiceMode));
    out.print(F(" rel="));
    out.print(gRelation, 3);
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
    out.print(gVolume, 3);
    out.print(F(" space="));
    out.print(gSpace, 3);
    out.print(F(" midichan="));
    if (gMidiChannel == 0)
        out.println(F("omni"));
    else
        out.println(gMidiChannel);
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
    const uint32_t upSec = millis() / 1000;
    // uptime mm:ss
    const uint32_t mm = upSec / 60;
    const uint32_t ss = upSec % 60;
    // delta overruns since last cpu call — reveals if overruns are happening
    // right now (real problem) vs. just accumulated at startup/during serial TX
    static uint32_t lastOver = 0;
    const uint32_t delta = over - lastOver;
    lastOver = over;
    out.print(F("audio ISR: "));
    out.print(us);
    out.print(F("us / 30us  headroom: "));
    out.print(headroom, 1);
    out.print(F("%  overruns: "));
    out.print(over);
    out.print(F(" (+"));
    out.print(delta);
    out.print(F(" since last)  uptime: "));
    if (mm < 10)
        out.print('0');
    out.print(mm);
    out.print(':');
    if (ss < 10)
        out.print('0');
    out.println(ss);
#else
    out.println(F("CPU_PROFILE not active — add -DCPU_PROFILE to build_flags"));
#endif
}

static void cmd_midichan(const char *args, Print &out) {
    if (strcmp(args, "omni") == 0 || strcmp(args, "0") == 0) {
        gMidiChannel = 0;
        out.println(F("midichan -> omni (all channels)"));
    } else {
        int ch = atoi(args);
        if (ch < 1 || ch > 16) {
            out.println(F("usage: midichan <1-16|omni>"));
            return;
        }
        gMidiChannel = (uint8_t)ch;
        out.print(F("midichan -> "));
        out.println(ch);
    }
}

static void cmd_config(const char *args, Print &out) {
    if (strcmp(args, "save") == 0) {
        switch (configStore_save()) {
        case ConfigSaveResult::SAVED:
            out.println(F("config saved"));
            break;
        case ConfigSaveResult::UNCHANGED:
            out.println(F("config unchanged — no write needed"));
            break;
        case ConfigSaveResult::THROTTLED:
            out.println(F("config save throttled — wait 10s between saves"));
            break;
        }
    } else if (strcmp(args, "load") == 0) {
        if (configStore_load())
            out.println(F("config loaded"));
        else
            out.println(F("no saved config — using defaults"));
    } else if (strcmp(args, "reset") == 0) {
        configStore_reset();
        out.println(F("config wiped — defaults active on next boot"));
    } else {
        out.println(F("usage: config <save|load|reset>"));
    }
}

// Forward declaration — cmd_help calls commands_printHelp which needs the table,
// and the table needs cmd_help.  Define cmd_help after the table is declared.
static void cmd_help(const char *args, Print &out);

// CHORD-mode convenience shim: select a chord shape by name or index (0-10).
// Maps directly to gRelation so the same smoothing / caching path is used.
static void cmd_chord(const char *args, Print &out) {
    static const struct {
        const char *name;
        uint8_t idx;
    } kNames[] = {
        {"unison", 0},
        {"power", 1},
        {"minor", 2},
        {"major", 3},
        {"sus2", 4},
        {"sus4", 5},
        {"maj7", 6},
        {"min7", 7},
        {"dom7", 8},
        {"dim", 9},
        {"octaves", 10},
    };
    static const char *kLabels[] = {
        "unison",
        "power",
        "minor",
        "major",
        "sus2",
        "sus4",
        "maj7",
        "min7",
        "dom7",
        "dim",
        "octaves",
    };
    // match by name
    for (uint8_t i = 0; i < 11; i++) {
        if (strcasecmp(args, kNames[i].name) == 0) {
            gRelation = kNames[i].idx * 2.4f;
            out.print(F("chord -> "));
            out.print(kLabels[kNames[i].idx]);
            out.print(F("  (rel "));
            out.print(gRelation, 1);
            out.println(F(")"));
            return;
        }
    }
    // match by index 0-10
    char *end;
    const long idx = strtol(args, &end, 10);
    if (end != args && idx >= 0 && idx <= 10) {
        gRelation = (float)idx * 2.4f;
        out.print(F("chord -> "));
        out.print(kLabels[idx]);
        out.print(F("  (rel "));
        out.print(gRelation, 1);
        out.println(F(")"));
        return;
    }
    out.println(F("usage: chord <name|0-10>"));
    out.println(F("  names: unison power minor major sus2 sus4 maj7 min7 dom7 dim octaves"));
    out.println(F("  index:    0      1      2     3     4    5    6    7    8    9    10"));
    out.print(F("  current mode: "));
    out.println(gVoiceMode == VoiceMode::CHORD ? F("CHORD") : F("not CHORD — rel still set"));
}

// ---------------------------------------------------------------------------
// Command table — the single source of truth for all transports.
//
// Columns: name | help text shown in listing | handler
// ---------------------------------------------------------------------------

// clang-format off
const CommandEntry kCommands[] = {
    {"pitch",     "<hz>        base frequency (20-8000 Hz)",                              cmd_pitch},
    {"note",      "<note>      note name (e.g., C4, A#3)",                                cmd_note},
    {"mode",      "<pair|cloud|chord|cascade|string>  voice mode (default: pair)",        cmd_mode},
    {"rel",       "<0-24>      RELATION semitones: 0=unison  7=fifth  12=octave  24=2oct",  cmd_rel},
    {"detune",    "<hz>        symmetric fine spread (0-200 Hz)",                         cmd_detune},
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
    {"space",     "<0-2>       stereo width: 0=mono  1=full stereo  2=hyper-wide (default: 1)", cmd_space},
    {"chord",     "<name|0-10> set chord shape (CHORD mode): unison power minor major sus2 sus4 maj7 min7 dom7 dim octaves", cmd_chord},
    {"midichan",  "<1-16|omni> MIDI receive channel (default: omni)",                    cmd_midichan},
    {"config",    "<save|load|reset>  persist/restore all parameters to flash",          cmd_config},
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
