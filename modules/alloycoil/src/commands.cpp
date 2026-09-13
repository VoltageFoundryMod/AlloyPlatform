// Alloy Coil's serial command table.
//
// Deliberately thin compared to AlloyFlux's: every parameter is in the
// generated manifest, so one `set` / `get` pair driven by that table covers all
// of them and stays correct when params.json changes. Only the things the
// manifest cannot express get their own command.

#include "io/commands.h"
#include "coil_config.h"
#include "config_store.h" // platform: save/load/reset
#include "io/param_map.h"
#include "io/ble_midi.h" // bleMidi_stateName()
#include "io/usb_midi.h" // gMidiChannel
#include "params.h"
#include <Arduino.h>
#include <hardware/clocks.h> // clock_get_hz — see cmd_status
#include <stdlib.h>
#include <string.h>

static void printParam(const ParamDescriptor &p, Print &out)
{
    out.print(F("  "));
    out.print(p.name);
    out.print(F(" = "));
    out.print(*p.target, 4);
    if(p.unit)
    {
        out.print(' ');
        out.print(p.unit);
    }
    out.print(F("   ["));
    out.print(p.minVal, 4);
    out.print(F(" .. "));
    out.print(p.maxVal, 4);
    out.println(F("]"));
}

static void cmd_set(const char *args, Print &out)
{
    // "set <name> <value>"
    char        name[24] = {0};
    const char *sp       = strchr(args, ' ');
    if(!sp || (size_t)(sp - args) >= sizeof(name))
    {
        out.println(F("usage: set <name> <value>   (see 'get' for names)"));
        return;
    }
    memcpy(name, args, (size_t)(sp - args));

    const ParamDescriptor *p = paramMap_findByName(name);
    if(!p)
    {
        out.print(F("unknown parameter: "));
        out.println(name);
        return;
    }
    float v = (float)atof(sp + 1);
    if(v < p->minVal)
        v = p->minVal;
    if(v > p->maxVal)
        v = p->maxVal;
    *p->target = v;
    out.print(F("set "));
    out.print(p->name);
    out.print(F(" -> "));
    out.println(v, 4);
}

static void cmd_get(const char *args, Print &out)
{
    if(*args)
    {
        const ParamDescriptor *p = paramMap_findByName(args);
        if(!p)
        {
            out.print(F("unknown parameter: "));
            out.println(args);
            return;
        }
        printParam(*p, out);
        return;
    }
    out.println(F("parameters:"));
    for(uint8_t i = 0; i < kParamCount; i++)
        printParam(kParamTable[i], out);
}

