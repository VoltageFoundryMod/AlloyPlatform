#include "io/commands.h"
#include "alloy_config.h"
#include "config_store.h" // platform: save/load/reset
#include "dsp/CurveEngine.h"
#include "dsp/DelayEngine.h" // DELAY_MAX_MS
#include "dsp/FilterEngine.h"
#include "dsp/FxChain.h"
#include "dsp/OTALadder.h"
#include "dsp/ReverbEngine.h"
#include "io/param_map.h"
#include "io/usb_midi.h" // gMidiChannel
#include "params.h"
#include "scale_quantizer.h"
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

// Defined further down with cmd_pot; declared here for cmd_status.
static const __FlashStringHelper *potTakeoverName(PotTakeoverMode m);

static void cmd_pitch(const char *args, Print &out)
{
    gBaseFreq = constrain((float)atof(args), 20.0f, 8000.0f);
    out.print(F("pitch -> "));
    out.println(gBaseFreq, 2);
}

static void cmd_note(const char *args, Print &out)
{
    // Simple note name parser: letter (A-G), optional accidental (# or b), octave number.
    // C4 = 261.63 Hz, A4 = 440 Hz, etc.
    if(strlen(args) < 2 || strlen(args) > 4)
    {
        out.println(F("invalid note format"));
        return;
    }
    char letter   = args[0];
    int  semitone = 0;
    // chromatic semitone offsets from C for A B C D E F G
    static const int8_t kNoteMap[] = {9, 11, 0, 2, 4, 5, 7};
    if(letter >= 'A' && letter <= 'G')
    {
        semitone = kNoteMap[letter - 'A'];
    }
    else
    {
        out.println(F("invalid note letter"));
        return;
    }
    int idx = 1;
    if(args[idx] == '#' || args[idx] == 'b')
    {
        semitone += (args[idx] == '#') ? 1 : -1;
        idx++;
    }
    int   octave = atoi(&args[idx]);
    float freq   = 16.3516f * powf(2.0f, octave + semitone / 12.0f);
    gBaseFreq    = constrain(freq, 20.0f, 8000.0f);
    out.print(F("note -> "));
    out.print(args);
    out.print(F(" ("));
    out.print(gBaseFreq, 2);
    out.println(F(" Hz)"));
}

static void cmd_color(const char *args, Print &out)
{
    gColor = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("color -> "));
    out.println(gColor, 3);
}

static void cmd_shape(const char *args, Print &out)
{
    gShape = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("shape -> "));
    out.println(gShape, 3);
}

static void cmd_fat(const char *args, Print &out)
{
    gFatness = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("fat -> "));
    out.println(gFatness, 3);
}

static void cmd_sub_octave(const char *args, Print &out)
{
    const int v = atoi(args);
    if(v != 1 && v != 2)
    {
        out.println(F("sub octave: 1 or 2"));
        return;
    }
    gSubOctave = (uint8_t)v;
    out.print(F("sub octave -> "));
    out.println(gSubOctave);
}

static void cmd_motion(const char *args, Print &out)
{
    gMotion = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("motion -> "));
    out.println(gMotion, 3);
}

static void cmd_curve(const char *args, Print &out)
{
    gCurve = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("curve -> "));
    out.println(gCurve, 3);
}

static void cmd_curvetime(const char *args, Print &out)
{
    gCurveTime = constrain((float)atof(args), 0.25f, 4.0f);
    out.print(F("curvetime -> "));
    out.println(gCurveTime, 2);
}

static void cmd_gate(const char *args, Print &out)
{
    if(strcmp(args, "free") == 0)
    {
        // Same path CC 119 and MODE+SHIFT take — clears the slots too, which
        // CLOUD needs or the drone returns underneath a phantom chord.
        returnToDrone();
        out.println(F("gate -> free (drone)"));
    }
    else
    {
        gGatePatched = true;
        gGateHigh    = (atoi(args) != 0);
        out.print(F("gate -> "));
        out.println(gGateHigh ? F("high") : F("low"));
    }
}

extern uint32_t sTrigReleaseAt; // defined in main.cpp
extern uint8_t  sTrigPolySlot;  // defined in main.cpp — 255 = not set

void doTrig(uint32_t durMs)
{
    if(modeUsesPolySlots(gVoiceMode))
    {
        const float freq    = constrain(gBaseFreq, 20.0f, 8000.0f);
        const float subMult = (gSubOctave == 2) ? 0.25f : 0.5f;
        // 1.0 velocity — a trig is always full level.
        // 254 tags the slot as owned by this pulse rather than a held note.
        sTrigPolySlot
            = polyNoteOn(freq, 1.0f, subMult, 254, polySlotLimit(gVoiceMode));
        // CLOUD needs the latch armed or the drone plays straight over the
        // trigged note; POLY ignores it. Same rule as a MIDI note on.
        gGatePatched = true;
    }
    else
    {
        gGatePatched  = true;
        gGateHigh     = true;
        sTrigPolySlot = 255; // not poly
    }
    sTrigReleaseAt = millis() + durMs;
}

static void cmd_trig(const char *args, Print &out)
{
    const uint32_t dur = (*args != '\0') ? (uint32_t)atoi(args) : 100u;
    doTrig(dur);
    out.print(F("trig -> "));
    out.print(dur);
    out.println(F("ms"));
}

static void cmd_driftspeed(const char *args, Print &out)
{
    gDriftSpeed = constrain((float)atof(args), 0.001f, 0.10f);
    out.print(F("driftspeed -> "));
    out.println(gDriftSpeed, 4);
}

