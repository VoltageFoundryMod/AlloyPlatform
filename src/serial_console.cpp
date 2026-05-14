#ifdef SERIAL_CONTROL

#include "io/serial_console.h"
#include "io/commands.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Private state
// ---------------------------------------------------------------------------

static char sBuf[64];
static uint8_t sBufIdx = 0;
static bool sLastWasCR = false; // tracks CR so following LF is swallowed

// --- Command history -------------------------------------------------------
// Ring buffer of the last HISTORY_SIZE commands, navigable with arrow keys.

static constexpr uint8_t HISTORY_SIZE = 5;
static char sHistory[HISTORY_SIZE][64];
static uint8_t sHistoryCount = 0; // entries stored so far (saturates at HISTORY_SIZE)
static uint8_t sHistoryHead = 0;  // index where the NEXT entry will be written
static int8_t sHistoryPos = 0;    // browsing position: 0 = current (unsaved) line, 1 = most recent

// Push the current line into the ring buffer.
static void historyPush(const char *line) {
    if (line[0] == '\0')
        return;
    if (sHistoryCount > 0) {
        uint8_t last = (sHistoryHead + HISTORY_SIZE - 1) % HISTORY_SIZE;
        if (strcmp(sHistory[last], line) == 0)
            return; // skip duplicate of last entry
    }
    strncpy(sHistory[sHistoryHead], line, sizeof(sHistory[0]) - 1);
    sHistory[sHistoryHead][sizeof(sHistory[0]) - 1] = '\0';
    sHistoryHead = (sHistoryHead + 1) % HISTORY_SIZE;
    if (sHistoryCount < HISTORY_SIZE)
        sHistoryCount++;
}

// Retrieve history entry: offset 1 = most recent, 2 = one older, etc.
static const char *historyGet(uint8_t offset) {
    if (offset == 0 || offset > sHistoryCount)
        return nullptr;
    int8_t idx = (int8_t)sHistoryHead - (int8_t)offset;
    if (idx < 0)
        idx += HISTORY_SIZE;
    return sHistory[idx];
}

// Erase the current line on the terminal and replace it with newLine.
static void replaceCurrentLine(const char *newLine) {
    for (uint8_t i = 0; i < sBufIdx; i++)
        Serial.print(F("\x08 \x08"));
    strncpy(sBuf, newLine, sizeof(sBuf) - 1);
    sBuf[sizeof(sBuf) - 1] = '\0';
    sBufIdx = (uint8_t)strlen(sBuf);
    Serial.print(sBuf);
}

// --- VT100 escape sequence state machine -----------------------------------
// Handles ESC [ A (up) and ESC [ B (down); all other sequences are discarded.

enum EscState : uint8_t { ESC_NONE,
                          ESC_GOT_ESC,
                          ESC_GOT_BRACKET };
static EscState sEscState = ESC_NONE;

static void printPrompt() {
    Serial.print(F("> "));
}

// Called when the user presses Enter.  sBuf holds the typed line, null-terminated.
static void handleLine() {
    sBuf[sBufIdx] = '\0';

    // Trim leading whitespace
    const char *cmd = sBuf;
    while (*cmd == ' ')
        cmd++;

    historyPush(cmd); // save before reset
    sHistoryPos = 0;  // reset browsing position on execute
    sBufIdx = 0;

    if (cmd[0] == '\0' || cmd[0] == '?') {
        // Empty line or standalone '?' — show help
        commands_printHelp(Serial);
    } else {
        commands_dispatch(cmd, Serial);
    }
    printPrompt();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void serialConsole_init() {
    Serial.begin(115200);
    // Wait for USB CDC host to open the port so the banner is visible.
    // Timeout after 3 s so the module boots standalone without blocking.
    const uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) {
    }
    Serial.println(F("AlloyFlux — Juno-inspired Eurorack DCO"));
}

void serialConsole_ready() {
    commands_printHelp(Serial);
    printPrompt();
}

void serialConsole_update() {
    while (Serial.available()) {
        const char c = (char)Serial.read();

        // --- Escape sequence handling ------------------------------------
        if (sEscState == ESC_GOT_BRACKET) {
            sEscState = ESC_NONE;
            if (c == 'A') {
                // Arrow UP: go back in history
                int8_t next = sHistoryPos + 1;
                const char *entry = historyGet((uint8_t)next);
                if (entry) {
                    sHistoryPos = next;
                    replaceCurrentLine(entry);
                }
            } else if (c == 'B') {
                // Arrow DOWN: come forward in history
                int8_t next = sHistoryPos - 1;
                if (next <= 0) {
                    sHistoryPos = 0;
                    replaceCurrentLine("");
                } else {
                    const char *entry = historyGet((uint8_t)next);
                    if (entry) {
                        sHistoryPos = next;
                        replaceCurrentLine(entry);
                    }
                }
            }
            // Left/right/F-keys and other CSI sequences are silently dropped.
            continue;
        }
        if (sEscState == ESC_GOT_ESC) {
            sEscState = (c == '[') ? ESC_GOT_BRACKET : ESC_NONE;
            continue;
        }
        if (c == '\x1b') {
            sEscState = ESC_GOT_ESC;
            continue;
        }

        // --- Line endings -----------------------------------------------
        // Handle CR, LF, and CR+LF all correctly.
        // CR  : dispatch, set flag so following LF is swallowed.
        // LF after CR: swallow (already dispatched on CR).
        // LF alone : dispatch normally.

        if (c == '\r') {
            sLastWasCR = true;
            Serial.println(); // move cursor to next line on terminal
            handleLine();
            continue;
        }

        if (c == '\n') {
            if (sLastWasCR) {
                sLastWasCR = false; // LF paired with CR — already dispatched
                continue;
            }
            sLastWasCR = false;
            Serial.println();
            handleLine();
            continue;
        }

        sLastWasCR = false;

        // --- Backspace / DEL --------------------------------------------
        if (c == '\x08' || c == '\x7f') {
            if (sBufIdx > 0) {
                sBufIdx--;
                Serial.print(F("\x08 \x08")); // erase character on terminal
            }
            continue;
        }

        // --- Printable characters ---------------------------------------
        if (c >= 0x20 && c <= 0x7e) {
            if (sBufIdx < (uint8_t)(sizeof(sBuf) - 1)) {
                sBuf[sBufIdx++] = c;
                Serial.print(c); // local echo
            }
            // silently drop if buffer full
        }
    }
}

#endif // SERIAL_CONTROL