static void cmd_status(const char *, Print &out)
{
    out.println(F("Alloy Coil — feedback resonator"));
    // First line on purpose: if the bit clock never started, everything below
    // is still live (control falls back to a millis() pace, so MIDI keeps
    // working) and it is easy to mistake a dead audio path for a silent patch.
    out.print(F("  audio        : "));
    out.println(audioRunning() ? F("running") : F("NOT RUNNING — I2S failed"));
#ifdef CPU_PROFILE
    out.print(F("  block        : "));
    out.print(gAudioElapsedUs);
    out.print(F("us / "));
    out.print(gAudioBudgetUs);
    out.print(F("us   underruns "));
    out.println(gAudioOverruns);
#endif
    // The engine does not fit in the block budget at the RP2350's stock
    // 150 MHz — that is the whole reason platformio.ini asks for 192. If this
    // reads 150, no amount of DSP accounting will explain the block time.
    // Compared against this build's own F_CPU rather than a hardcoded 192:
    // the wireless env runs at 230.4 MHz (M78f), so a fixed "expect 192" read
    // as a fault on a board that was perfectly correct.
    out.print(F("  sys clock    : "));
    out.print(clock_get_hz(clk_sys) / 1000000u);
    out.print(F(" MHz  (expect "));
    out.print((uint32_t)(F_CPU / 1000000u));
    out.println(F(")"));
    out.print(F("  MIDI channel : "));
    if(gMidiChannel == 0)
        out.println(F("omni"));
    else
        out.println(gMidiChannel);
    // Both halves of the echo's storage bargain, because either can be
    // overridden at build time (`make ... ECHO_S=n ECHO_DECIM=n`) and a board
    // that cannot say which configuration it is running is not one you can
    // A/B against. The derived numbers are the ones that matter to the ear:
    // the loop's own rate, and the anti-alias cutoff at rate x 0.25.
    out.print(F("  echo         : "));
    out.print((int)COIL_ECHO_MAX_S);
    out.print(F(" s max, /"));
    out.print((int)COIL_ECHO_DECIMATION);
    out.print(F(" = "));
    out.print(48000 / (int)COIL_ECHO_DECIMATION);
    out.print(F(" Hz, AA "));
    out.print(48000 / (int)COIL_ECHO_DECIMATION / 4);
    out.print(F(" Hz, "));
    // Exactly what the buffer costs: rate x 2 bytes x 2 channels x seconds.
    out.print((48000 / (int)COIL_ECHO_DECIMATION) * 4 * (int)COIL_ECHO_MAX_S
              / 1024);
    out.println(F(" KiB"));
    out.print(F("  smoother     : "));
    if(gSmoothFrames == 0)
        out.println(F("OFF"));
    else
    {
        out.print(gSmoothFrames);
        out.print(F(" frames ("));
        out.print(48000.0f / (float)gSmoothFrames, 1);
        out.println(F(" Hz)"));
    }
    out.print(F("  limiter      : "));
    out.println(gLimiterEnabled ? F("on") : F("OFF"));
    // Live button state, for bring-up: hold a switch and re-run `status`. Both
    // are active-low with an internal pull-up, so a pin shorted to ground reads
    // permanently down and an unconnected one permanently up.
    {
        const uint8_t b = coilButtonsDown();
        out.print(F("  buttons      : WARP "));
        out.print((b & 0x1) ? F("down") : F("up"));
        out.print(F("   SHIFT "));
        out.println((b & 0x2) ? F("down") : F("up"));
    }
    // The *flag*, not the switch. Since WARP became a toggle the two can
    // disagree for as long as you like — the button reads up while warp is on
    // — and a latched warp is a state the hardware never sustained while the
    // button was momentary. It halves the echo time, so it changes what the
    // delay line is doing; if a block-time mystery shows up, look here before
    // anywhere else. CC 20 and the Alloy Controller set the same flag.
    out.print(F("  warp         : "));
    out.println(gCoilParams.warp ? F("ON (echo time halved)") : F("off"));
    cmd_get("", out);
}

static void cmd_save(const char *args, Print &out)
{
    const uint8_t slot = (*args) ? (uint8_t)atoi(args) : 0;
    switch(configStore_save(slot))
    {
        case ConfigSaveResult::SAVED: out.println(F("saved")); break;
        case ConfigSaveResult::UNCHANGED: out.println(F("unchanged")); break;
        case ConfigSaveResult::THROTTLED:
            out.println(F("throttled (10 s rate limit on slot 0)"));
            break;
    }
}

static void cmd_load(const char *args, Print &out)
{
    const uint8_t slot = (*args) ? (uint8_t)atoi(args) : 0;
    const bool    ok   = configStore_load(slot);
    // A recall changes every parameter at once. Gliding into it would take a
    // second on the feedback body alone, which is a smear rather than a
    // crossfade — so land on the preset and start from there.
    if(ok)
        coilControlSnap();
    out.println(ok ? F("loaded") : F("no valid preset"));
}

static void cmd_reset(const char *args, Print &out)
{
    const uint8_t slot = (*args) ? (uint8_t)atoi(args) : 0;
    configStore_reset(slot);
    configStore_applyDefaults();
    coilControlSnap();
    out.println(F("reset to defaults"));
}