// CLOUD's oscillator pool (M78). Two numbers, deliberately left adjustable at
// runtime: twelve oscillators is where the mode is *known* to fit alongside
// every effect, but the only honest way to find out whether fifteen also fits
// on a given clock and effect chain is to set it and watch. Run `stats` for a
// while afterwards and read the *deltas* on `slow-blk` and `overruns`, not the
// totals — a static overrun count is a mode-switch transient, not a fault.
//
// Width per note falls out of the two and is printed for that reason: it is
// the number that actually describes what you will hear.
static void cmd_cloud(const char *args, Print &out)
{
    if(strncasecmp(args, "pool", 4) == 0)
        gCloudPool = (uint8_t)constrain(atoi(args + 4), 1, (int)kCloudOscMax);
    else if(strncasecmp(args, "notes", 5) == 0)
        gCloudMaxNotes
            = (uint8_t)constrain(atoi(args + 5), 1, (int)kCloudMaxNotes);
    else if(*args != '\0')
    {
        out.println(F("usage: cloud [pool <n> | notes <n>]"));
        return;
    }

    out.print(F("cloud -> pool "));
    out.print(gCloudPool);
    out.print(F("  notes "));
    out.print(gCloudMaxNotes);
    out.print(F("  saws/note "));
    // Mirrors SynthEngine::_cloudWidthFor() — widest odd stack that fits.
    for(uint8_t n = 1; n <= gCloudMaxNotes; n++)
    {
        uint8_t w = 7u;
        while(w > 1u && (uint16_t)w * (uint16_t)n > (uint16_t)gCloudPool)
            w -= 2u;
        out.print(w);
        if(n < gCloudMaxNotes)
            out.print('/');
    }
    out.println();
}

static void cmd_rel(const char *args, Print &out)
{
    gRelation = constrain((float)atof(args), 0.0f, 24.0f);
    out.print(F("rel -> "));
    out.print(gRelation, 1);
    out.println(F(" st"));
}

