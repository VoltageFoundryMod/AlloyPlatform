#ifdef SERIAL_CONTROL

#include "serial_console.h"
#include "debug.h"
#include "params.h"
#include <Arduino.h>

// Forward declaration — printHelp() is defined after the command table
// (it iterates over it), but cmd_help needs to call it.
static void printHelp();

// ---------------------------------------------------------------------------
// Command handlers
//
// Each handler receives `args`: the substring after "command ", or "" for
// commands that take no arguments.  atof() is used instead of sscanf(%f)
// to avoid pulling the heavy sscanf float implementation on newlib-nano.
// ---------------------------------------------------------------------------

static void cmd_pitch(const char *args) {
    gBaseFreq = constrain(atof(args), 20.0f, 8000.0f);
    Serial.print(F("pitch -> "));
    Serial.println(gBaseFreq, 2);
}

static void cmd_detune(const char *args) {
    gDetune = constrain(atof(args), 0.0f, 200.0f);
    Serial.print(F("detune -> "));
    Serial.println(gDetune, 2);
}

static void cmd_wave(const char *args) {
    gWaveform = constrain(atof(args), 0.0f, 1.0f);
    Serial.print(F("wave -> "));
    Serial.println(gWaveform, 3);
}

static void cmd_vol(const char *args) {
    gVolume = constrain(atof(args), 0.0f, 1.0f);
    Serial.print(F("vol -> "));
    Serial.println(gVolume, 3);
}

static void cmd_status(const char * /*args*/) {
    Serial.print(F("pitch="));
    Serial.print(gBaseFreq, 2);
    Serial.print(F(" detune="));
    Serial.print(gDetune, 2);
    Serial.print(F(" wave="));
    Serial.print(gWaveform, 3);
    Serial.print(F(" vol="));
    Serial.println(gVolume, 3);
}

static void cmd_help(const char * /*args*/) {
    printHelp();
}

// ---------------------------------------------------------------------------
// Command table
//
// Each entry: { "name", "<args>  description", handler }
//
// The dispatch loop matches cmd against `name` followed by a space (for
// commands that take an argument) or end-of-string (argument-less commands).
// To add a new command:
//   1. Write a static void cmd_xxx(const char* args) handler above.
//   2. Add one row here.
// ---------------------------------------------------------------------------

struct CommandEntry {
    const char *name;
    const char *help;
    void (*handler)(const char *args);
};

static const CommandEntry kCommands[] = {
    {"pitch", "<hz>   base frequency (20-8000 Hz)", cmd_pitch},
    {"detune", "<hz>   symmetric spread (0-200 Hz)", cmd_detune},
    {"wave", "<0-1>  waveform blend: 0=sine, 1=saw", cmd_wave},
    {"vol", "<0-1>  master volume", cmd_vol},
    {"status", "       print all current parameters", cmd_status},
    {"help", "       show this help", cmd_help},
};

static constexpr uint8_t kCommandCount =
    (uint8_t)(sizeof(kCommands) / sizeof(kCommands[0]));

// ---------------------------------------------------------------------------
// Private — print help listing (reused by init and unknown-command path)
// ---------------------------------------------------------------------------

static void printHelp() {
    Serial.println(F("Available commands:"));
    for (uint8_t i = 0; i < kCommandCount; i++) {
        Serial.print(F("  "));
        Serial.print(kCommands[i].name);
        Serial.print(' ');
        Serial.println(kCommands[i].help);
    }
}

// ---------------------------------------------------------------------------
// Private — print the command prompt
// ---------------------------------------------------------------------------

static void printPrompt() {
    Serial.print(F("> "));
}

// ---------------------------------------------------------------------------
// Private — dispatch a null-terminated command string
// ---------------------------------------------------------------------------

static void dispatch(const char *cmd) {
    for (uint8_t i = 0; i < kCommandCount; i++) {
        const char *name = kCommands[i].name;
        size_t nlen = strlen(name);

        // Match: cmd starts with name AND is followed by ' ' (has args) or '\0' (no args)
        if (strncmp(cmd, name, nlen) == 0 &&
            (cmd[nlen] == ' ' || cmd[nlen] == '\0')) {
            const char *args = (cmd[nlen] == ' ') ? cmd + nlen + 1 : "";
            kCommands[i].handler(args);
            printPrompt();
            return;
        }
    }
    // Unknown command — show help
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
    printHelp();
    printPrompt();
}

// ---------------------------------------------------------------------------
// Private state — line buffer
// ---------------------------------------------------------------------------

static char sBuf[64];
static uint8_t sBufIdx = 0;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void serialConsole_init() {
    Serial.begin(115200);
    // Wait for the USB CDC host to open the port so the banner is visible.
    // Timeout after ~3 s so the module doesn't hang when powered standalone.
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) {
    }
    Serial.println(F("AlloyFlux - Juno-inspired Eurorack DCO"));
}

void serialConsole_ready() {
    printHelp();
    printPrompt();
}

void serialConsole_update() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        // If newline and the buffer is empty (user just hit enter), reprint the prompt.
        if ((c == '\n' || c == '\r') && sBufIdx == 0) {
            printPrompt();
        }
        // If newline, dispatch command; else add to buffer if space allows.
        if (c == '\n' || c == '\r') {
            if (sBufIdx > 0) {
                sBuf[sBufIdx] = '\0';
                dispatch(sBuf);
                sBufIdx = 0;
            }
        } else if (sBufIdx < (uint8_t)(sizeof(sBuf) - 1)) {
            sBuf[sBufIdx++] = c;
        }
    }
}

#endif // SERIAL_CONTROL