#ifdef CPU_PROFILE
// Bare `perf` toggles; `perf on` / `perf off` set it explicitly. The old test
// compared one char against the multi-character literal 'off', which can never
// match — so `perf off` left the 5 s report running.
static void cmd_perf(const char *args, Print &out)
{
    while(*args == ' ')
        args++;
    if(*args == '\0')
        gPerformancePrintEnabled = !gPerformancePrintEnabled;
    else if(strncasecmp(args, "off", 3) == 0)
        gPerformancePrintEnabled = false;
    else if(strncasecmp(args, "on", 2) == 0)
        gPerformancePrintEnabled = true;
    else
    {
        out.println(F("usage: perf [on|off]   (bare perf toggles)"));
        return;
    }
    out.println(gPerformancePrintEnabled ? F("perf on") : F("perf off"));
}

// M78 — bring-up for the experimental wireless Coil. Prints "not-built" on
// every env but alloycoil_w, and "no-radio" on a board whose CYW43 did not
// answer. `ble pair` reopens the 60 s advertising window that init() opened.
static void cmd_ble(const char *args, Print &out)
{
    if(strncasecmp(args, "pair", 4) == 0)
        bleMidi_startPairing();
    else if(strncasecmp(args, "off", 3) == 0)
        bleMidi_stopPairing();
    else if(strncasecmp(args, "poll ", 5) == 0)
        bleMidi_setPolling(args[5] != '0');
    else if(*args != '\0')
    {
        out.println(F("usage: ble [pair|off|poll 0|poll 1]"));
        return;
    }
    out.print(F("ble -> "));
    out.print(bleMidi_stateName());
    out.print(F("  poll "));
    out.println(bleMidi_polling() ? F("on") : F("OFF (diagnostic)"));
}

static void cmd_cpu(const char *, Print &out)
{
    const float budget = (float)gAudioBudgetUs;
    out.print(F("[cpu] "));
    out.print(gAudioElapsedUs);
    out.print(F("us/"));
    out.print(gAudioBudgetUs);
    out.print(F("us  headroom "));
    out.print((budget - (float)gAudioElapsedUs) / budget * 100.0f, 1);
    out.print(F("%  underruns "));
    out.print(gAudioOverruns);
    out.print(F("  set/step "));
    out.print(coilSmootherPushes());
    out.println(F("/12"));
}
#endif

// --- Audio-path cost bisect (M63i) -----------------------------------------
// Both of these exist to attribute block time on a live board. See params.h.

static void cmd_smooth(const char *args, Print &out)
{
    if(*args)
        gSmoothFrames = (uint32_t)atoi(args);
    out.print(F("smooth = "));
    if(gSmoothFrames == 0)
    {
        out.println(F("0 (OFF — parameters frozen, measurement only)"));
        return;
    }
    out.print(gSmoothFrames);
    out.print(F(" frames = "));
    out.print(48000.0f / (float)gSmoothFrames, 1);
    out.println(F(" Hz"));
}

static void cmd_limiter(const char *args, Print &out)
{
    if(*args)
        gLimiterEnabled = (*args != '0');
    out.println(gLimiterEnabled ? F("limiter = on")
                                : F("limiter = OFF (output will hard-clamp)"));
}

static void cmd_help(const char *, Print &out)
{ commands_printHelp(out); }

const CommandEntry kCommands[] = {
    {"set", "<name> <value> - set a parameter", cmd_set},
    {"get", "[name] - show one parameter, or all", cmd_get},
    {"status", "- module state summary", cmd_status},
    {"save", "[slot] - save preset (0 = live)", cmd_save},
    {"load", "[slot] - load preset (0 = live)", cmd_load},
    {"reset", "[slot] - wipe preset and restore defaults", cmd_reset},
    {"smooth", "[n] - smoother interval in frames (0 = off)", cmd_smooth},
    {"limiter", "[0|1] - output limiter on/off", cmd_limiter},
#ifdef CPU_PROFILE
    {"perf", "[on|off] - periodic CPU report (bare toggles)", cmd_perf},
    {"cpu", "- one CPU report now", cmd_cpu},
#endif
    {"ble", "[pair|off|poll 0|1]  BLE state; poll 0 = cost bisect", cmd_ble},
    {"help", "- this list", cmd_help},
};

const uint8_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);