static void cmd_mode(const char *args, Print &out)
{
    const struct
    {
        const char *name;
        VoiceMode   mode;
        bool        active;
    } modes[] = {
        {"pair", VoiceMode::PAIR, true},
        {"cloud", VoiceMode::CLOUD, true},
        {"chord", VoiceMode::CHORD, true},
        {"cascade", VoiceMode::CASCADE, false},
        {"string", VoiceMode::STRING, false},
        {"plasma", VoiceMode::PLASMA, true},
        {"poly", VoiceMode::POLY, true},
    };
    for(uint8_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
    {
        if(strcasecmp(args, modes[i].name) == 0)
        {
            if(!modes[i].active)
            {
                out.print(F("mode -> "));
                out.print(modes[i].name);
                out.println(F(" (not yet active)"));
                // Still store it so it shows in status for planning purposes
            }
            else
            {
                out.print(F("mode -> "));
                out.println(modes[i].name);
            }
            gVoiceMode = modes[i].mode;
            return;
        }
    }
    out.println(F("usage: mode <pair|cloud|chord|cascade|string|poly>"));
    out.print(
        F("active modes: pair cloud chord cascade string poly  current: "));
    out.println(voiceModeName(gVoiceMode));
}

static void cmd_chorus(const char *args, Print &out)
{
    if(strcmp(args, "off") == 0 || strcmp(args, "0") == 0)
    {
        gChorusMode = ChorusMode::OFF;
        out.println(F("chorus -> off"));
    }
    else if(strcmp(args, "I") == 0 || strcmp(args, "1") == 0)
    {
        gChorusMode = ChorusMode::I;
        out.println(F("chorus -> I (slow, 0.51 Hz)"));
    }
    else if(strcmp(args, "II") == 0 || strcmp(args, "2") == 0)
    {
        gChorusMode = ChorusMode::II;
        out.println(F("chorus -> II (fast, 0.62 Hz)"));
    }
    else if(strcmp(args, "I+II") == 0 || strcmp(args, "3") == 0)
    {
        gChorusMode = ChorusMode::I_II;
        out.println(F("chorus -> I+II (L=slow, R=fast)"));
    }
    else
    {
        out.println(F("usage: chorus <off|I|II|I+II>"));
    }
}

static void cmd_vol(const char *args, Print &out)
{
    gVolume = constrain((float)atof(args), 0.0f, 1.0f);
    out.print(F("vol -> "));
    out.println(gVolume, 3);
}

static void cmd_space(const char *args, Print &out)
{
    gSpace = constrain((float)atof(args), 0.0f, 2.0f);
    out.print(F("space -> "));
    out.println(gSpace, 3);
}

static void cmd_status(const char * /*args*/, Print &out)
{
    out.print(F("pitch="));
    out.print(gBaseFreq, 2);
    out.print(F(" mode="));
    out.print(voiceModeName(gVoiceMode));
    out.print(F(" rel="));
    out.print(gRelation, 3);
    out.print(F(" color="));
    out.print(gColor, 3);
    out.print(F(" shape="));
    out.print(gShape, 3);
    out.print(F(" fat="));
    out.print(gFatness, 3);
    out.print(F(" suboct="));
    out.print(gSubOctave);
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
    switch(gChorusMode)
    {
        case ChorusMode::OFF: out.print(F("off")); break;
        case ChorusMode::I: out.print(F("I")); break;
        case ChorusMode::II: out.print(F("II")); break;
        case ChorusMode::I_II: out.print(F("I+II")); break;
    }
    out.print(F(" vol="));
    out.print(gVolume, 3);
    out.print(F(" space="));
    out.print(gSpace, 3);
    out.print(F(" midichan="));
    if(gMidiChannel == 0)
        out.print(F("omni"));
    else
        out.print(gMidiChannel);
    out.print(F(" veloc="));
    out.print(gVelocitySensitive ? F("on") : F("off"));
    out.print(F(" glide="));
    out.print(gGlideEnabled ? F("on") : F("off"));
    if(gGlideEnabled)
    {
        out.print(F(" glidetime="));
        out.print(gGlideTime, 3);
    }
    out.print(F(" scale="));
    out.print(kScaleNames[(uint8_t)gQuantizeScale]);
    if(gTranspose != 0)
    {
        out.print(F(" transpose="));
        out.print(gTranspose);
    }
    out.print(F(" pot="));
    out.print(potTakeoverName(gPotTakeoverMode));
    out.println();

    // Line 2 — post-effects state
    out.print(F("filter="));
    switch(gFilterMode)
    {
        case FilterMode::OFF: out.print(F("off")); break;
        case FilterMode::LP: out.print(F("lp")); break;
        case FilterMode::HP: out.print(F("hp")); break;
        case FilterMode::BP: out.print(F("bp")); break;
        case FilterMode::NOTCH: out.print(F("notch")); break;
    }
    if(gFilterMode != FilterMode::OFF)
    {
        out.print(F(" cut="));
        out.print(gFilterCutoff, 0);
        out.print(F(" res="));
        out.print(gFilterRes, 2);
    }
    out.print(F(" fxorder=filter:"));
    out.print(gFxOrder.filterPostChorus ? F("post") : F("pre"));
    out.print(F(",delay:"));
    out.print(gFxOrder.delayPostReverb ? F("post") : F("pre"));
    out.print(F(" ftype="));
    out.print(gFilterType == FilterType::SVF ? F("svf") : F("ladder"));
    out.print(F(" env="));
    out.print(gEnvelopeType == EnvelopeType::AR ? F("ar") : F("adsr"));
    if(gEnvelopeType == EnvelopeType::ADSR)
    {
        out.print(F(" a="));
        out.print(gAdsrAttack, 3);
        out.print(F(" d="));
        out.print(gAdsrDecay, 3);
        out.print(F(" s="));
        out.print(gAdsrSustain, 2);
        out.print(F(" r="));
        out.print(gAdsrRelease, 3);
    }
    out.print(F(" reverb="));
    if(!gRevEnabled)
    {
        out.print(F("off"));
    }
    else
    {
        out.print(F("on mix="));
        out.print(gRevMix, 2);
        out.print(F(" size="));
        out.print(gRevSize, 2);
        out.print(F(" damp="));
        out.print(gRevDamping, 2);
    }
    out.print(F(" delay="));
    if(gDelayMix < 0.001f)
    {
        out.println(F("off"));
    }
    else
    {
        out.print(F("on mix="));
        out.print(gDelayMix, 2);
        out.print(F(" time="));
        out.print(gDelayTime, 0);
        out.print(F("ms fb="));
        out.println(gDelayFeedback, 2);
    }
}

static void cmd_performance_print(const char *args, Print &out)
{
#ifdef CPU_PROFILE
    if(strcmp(args, "on") == 0)
    {
        gPerformancePrintEnabled = true;
        out.println(F("CPU profiling print enabled"));
    }
    else if(strcmp(args, "off") == 0)
    {
        gPerformancePrintEnabled = false;
        out.println(F("CPU profiling print disabled"));
    }
    else
    {
        out.println(F("usage: performance on|off"));
    }
#else
    out.println(F("CPU_PROFILE not active — add -DCPU_PROFILE to build_flags"));
#endif
}

static void cmd_cpu(const char * /*args*/, Print &out)
{
#ifdef CPU_PROFILE
    const uint32_t us     = gAudioElapsedUs;
    const uint32_t over   = gAudioOverruns;
    const float    budget = (float)gAudioBudgetUs;
    // M63a — budget is one audio block's wall-clock period, published by
    // AudioDriver, not the old fixed 30 µs per sample.
    const float    headroom = (budget - (float)us) / budget * 100.0f;
    const uint32_t upSec    = millis() / 1000;
    // uptime mm:ss
    const uint32_t mm = upSec / 60;
    const uint32_t ss = upSec % 60;
    // delta overruns since last cpu call — reveals if overruns are happening
    // right now (real problem) vs. just accumulated at startup/during serial TX
    static uint32_t lastOver = 0;
    const uint32_t  delta    = over - lastOver;
    lastOver                 = over;
    out.print(F("audio block: "));
    out.print(us);
    out.print(F("us / "));
    out.print(gAudioBudgetUs);
    out.print(F("us  headroom: "));
    out.print(headroom, 1);
    out.print(F("%  overruns: "));
    out.print(over);
    out.print(F(" (+"));
    out.print(delta);
    out.print(F(" since last)  uptime: "));
    if(mm < 10)
        out.print('0');
    out.print(mm);
    out.print(':');
    if(ss < 10)
        out.print('0');
    out.println(ss);
#else
    out.println(F("CPU_PROFILE not active — add -DCPU_PROFILE to build_flags"));
#endif
}

static void cmd_veloc(const char *args, Print &out)
{
    if(strcmp(args, "on") == 0 || strcmp(args, "1") == 0)
    {
        gVelocitySensitive = true;
        out.println(F("veloc -> on"));
    }
    else if(strcmp(args, "off") == 0 || strcmp(args, "0") == 0)
    {
        gVelocitySensitive = false;
        gMidiVelocity      = 1.0f; // restore immediately
        out.println(F("veloc -> off"));
    }
    else
    {
        out.print(F("veloc: "));
        out.println(gVelocitySensitive ? F("on") : F("off"));
    }
}

static void cmd_glide(const char *args, Print &out)
{
    if(strcmp(args, "on") == 0 || strcmp(args, "1") == 0)
    {
        gGlideEnabled = true;
        out.println(F("glide -> on"));
    }
    else if(strcmp(args, "off") == 0 || strcmp(args, "0") == 0)
    {
        gGlideEnabled = false;
        out.println(F("glide -> off"));
    }
    else
    {
        out.print(F("glide: "));
        out.println(gGlideEnabled ? F("on") : F("off"));
    }
}

static void cmd_glidetime(const char *args, Print &out)
{
    if(*args)
    {
        gGlideTime = constrain((float)atof(args), 0.0f, 2.0f);
        out.print(F("glidetime -> "));
        out.println(gGlideTime, 3);
    }
    else
    {
        out.print(F("glidetime: "));
        out.println(gGlideTime, 3);
    }
}

static void cmd_scale(const char *args, Print &out)
{
    if(*args == '\0')
    {
        out.print(F("scale: "));
        out.println(kScaleNames[(uint8_t)gQuantizeScale]);
        return;
    }
    // "off" and "chromatic" both mean CHROMATIC (bypass).
    if(strcmp(args, "off") == 0 || strcmp(args, "chromatic") == 0)
    {
        gQuantizeScale = ScaleId::CHROMATIC;
        out.println(F("scale -> chromatic (off)"));
        return;
    }
    for(uint8_t i = 0; i < (uint8_t)ScaleId::COUNT; i++)
    {
        if(strcmp(args, kScaleNames[i]) == 0)
        {
            gQuantizeScale = (ScaleId)i;
            out.print(F("scale -> "));
            out.println(kScaleNames[i]);
            return;
        }
    }
    out.print(F("unknown scale; options: "));
    for(uint8_t i = 0; i < (uint8_t)ScaleId::COUNT; i++)
    {
        out.print(kScaleNames[i]);
        if(i + 1 < (uint8_t)ScaleId::COUNT)
            out.print(' ');
    }
    out.println();
}

static void cmd_transpose(const char *args, Print &out)
{
    if(*args)
    {
        int v = atoi(args);
        if(v < -24 || v > 24)
        {
            out.println(F("usage: transpose <-24..24>"));
            return;
        }
        gTranspose = (int8_t)v;
        out.print(F("transpose -> "));
        out.println(gTranspose);
    }
    else
    {
        out.print(F("transpose: "));
        out.println(gTranspose);
    }
}

static void cmd_midichan(const char *args, Print &out)
{
    if(strcmp(args, "omni") == 0 || strcmp(args, "0") == 0)
    {
        gMidiChannel = 0;
        out.println(F("midichan -> omni (all channels)"));
    }
    else
    {
        int ch = atoi(args);
        if(ch < 1 || ch > 16)
        {
            out.println(F("usage: midichan <1-16|omni>"));
            return;
        }
        gMidiChannel = (uint8_t)ch;
        out.print(F("midichan -> "));
        out.println(ch);
    }
}

static void cmd_config(const char *args, Print &out)
{
    // Parse sub-command and optional slot: "save [1-9]", "load [1-9]", "reset [all]"
    char sub[16]    = {};
    char slotStr[8] = {};
    sscanf(args, "%15s %7s", sub, slotStr);

    uint8_t slot = 0;
    if(slotStr[0] >= '1' && slotStr[0] <= '9' && slotStr[1] == '\0')
        slot = (uint8_t)(slotStr[0] - '0');

    if(strcmp(sub, "save") == 0)
    {
        switch(configStore_save(slot))
        {
            case ConfigSaveResult::SAVED:
                if(slot == 0)
                    out.println(F("config saved (live slot)"));
                else
                {
                    out.print(F("preset "));
                    out.print(slot);
                    out.println(F(" saved"));
                }
                break;
            case ConfigSaveResult::UNCHANGED:
                out.println(F("config unchanged — no write needed"));
                break;
            case ConfigSaveResult::THROTTLED:
                out.println(
                    F("config save throttled — wait 10s between saves"));
                break;
        }
    }
    else if(strcmp(sub, "load") == 0)
    {
        if(configStore_load(slot))
        {
            if(slot == 0)
                out.println(F("config loaded (live slot)"));
            else
            {
                out.print(F("preset "));
                out.print(slot);
                out.println(F(" loaded"));
            }
        }
        else
        {
            if(slot == 0)
                out.println(F("no saved config — using defaults"));
            else
            {
                out.print(F("preset "));
                out.print(slot);
                out.println(F(" empty"));
            }
        }
    }
    else if(strcmp(sub, "reset") == 0)
    {
        if(strcmp(slotStr, "all") == 0)
        {
            configStore_reset(255);
            configStore_applyDefaults();
            out.println(F("all presets wiped — factory defaults applied"));
        }
        else
        {
            configStore_reset(slot);
            if(slot == 0)
            {
                configStore_applyDefaults();
                out.println(F("live config reset to factory defaults"));
            }
            else
            {
                out.print(F("preset "));
                out.print(slot);
                out.println(F(" wiped"));
            }
        }
    }
    else
    {
        out.println(F("usage: config <save|load|reset> [1-9|all]"));
        out.println(F("  slot 0 (default) = live auto-save state"));
        out.println(F("  slots 1-9        = user presets"));
        out.println(F("  reset all        = wipe all slots"));
    }
}

// Forward declaration — cmd_help calls commands_printHelp which needs the table,
// and the table needs cmd_help.  Define cmd_help after the table is declared.
static void cmd_help(const char *args, Print &out);

// CHORD-mode convenience shim: select a chord shape by name or index (0-10).
// Maps directly to gRelation so the same smoothing / caching path is used.
static void cmd_chord(const char *args, Print &out)
{
    static const struct
    {
        const char *name;
        uint8_t     idx;
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
    for(uint8_t i = 0; i < 11; i++)
    {
        if(strcasecmp(args, kNames[i].name) == 0)
        {
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
    char      *end;
    const long idx = strtol(args, &end, 10);
    if(end != args && idx >= 0 && idx <= 10)
    {
        gRelation = (float)idx * 2.4f;
        out.print(F("chord -> "));
        out.print(kLabels[idx]);
        out.print(F("  (rel "));
        out.print(gRelation, 1);
        out.println(F(")"));
        return;
    }
    out.println(F("usage: chord <name|0-10>"));
    out.println(
        F("  names: unison power minor major sus2 sus4 maj7 min7 dom7 dim "
          "octaves"));
    out.println(
        F("  index:    0      1      2     3     4    5    6    7    8    9    "
          "10"));
    out.print(F("  current mode: "));
    out.println(gVoiceMode == VoiceMode::CHORD
                    ? F("CHORD")
                    : F("not CHORD — rel still set"));
}

// M26a — Filter: <lp|hp|bp|notch|off> [cutoff_hz] [resonance]
static void cmd_filter(const char *args, Print &out)
{
    // Parse mode name (first token)
    char        modeBuf[8] = {};
    const char *rest       = args;
    uint8_t     i          = 0;
    while(*rest && *rest != ' ' && i < (uint8_t)(sizeof(modeBuf) - 1))
        modeBuf[i++] = *rest++;
    while(*rest == ' ')
        rest++;

    FilterMode mode;
    if(strcmp(modeBuf, "lp") == 0)
        mode = FilterMode::LP;
    else if(strcmp(modeBuf, "hp") == 0)
        mode = FilterMode::HP;
    else if(strcmp(modeBuf, "bp") == 0)
        mode = FilterMode::BP;
    else if(strcmp(modeBuf, "notch") == 0)
        mode = FilterMode::NOTCH;
    else if(strcmp(modeBuf, "off") == 0)
        mode = FilterMode::OFF;
    else
    {
        out.println(
            F("usage: filter <lp|hp|bp|notch|off> [cutoff_hz] [resonance]"));
        out.print(F("  filter mode: "));
        const char *names[] = {"off", "lp", "hp", "bp", "notch"};
        out.print(names[(uint8_t)gFilterMode]);
        out.print(F("  cutoff: "));
        out.print(gFilterCutoff, 0);
        out.print(F(" Hz"));
        out.print(F("  res: "));
        out.println(gFilterRes, 2);
        return;
    }
    gFilterMode = mode;

    if(*rest)
    {
        gFilterCutoff = constrain((float)atof(rest), 20.0f, 16000.0f);
        while(*rest && *rest != ' ')
            rest++;
        while(*rest == ' ')
            rest++;
        if(*rest)
            gFilterRes = constrain((float)atof(rest), 0.0f, 1.0f);
    }

    out.print(F("filter -> "));
    if(mode == FilterMode::OFF)
    {
        out.println(F("off"));
    }
    else
    {
        const char *names[] = {"off", "lp", "hp", "bp", "notch"};
        out.print(names[(uint8_t)mode]);
        out.print(F("  cutoff: "));
        out.print(gFilterCutoff, 0);
        out.print(F(" Hz"));
        out.print(F("  res: "));
        out.println(gFilterRes, 2);
    }
}

// M26a — FxOrder: fxorder filter <pre|post>  |  fxorder delay <pre|post>
static void cmd_fxorder(const char *args, Print &out)
{
    char        slotBuf[8] = {};
    const char *rest       = args;
    uint8_t     i          = 0;
    while(*rest && *rest != ' ' && i < (uint8_t)(sizeof(slotBuf) - 1))
        slotBuf[i++] = *rest++;
    while(*rest == ' ')
        rest++;

    if(strcmp(slotBuf, "filter") == 0)
    {
        if(strcmp(rest, "pre") == 0)
        {
            gFxOrder.filterPostChorus = false;
            out.println(F("fxorder filter -> pre-chorus (default)"));
        }
        else if(strcmp(rest, "post") == 0)
        {
            gFxOrder.filterPostChorus = true;
            out.println(F("fxorder filter -> post-chorus"));
        }
        else
        {
            out.print(F("fxorder filter: "));
            out.println(gFxOrder.filterPostChorus ? F("post-chorus")
                                                  : F("pre-chorus"));
        }
    }
    else if(strcmp(slotBuf, "delay") == 0)
    {
        if(strcmp(rest, "pre") == 0)
        {
            gFxOrder.delayPostReverb = false;
            out.println(F("fxorder delay -> pre-reverb (default)"));
        }
        else if(strcmp(rest, "post") == 0)
        {
            gFxOrder.delayPostReverb = true;
            out.println(F("fxorder delay -> post-reverb"));
        }
        else
        {
            out.print(F("fxorder delay: "));
            out.println(gFxOrder.delayPostReverb ? F("post-reverb")
                                                 : F("pre-reverb"));
        }
    }
    else
    {
        out.println(F("usage: fxorder <filter|delay> <pre|post>"));
        out.print(F("  filter: "));
        out.println(gFxOrder.filterPostChorus ? F("post-chorus")
                                              : F("pre-chorus"));
        out.print(F("  delay:  "));
        out.println(gFxOrder.delayPostReverb ? F("post-reverb")
                                             : F("pre-reverb"));
    }
}

// M26b — Reverb: reverb [off | on | freeze [on|off] | modspeed <v> | moddepth <v> | <mix> [size] [damping]]
static void cmd_reverb(const char *args, Print &out)
{
    if(strncmp(args, "freeze", 6) == 0)
    {
        const char *sub = args + 6;
        while(*sub == ' ')
            sub++;
        if(strcmp(sub, "on") == 0 || strcmp(sub, "1") == 0)
        {
            gRevFrozen = true;
            out.println(F("reverb freeze -> on"));
        }
        else if(strcmp(sub, "off") == 0 || strcmp(sub, "0") == 0)
        {
            gRevFrozen = false;
            out.println(F("reverb freeze -> off"));
        }
        else
        {
            out.println(gRevFrozen ? F("reverb freeze: on")
                                   : F("reverb freeze: off"));
        }
        return;
    }
    if(strncmp(args, "modspeed", 8) == 0)
    {
        const char *v = args + 8;
        while(*v == ' ')
            v++;
        if(*v)
            gRevModSpeed = constrain((float)atof(v), 0.1f, 4.0f);
        out.print(F("reverb modspeed: "));
        out.println(gRevModSpeed, 2);
        return;
    }
    if(strncmp(args, "moddepth", 8) == 0)
    {
        const char *v = args + 8;
        while(*v == ' ')
            v++;
        if(*v)
            gRevModDepth = constrain((float)atof(v), 0.0f, 1.0f);
        out.print(F("reverb moddepth: "));
        out.println(gRevModDepth, 2);
        return;
    }
    if(strcmp(args, "off") == 0)
    {
        gRevEnabled = false;
        out.println(F("reverb -> off"));
        return;
    }
    if(strcmp(args, "on") == 0)
    {
        gRevEnabled = true;
        out.print(F("reverb -> on  mix "));
        out.print((float)gRevMix, 2);
        out.print(F("  size "));
        out.print(gRevSize, 2);
        out.print(F("  damping "));
        out.println(gRevDamping, 2);
        return;
    }
    if(*args == '\0')
    {
        out.print(F("reverb: "));
        out.println(gRevEnabled ? F("on") : F("off"));
        out.print(F("  mix: "));
        out.println((float)gRevMix, 2);
        out.print(F("  size: "));
        out.println(gRevSize, 2);
        out.print(F("  damping: "));
        out.println(gRevDamping, 2);
        out.print(F("  modspeed: "));
        out.println(gRevModSpeed, 2);
        out.print(F("  moddepth: "));
        out.println(gRevModDepth, 2);
        out.print(F("  freeze: "));
        out.println(gRevFrozen ? F("on") : F("off"));
        return;
    }
    const float mix = constrain((float)atof(args), 0.0f, 1.0f);
    gRevMix         = mix;
    gRevEnabled     = (mix > 0.001f);

    const char *rest = args;
    while(*rest && *rest != ' ')
        rest++;
    while(*rest == ' ')
        rest++;
    if(*rest)
    {
        gRevSize = constrain((float)atof(rest), 0.0f, 1.0f);
        while(*rest && *rest != ' ')
            rest++;
        while(*rest == ' ')
            rest++;
        if(*rest)
            gRevDamping = constrain((float)atof(rest), 0.0f, 1.0f);
    }

    out.print(F("reverb -> "));
    if(!gRevEnabled)
    {
        out.println(F("off (mix=0)"));
    }
    else
    {
        out.print(F("mix "));
        out.print((float)gRevMix, 2);
        out.print(F("  size "));
        out.print(gRevSize, 2);
        out.print(F("  damping "));
        out.println(gRevDamping, 2);
    }
}

// M26c — Delay: delay [off | <mix> [time_ms] [feedback]]
static void cmd_delay(const char *args, Print &out)
{
    if(strcmp(args, "off") == 0)
    {
        gDelayMix = 0.0f;
        out.println(F("delay -> off"));
        return;
    }
    if(strcmp(args, "on") == 0)
    {
        if(gDelayMix < 0.001f)
            gDelayMix = 0.3f; // restore last or default mix
        out.print(F("delay -> on  mix "));
        out.print(gDelayMix, 2);
        out.print(F("  time "));
        out.print(gDelayTime, 0);
        out.print(F(" ms  feedback "));
        out.println(gDelayFeedback, 2);
        return;
    }
    if(*args == '\0')
    {
        out.print(F("delay: "));
        out.println(gDelayMix > 0.001f ? F("on") : F("off"));
        out.print(F("  mix: "));
        out.println(gDelayMix, 2);
        out.print(F("  time: "));
        out.print(gDelayTime, 0);
        out.println(F(" ms"));
        out.print(F("  feedback: "));
        out.println(gDelayFeedback, 2);
        return;
    }
    gDelayMix = constrain((float)atof(args), 0.0f, 1.0f);

    const char *rest = args;
    while(*rest && *rest != ' ')
        rest++;
    while(*rest == ' ')
        rest++;
    if(*rest)
    {
        gDelayTime = constrain((float)atof(rest), 10.0f, (float)DELAY_MAX_MS);
        while(*rest && *rest != ' ')
            rest++;
        while(*rest == ' ')
            rest++;
        if(*rest)
            gDelayFeedback = constrain((float)atof(rest), 0.0f, 0.95f);
    }

    out.print(F("delay -> "));
    if(gDelayMix < 0.001f)
    {
        out.println(F("off (mix=0)"));
    }
    else
    {
        out.print(F("mix "));
        out.print(gDelayMix, 2);
        out.print(F("  time "));
        out.print(gDelayTime, 0);
        out.print(F(" ms"));
        out.print(F("  feedback "));
        out.println(gDelayFeedback, 2);
    }
}

// ---------------------------------------------------------------------------
// Command table — the single source of truth for all transports.
//
// Columns: name | help text shown in listing | handler
// ---------------------------------------------------------------------------

// M5x — filter type selector: svf (Cytomic SVF) | ladder (OTA 4-pole)
static void cmd_filter_type(const char *args, Print &out)
{
    if(strcmp(args, "svf") == 0)
    {
        gFilterType = FilterType::SVF;
        // gFilterInst is re-pointed in updateControl() on the next tick
        out.println(F("filter type -> svf (Cytomic state-variable)"));
    }
    else if(strcmp(args, "ladder") == 0)
    {
        gFilterType = FilterType::LADDER;
        out.println(
            F("filter type -> ladder (OTA 4-pole, LP only, self-oscillating)"));
    }
    else
    {
        out.print(F("filter type: "));
        out.println(gFilterType == FilterType::SVF ? F("svf") : F("ladder"));
        out.println(F("usage: filter type <svf|ladder>"));
    }
}

// M5x — envelope type selector: ar | adsr
static void cmd_env_type(const char *args, Print &out)
{
    if(strcmp(args, "ar") == 0 || strcmp(args, "AR") == 0)
    {
        gEnvelopeType = EnvelopeType::AR;
        gCurveEng->reset();
        out.println(F("env type -> ar (single-knob AR with pluck mode)"));
    }
    else if(strcmp(args, "adsr") == 0 || strcmp(args, "ADSR") == 0)
    {
        gEnvelopeType = EnvelopeType::ADSR;
        gCurveEng->reset();
        out.println(F("env type -> adsr (A/D/S/R + optional loop)"));
    }
    else
    {
        out.print(F("env type: "));
        out.println(gEnvelopeType == EnvelopeType::AR ? F("ar") : F("adsr"));
        out.println(F("usage: env type <ar|adsr>"));
    }
}

// M5x — set ADSR parameters: adsr <attack_s> <decay_s> <sustain> <release_s> [loop]
static void cmd_adsr(const char *args, Print &out)
{
    if(!args || !*args)
    {
        out.print(F("adsr: A="));
        out.print(gAdsrAttack, 3);
        out.print(F(" D="));
        out.print(gAdsrDecay, 3);
        out.print(F(" S="));
        out.print(gAdsrSustain, 2);
        out.print(F(" R="));
        out.print(gAdsrRelease, 3);
        out.print(F(" loop="));
        out.println(gAdsrLoop ? F("on") : F("off"));
        out.println(F("usage: adsr <A_s> <D_s> <S_0-1> <R_s> [loop]"));
        return;
    }
    const char *p         = args;
    auto        nextFloat = [&](float &v, float lo, float hi)
    {
        while(*p == ' ')
            p++;
        if(!*p)
            return;
        v = constrain((float)atof(p), lo, hi);
        while(*p && *p != ' ')
            p++;
    };
    nextFloat(gAdsrAttack, 0.001f, 10.0f);
    nextFloat(gAdsrDecay, 0.001f, 10.0f);
    nextFloat(gAdsrSustain, 0.0f, 1.0f);
    nextFloat(gAdsrRelease, 0.001f, 10.0f);
    while(*p == ' ')
        p++;
    if(*p)
    {
        if(strncmp(p, "loop", 4) == 0)
            gAdsrLoop = true;
        else if(strncmp(p, "noloop", 6) == 0)
            gAdsrLoop = false;
    }
    out.print(F("adsr -> A="));
    out.print(gAdsrAttack, 3);
    out.print(F(" D="));
    out.print(gAdsrDecay, 3);
    out.print(F(" S="));
    out.print(gAdsrSustain, 2);
    out.print(F(" R="));
    out.print(gAdsrRelease, 3);
    out.print(F(" loop="));
    out.println(gAdsrLoop ? F("on") : F("off"));
}

// M5x — standalone loop toggle: env loop <on|off>
static void cmd_env_loop(const char *args, Print &out)
{
    if(strcmp(args, "on") == 0)
    {
        gAdsrLoop = true;
        out.println(F("env loop -> on"));
    }
    else if(strcmp(args, "off") == 0)
    {
        gAdsrLoop = false;
        out.println(F("env loop -> off"));
    }
    else
    {
        out.print(F("env loop: "));
        out.println(gAdsrLoop ? F("on") : F("off"));
    }
}

// ---------------------------------------------------------------------------
// cmd_pot — knob takeover behaviour (M62).
//
// A physical knob cannot move itself, so once the Web Configurator, a MIDI CC
// or a preset recall changes a parameter, the panel knob is left pointing
// somewhere else.  This selects what happens the next time that knob is turned:
//
//   scale  (default) knob movement steers the value proportionally toward the
//                    end of travel — no jump, and the knob never feels dead
//   pickup           knob is inert until it passes through the current value
//   jump             first movement takes over instantly
//
// `pot sync` skips the wait entirely: every parameter snaps to its knob.
// ---------------------------------------------------------------------------
static const __FlashStringHelper *potTakeoverName(PotTakeoverMode m)
{
    switch(m)
    {
        case PotTakeoverMode::JUMP: return F("jump");
        case PotTakeoverMode::PICKUP: return F("pickup");
        default: return F("scale");
    }
}

static void cmd_pot(const char *args, Print &out)
{
    if(strncmp(args, "takeover", 8) == 0)
    {
        const char *mode = args + 8;
        while(*mode == ' ')
            mode++;
        if(strcmp(mode, "scale") == 0)
            gPotTakeoverMode = PotTakeoverMode::SCALE;
        else if(strcmp(mode, "pickup") == 0)
            gPotTakeoverMode = PotTakeoverMode::PICKUP;
        else if(strcmp(mode, "jump") == 0)
            gPotTakeoverMode = PotTakeoverMode::JUMP;
        else if(*mode != '\0')
        {
            out.println(F("usage: pot takeover <scale|pickup|jump>"));
            return;
        }
        out.print(F("pot takeover -> "));
        out.println(potTakeoverName(gPotTakeoverMode));
        return;
    }
    if(strcmp(args, "sync") == 0)
    {
        potsReattach();
        out.println(F("pot sync -> knobs now own their parameters"));
        return;
    }
    if(args[0] == '\0')
    {
        out.print(F("pot takeover: "));
        out.println(potTakeoverName(gPotTakeoverMode));
        return;
    }
    out.println(F("usage: pot <takeover <scale|pickup|jump> | sync>"));
}

// ---------------------------------------------------------------------------
// cmd_dump — machine-readable patch snapshot consumed by the web configurator.
//
// Output format:
//   dump_begin
//   cc:<N>=<V>        (one line per parameter; N=CC number, V=0-127 raw CC value)
//   ...
//   dump_end
//
// Web configurator sends "dump", accumulates lines between the delimiters,
// then replays each CC to update its UI (same path as incoming MIDI CC).
// ---------------------------------------------------------------------------
static void cmd_dump(const char * /*args*/, Print &out)
{
    out.println(F("dump_begin"));
    // Continuous float params from the central CC table
    for(uint8_t i = 0; i < kParamCount; i++)
    {
        const ParamDescriptor &p = kParamTable[i];
        out.print(F("cc:"));
        out.print(p.cc);
        out.print('=');
        out.println(p.toCC(*p.target));
    }
    // Discrete params from the generated enum table. toCC() returns the low
    // edge of the option's band, which the configurator resolves the same way
    // as any other value in that band — and is already what the SysEx patch
    // dump sends, so the two transports now agree by construction.
    //
    // This was a hand-written list of ternaries, and it drifted: it still
    // emitted a `cc:85` derived from gDelayMix, but CC 85 is Gate Length and
    // the float loop above already printed it. The web replays these lines in
    // order, so the stale one won and Gate Length jumped to 0 or 2000 ms on
    // every dump. `cc:16` was duplicated the same way after ROOT moved into
    // params.json.
    for(uint8_t i = 0; i < kEnumCount; i++)
    {
        const EnumParamDescriptor &e = kEnumTable[i];
        out.print(F("cc:"));
        out.print(e.cc);
        out.print('=');
        out.println(e.toCC(*e.target));
    }
    // Params encoded outside both generated tables — keep in sync with
    // moduleHook_extraPatchPairs(), which does the same job for SysEx.
    // cc:104 = transpose: encode signed −24…+24 as 0–48 (offset 24) for 7-bit CC safety
    out.print(F("cc:104="));
    out.println((uint8_t)constrain((int)gTranspose + 24, 0, 48));
    out.print(F("cc:114="));
    out.println(gRevFrozen ? 127 : 0);
    out.print(F("cc:116="));
    out.println(gRevEnabled ? 127 : 0);
    out.print(F("cc:110="));
    out.println(gMidiChannel); // 0=omni, 1-16
    out.println(F("dump_end"));
}

// clang-format off
const CommandEntry kCommands[] = {
    {"pitch",     "<hz>        base frequency (20-8000 Hz)",                              cmd_pitch},
    {"note",      "<note>      note name (e.g., C4, A#3)",                                cmd_note},
    {"mode",      "<pair|cloud|chord|cascade|string>  voice mode (default: pair)",        cmd_mode},
    {"rel",       "<0-24>      RELATION semitones: 0=unison  7=fifth  12=octave  24=2oct",  cmd_rel},
    {"cloud",     "[pool <n>|notes <n>]  CLOUD supersaw pool; bare = show saws/note",     cmd_cloud},
    {"color",     "<0-1>       COLOR: FM depth (pair/cascade) or fine Hz spread (other)",   cmd_color},
    {"shape",     "<0-1>       waveform: 0=sine  0.25=tri  0.5=saw  0.75=pulse  1=hollow", cmd_shape},
    {"fat",       "<0-1>       sub osc level: 0=off  1=full (50% of main)",               cmd_fat},
    {"suboct",    "<1|2>       sub oscillator octave: 1=one below (default)  2=two below", cmd_sub_octave},
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
    {"filter",       "<lp|hp|bp|notch|off> [cutoff_hz] [resonance]  SVF/ladder filter (M26a)", cmd_filter},
    {"filter type",  "<svf|ladder>  switch filter algorithm (M5x)",                          cmd_filter_type},
    {"fxorder",      "<filter|delay> <pre|post>   effect chain ordering (M26a)",             cmd_fxorder},
    {"reverb",       "[off | <mix> [size] [damping]]   plate reverb, Core 1 (M26b)",         cmd_reverb},
    {"delay",        "[off | <mix> [time_ms] [feedback]]   ping-pong delay (M26c)",          cmd_delay},
    {"env type",     "<ar|adsr>  switch envelope algorithm (M5x)",                           cmd_env_type},
    {"adsr",         "<A_s> <D_s> <S> <R_s> [loop|noloop]  ADSR params (M5x)",              cmd_adsr},
    {"env loop",     "<on|off>  loop ADSR as LFO (M5x)",                                     cmd_env_loop},
    {"veloc",     "<on|off>    MIDI velocity sensitivity: on=velocity scales vol (default), off=fixed", cmd_veloc},
    {"glide",     "<on|off>    portamento on/off: on=pitch slides between notes, off=instant",          cmd_glide},
    {"glidetime", "<0-2>       portamento slide time in seconds (0=instant, default)",                  cmd_glidetime},
    {"scale",     "<name|off>  scale quantization (chromatic=off, major, minor, dorian...)",           cmd_scale},
    {"transpose", "<-24..24>   MIDI note transpose in semitones (default: 0)",                        cmd_transpose},
    {"midichan",  "<1-16|omni> MIDI receive channel (default: omni)",                    cmd_midichan},
    {"pot",       "<takeover <scale|pickup|jump> | sync>  knob takeover after a web/MIDI edit", cmd_pot},
    {"config",    "<save|load|reset> [1-9|all]  preset slots 1-9; no slot = live state", cmd_config},
    {"dump",      "            output all params as cc:N=V lines (web configurator sync)", cmd_dump},
    {"status",    "            print all current parameters",                              cmd_status},
    {"perf",      "            enable or disable CPU profiling printing",                  cmd_performance_print},
    {"cpu",       "            audio ISR µs, headroom, overrun count",                    cmd_cpu},
    {"help",      "            show this help",                                            cmd_help},
};
// clang-format on

const uint8_t kCommandCount
    = (uint8_t)(sizeof(kCommands) / sizeof(kCommands[0]));

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static void cmd_help(const char * /*args*/, Print &out)
{ commands_printHelp(out); }
