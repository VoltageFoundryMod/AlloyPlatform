#ifdef SERIAL_CONTROL

#include "serial_console.h"
#include "commands.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Private state
// ---------------------------------------------------------------------------

static char sBuf[64];
static uint8_t sBufIdx = 0;
static bool sLastWasCR = false; // tracks CR so following LF is swallowed

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

static void printPrompt() {
    Serial.print(F("> "));
}

// Called when the user presses Enter.  sBuf holds the typed line, null-terminated.
static void handleLine() {
    sBuf[sBufIdx] = '\0';
    sBufIdx = 0;

    // Trim leading whitespace
    const char *cmd = sBuf;
    while (*cmd == ' ')
        cmd++;

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
