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
    out.print(F("  sys clock    : "));
    out.print(clock_get_hz(clk_sys) / 1000000u);
    out.println(F(" MHz  (expect 192)"));
    out.print(F("  MIDI channel : "));
    if(gMidiChannel == 0)
        out.println(F("omni"));
    else
        out.println(gMidiChannel);
    out.print(F("  echo max     : "));
    out.print((int)COIL_ECHO_MAX_S);
    out.println(F(" s"));
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
static void cmd_perf(const char *args, Print &out)
{
    gPerformancePrintEnabled = (*args != 'off');
    out.println(gPerformancePrintEnabled ? F("perf on") : F("perf off"));
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
    {"perf", "<on|off> - periodic CPU report", cmd_perf},
    {"cpu", "- one CPU report now", cmd_cpu},
#endif
    {"help", "- this list", cmd_help},
};

const uint8_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);
