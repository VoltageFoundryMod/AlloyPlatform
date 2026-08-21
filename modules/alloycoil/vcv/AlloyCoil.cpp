// Alloy Coil — VCV Rack module.
//
// The same engine the firmware runs, driven through the same IOBridge, so the
// plugin and the hardware are one implementation with two front ends. What
// differs is bandwidth: Rack gives the EXCITER input a sample per frame, where
// the firmware currently reads that jack at the control tick.
//
// Engine by Synthux Academy (Nick Donaldson / Roey Tsemah) — see
// modules/alloycoil/README.md, CREDITS.md and LICENSE.

#include "ControlSmoother.h"
#include "FeedbackSynthEngine.h"
#include "OutputStage.h"
#include "ManifestQuantity.hpp" // tooltips that read the manifest's curve
#include "SubMenuSlider.hpp"    // shift-secondaries as context-menu sliders
#include "PanelLayout.h"     // shared panel geometry (all modules, one PCB)
#include "PanelLed.hpp"      // aperture-shaped lights, matching the panel art
#include "VCVRackIO.h"       // platform: positional slots -> Rack indices
#include "io/CoilLeds.h"   // the LED language, shared with the firmware
#include "io/IOBridge.h"
#include "io/PanelMap.h"
#include "param_manifest.generated.h" // knob curves, see manifestRow()
#include "params.h"
#include "plugin.hpp"

#include <app/MidiDisplay.hpp> // appendMidiMenu() — the MIDI settings submenu
#include <atomic>
#include <cassert>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Globals declared extern in params.h. main.cpp is not compiled into the
// plugin, so the definitions live here.
//
// One set per process, not per Module — a second AlloyCoil in the same rack
// shares them. That is a real limitation and it is the same one AlloyFlux has;
// the engine instance itself is per-Module, so only the goal values collide,
// and they are overwritten from this module's own knobs every process() call.
// ---------------------------------------------------------------------------
// Same generated definitions the firmware links, so the boot values here and
// on hardware cannot drift apart.
#include "param_globals.generated.h"

volatile float gExciterIn = 0.0f;

// ---------------------------------------------------------------------------
// The manifest row for a parameter, by params.json name.
//
// Everything a Rack param needs beyond its widget lives on that row: the boot
// position, the curve the tooltip has to invert, the unit. Looking the row up
// once and handing it to the ParamQuantity keeps all three derived from the
// same source instead of transcribed next to it.
//
// The boot position is the case that already went wrong. configParam() takes a
// *position*, not a value, so every default here used to be a hand-computed
// inverse of that knob's curve — and when M63i changed fbbody and echotime from
// log to square-law, the two numbers computed against the old curves stayed
// put. The module booted with its echo at 1.14 s instead of the 0.5 s
// params.json specifies, silently, in the build that is supposed to be the A/B
// reference for the hardware. ParamDescriptor::toPos() is exactly that inverse.
// ---------------------------------------------------------------------------
static const ParamDescriptor *manifestRow(const char *name)
{
    for(uint8_t i = 0; i < kParamManifestCount; i++)
    {
        if(std::strcmp(kParamManifest[i].name, name) == 0)
            return &kParamManifest[i];
    }
    return nullptr; // unreachable for a name that is in params.json
}

// ---------------------------------------------------------------------------
// SysEx — the Alloy platform patch protocol, identical to the one the firmware
// speaks in platform/src/usb_midi.cpp. The Web Configurator cannot tell a Rack
// module from a board except by these two signature bytes, which is the point:
// one page drives either.
//
//   F0 7D <id0> <id1> <cmd> [cc0 val0 cc1 val1 ...] F7
//
// 7F 7F in place of the signature is the discovery wildcard. A host that does
// not yet know what is on the port broadcasts REQUEST_DUMP; every Alloy module
// answers with an ordinary PATCH_DUMP carrying its *real* signature, so one
// round trip both identifies the module and delivers its patch. Over loopMIDI /
// IAC the port name says nothing, so that probe is the only way the page can
// tell this module from an AlloyFlux in the same rack. A broadcast may only
// ask, never change — every module on the port sees it.
//
// The signature is duplicated from modules/alloycoil/src/module_hooks.cpp
// rather than shared: that file is Arduino code and is not compiled into the
// plugin. 'A','C' — it must differ from AlloyFlux's 'A','F'.
// ---------------------------------------------------------------------------
static constexpr uint8_t kSysExMfr  = 0x7D; // non-commercial manufacturer ID
static constexpr uint8_t kSysExAny  = 0x7F; // wildcard signature byte
static constexpr uint8_t kSysExDev0 = 'A';
static constexpr uint8_t kSysExDev1 = 'C';

static constexpr uint8_t kSysExRequestDump = 0x01;
static constexpr uint8_t kSysExPatchDump   = 0x02;
static constexpr uint8_t kSysExApplyPatch  = 0x03;
static constexpr uint8_t kSysExPresetSave  = 0x04;
static constexpr uint8_t kSysExPresetLoad  = 0x05;
static constexpr uint8_t kSysExPresetReset = 0x06;

struct AlloyCoil : Module
{
    enum ParamId
    {
        PITCH_PARAM,
        FBGAIN_PARAM,
        FBBODY_PARAM,
        FBLPF_PARAM,
        FBHPF_PARAM,
        ECHOSEND_PARAM,
        ECHOTIME_PARAM,
        ECHOFB_PARAM,
        REVMIX_PARAM,
        REVDECAY_PARAM,
        VOL_PARAM,
        EXCITE_PARAM,
        // ---- Panel buttons (SW2/SW3). WARP is the doppler warp — held, it
        //      halves the echo time, which is upstream Audrey II's one panel
        //      switch. It sits on the slot AlloyFlux calls MODE; the panels
        //      disagree because the modules do, which is what PanelMap is for.
        //      SHIFT selects the shift-secondaries on hardware; Rack has no key
        //      to hold so those are context-menu sliders and the button is
        //      wired to the HAL but inert, rather than given a second,
        //      Rack-only meaning the firmware would not share. ----
        WARP_PARAM,
        SHIFT_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        VOCT_INPUT,
        GATE_INPUT, // reserved for the VCA/envelope option — see PanelMap.h
        // Hardware TRS MIDI jack (J2). Visual only in Rack — software MIDI
        // arrives through the module's MIDI settings, not a patch cable — but
        // the hole is on the panel, so leaving it undrawn would be the lie.
        MIDI_INPUT,
        // Listed in panel reading order from here: top row CV 1, CV 2, then
        // the lower row left to right starting at EXC IN.
        FBBODY_CV_INPUT,
        FBGAIN_CV_INPUT,
        EXCITER_INPUT,
        ECHOSEND_CV_INPUT,
        REVMIX_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId
    {
        L_OUTPUT,
        R_OUTPUT,
        OUTPUTS_LEN
    };
    // NOTE: this unscoped enum shadows the global ::LightId from HardwareIO.h
    // inside this struct, the same way AlloyFlux's does. Registration uses the
    // Led:: names from PanelMap.h, which are typed ::LightId constants and so
    // are unaffected.
    //
    // Named for the panel's LED1..LED7 designators, listed here in LightId
    // (left/right pair) order — see PanelLayout::kLedMm for why those differ.
    enum LightId
    {
        LED1_R_LIGHT, // D12 upper left   — output level, left
        LED1_G_LIGHT,
        LED1_B_LIGHT,
        LED7_R_LIGHT, // D22 upper right  — output level, right
        LED7_G_LIGHT,
        LED7_B_LIGHT,
        LED2_R_LIGHT, // D13 left         — loop danger
        LED2_G_LIGHT,
        LED2_B_LIGHT,
        LED6_R_LIGHT, // D21 right        — loop danger
        LED6_G_LIGHT,
        LED6_B_LIGHT,
        LED3_R_LIGHT, // D14 lower left   — echo
        LED3_G_LIGHT,
        LED3_B_LIGHT,
        LED5_R_LIGHT, // D16 lower right  — reverb / shift
        LED5_G_LIGHT,
        LED5_B_LIGHT,
        LED4_R_LIGHT, // D15 bottom centre — string alive / exciter
        LED4_G_LIGHT,
        LED4_B_LIGHT,
        LIGHTS_LEN
    };

    // -----------------------------------------------------------------------
    // MIDI ports.
    //
    // The input is what the Web Configurator and any keyboard reach the module
    // through; the output carries PATCH_DUMP replies and CC feedback back. Both
    // are chosen from the module's context menu, and the output auto-follows the
    // input — see syncMidiOutput().
    // -----------------------------------------------------------------------
    rack::midi::InputQueue midiInput;
    rack::midi::Output     midiOutput;

    /// Set from the menu (UI thread) or the SysEx handler, consumed in
    /// process(). midiOutput may only be written from the audio thread.
    std::atomic<bool> _dumpPending{false};

    /// Last CC value emitted per CC number; 0xFF = never sent, so the first
    /// feedback pass emits a full snapshot. Seeded from inbound host CCs and
    /// from every dump, so a value the host already has is not sent back at it.
    uint8_t _lastFeedbackCC[128];

    /// Preset slots 1–9, addressed by SysEx and persisted with the patch. The
    /// firmware keeps these in flash (config_store); Rack keeps them in the
    /// module's own JSON, which is the same thing with a different backing
    /// store — the wire protocol does not change.
    std::vector<std::pair<uint8_t, uint8_t>> _presets[9];

    // -----------------------------------------------------------------------
    // CC ↔ knob mapping.
    //
    // Built from the generated manifest, so params.json stays the one place a
    // CC number is written down. What this file adds is the half the manifest
    // cannot know: which *Rack param* owns each parameter. The manifest's
    // `target` is the gXxx goal value, and writing that here would be useless —
    // fillCoilParams() overwrites every one of them from the knobs on the next
    // control tick. A CC has to move the knob.
    //
    // The conversion is a bare `pos = cc / 127`, with no curve applied, and that
    // is not a shortcut: every AlloyCoil knob is configured 0–1 and IOBridge
    // applies exactly the curve params.json declares. Knob travel, CC/127 and
    // ParamDescriptor's control position are the same quantity, which is why a
    // knob and a CC land on the same value. A parameter configured over some
    // other range would break that and needs its own case here.
    // -----------------------------------------------------------------------
    struct KnobOwner
    {
        const char *name;
        int         param;
    };

    /// cc → Rack param index, -1 for a CC this module does not own.
    int8_t _ccToParam[128];
    /// (cc, param) in manifest order — the order a patch dump is emitted in.
    std::vector<std::pair<uint8_t, uint8_t>> _ccOrder;

    void _buildCCMap()
    {
        static const KnobOwner kOwners[] = {
            {"pitch", PITCH_PARAM},
            {"fbgain", FBGAIN_PARAM},
            {"fbbody", FBBODY_PARAM},
            {"fblpf", FBLPF_PARAM},
            {"fbhpf", FBHPF_PARAM},
            {"echosend", ECHOSEND_PARAM},
            {"echotime", ECHOTIME_PARAM},
            {"echofb", ECHOFB_PARAM},
            {"revmix", REVMIX_PARAM},
            {"revdecay", REVDECAY_PARAM},
            {"vol", VOL_PARAM},
            {"excite", EXCITE_PARAM},
        };
        std::memset(_ccToParam, -1, sizeof(_ccToParam));
        _ccOrder.clear();
        _ccOrder.reserve(kParamManifestCount);
        for(uint8_t i = 0; i < kParamManifestCount; i++)
        {
            for(const KnobOwner &o : kOwners)
            {
                if(std::strcmp(kParamManifest[i].name, o.name) != 0)
                    continue;
                const uint8_t cc = kParamManifest[i].cc & 0x7f;
                _ccToParam[cc]   = (int8_t)o.param;
                _ccOrder.push_back({cc, (uint8_t)o.param});
                break;
            }
        }
        // A manifest row with no entry above is simply absent from both tables:
        // it drops out of the dump and its CC is ignored, rather than writing a
        // goal value the next control tick would discard anyway.
    }

    /**
     * Configure one knob from its manifest row.
     *
     * @param paramId   the Rack param this knob drives
     * @param name      params.json name, i.e. the manifest row to read
     * @param rackLabel tooltip title — see the note at the call site
     *
     * The unit gets the leading space Rack's convention wants; the manifest
     * stores it bare because the web UI composes its readouts differently.
     */
    void configManifestParam(int paramId, const char *name, const char *rackLabel)
    {
        const ParamDescriptor *d = manifestRow(name);
        // Unreachable for a name that is in params.json, which every call site's
        // is. Guarded rather than dereferenced anyway: a typo here would
        // otherwise take Rack down while loading the plugin, and a knob that
        // boots at zero with a raw-position tooltip is a far better way to find
        // out. ManifestQuantity makes the same null check for the same reason.
        assert(d && "no such parameter in params.json");
        auto *q = configParam<ManifestQuantity>(
            paramId,
            0.f,
            1.f,
            d ? d->toPos(d->defVal) : 0.f,
            rackLabel,
            d && d->unit ? std::string(" ") + d->unit : std::string());
        q->desc = d;
    }

    AlloyCoil() : _io(this)
    {
        std::memset(_lastFeedbackCC, 0xFF, sizeof(_lastFeedbackCC));
        _buildCCMap();
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Every knob is 0–1 and the range mapping lives in IOBridge, exactly as
        // it does for the hardware pots. Keeping the curve in one place is what
        // stops a knob and a MIDI CC landing on different values.
        //
        // configManifestParam() carries the consequences of that: the boot
        // position, the unit and the tooltip's inverse of the curve all come off
        // the manifest row. The Rack label is the one thing passed in, because
        // it is the one thing the manifest cannot supply — its labels are short
        // ("Send", "Mix", "Time") for a web UI that groups them under a category
        // heading, and a Rack tooltip has no such heading to sit under.
        configManifestParam(PITCH_PARAM, "pitch", "String pitch");
        configManifestParam(FBGAIN_PARAM, "fbgain", "Feedback gain");
        configManifestParam(FBBODY_PARAM, "fbbody", "Body");
        configManifestParam(FBLPF_PARAM, "fblpf", "Feedback LPF");
        configManifestParam(FBHPF_PARAM, "fbhpf", "Feedback HPF");
        configManifestParam(ECHOSEND_PARAM, "echosend", "Echo send");
        configManifestParam(ECHOTIME_PARAM, "echotime", "Echo time");
        configManifestParam(ECHOFB_PARAM, "echofb", "Echo feedback");
        configManifestParam(REVMIX_PARAM, "revmix", "Reverb mix");
        configManifestParam(REVDECAY_PARAM, "revdecay", "Reverb decay");
        configManifestParam(VOL_PARAM, "vol", "Volume");
        // Stored 0–2, shown in dB. The range past unity is what lets a ±5 V
        // Eurorack source reach the engine's full scale through a ±8 V jack;
        // reading it as −inf…+6 dB is what makes the default obviously neutral
        // rather than obviously halfway. See ParamDisplay.
        configManifestParam(EXCITE_PARAM, "excite", "Exciter level");

        // Panel buttons — momentary, matching the hardware switches.
        configButton(WARP_PARAM,
                     "Warp — hold to halve the echo time; the tail pitches up "
                     "on press and back down on release");
        configButton(SHIFT_PARAM,
                     "Shift — selects the secondary parameters on hardware; "
                     "in Rack they are in the context menu");

        configInput(VOCT_INPUT, "V/Oct");
        configInput(GATE_INPUT, "Gate (unused — reserved for VCA/envelope)");
        configInput(MIDI_INPUT,
                    "MIDI (TRS — use context menu for software MIDI)");
        configInput(FBBODY_CV_INPUT, "Body CV");
        configInput(FBGAIN_CV_INPUT, "Feedback gain CV");
        configInput(ECHOSEND_CV_INPUT, "Echo send CV");
        configInput(REVMIX_CV_INPUT, "Reverb mix CV");
        configInput(EXCITER_INPUT,
                    "Exciter — audio into the resonator; unpatched, the string "
                    "self-excites from its own noise floor");
        configOutput(L_OUTPUT, "Left");
        configOutput(R_OUTPUT, "Right");

        // Positional slots -> Rack indices. Physical knobs first, then the two
        // SHIFT-secondaries; the numbering is AlloyFlux's panel positions, so
        // the same PCB serves either firmware. See io/PanelMap.h.
        _io.assignPot(Pot::PITCH, PITCH_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::FBGAIN, FBGAIN_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::FBBODY, FBBODY_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::FBLPF, FBLPF_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::ECHOSEND, ECHOSEND_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::ECHOTIME, ECHOTIME_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::ECHOFB, ECHOFB_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::REVMIX, REVMIX_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::VOL, VOL_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::FBHPF, FBHPF_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::REVDECAY, REVDECAY_PARAM, 0.f, 1.f);
        _io.assignPot(Pot::EXCITE, EXCITE_PARAM, 0.f, 1.f);

        _io.assignCV(Cv::VOCT, VOCT_INPUT);
        _io.assignCV(Cv::FBBODY, FBBODY_CV_INPUT);
        _io.assignCV(Cv::FBGAIN, FBGAIN_CV_INPUT);
        _io.assignCV(Cv::ECHOSEND, ECHOSEND_CV_INPUT);
        _io.assignCV(Cv::REVMIX, REVMIX_CV_INPUT);
        _io.assignCV(Cv::EXCITER, EXCITER_INPUT);
        // Cv::GATE is deliberately not assigned — see PanelMap.h.
        // MIDI_INPUT has no CV slot at all: it is a MIDI jack, not a CV one.

        _io.assignButton(Btn::WARP, WARP_PARAM);
        _io.assignButton(Btn::SHIFT, SHIFT_PARAM);

        // Led:: names are typed ::LightId constants, not this struct's enum.
        _io.assignLight(Led::LEVEL_L, LED1_R_LIGHT);
        _io.assignLight(Led::LEVEL_R, LED7_R_LIGHT);
        _io.assignLight(Led::LOOP_L, LED2_R_LIGHT);
        _io.assignLight(Led::LOOP_R, LED6_R_LIGHT);
        _io.assignLight(Led::ECHO, LED3_R_LIGHT);
        _io.assignLight(Led::SPACE, LED5_R_LIGHT);
        _io.assignLight(Led::CENTRE, LED4_R_LIGHT);

        configLight(LED1_R_LIGHT, "Output level (left)");
        configLight(LED7_R_LIGHT, "Output level (right)");
        configLight(LED2_R_LIGHT,
                    "Feedback loop — green decaying, amber near unity, red "
                    "building");
        configLight(LED6_R_LIGHT,
                    "Feedback loop — green decaying, amber near unity, red "
                    "building");
        configLight(LED3_R_LIGHT, "Echo — flashes once per repeat");
        configLight(LED5_R_LIGHT, "Reverb (white while SHIFT is held)");
        configLight(LED4_R_LIGHT, "String activity / exciter");

        initForRate(APP->engine->getSampleRate());
    }

    void onSampleRateChange() override
    { initForRate(APP->engine->getSampleRate()); }

    /// Everything that depends on the host rate, in one place so the
    /// constructor and the rate-change hook cannot drift.
    void initForRate(float sampleRate)
    {
        _engine.Init(sampleRate);
        _output.Init();
        // Same rule as the firmware: the smoother's coefficients belong to the
        // rate Step() is actually called at, which is once per kSmoothFrames.
        _smoother.Init(sampleRate / (float)kSmoothFrames);
        _smoothPhase = 0;
        // CC feedback runs at the firmware's control rate, not Rack's. The
        // panel here is read at ~1 kHz (see kControlDiv), and emitting a CC per
        // tick would put eight times as much MIDI on the wire as the module
        // does for the same knob move — enough to make a drag feel laggy in the
        // configurator on a virtual port.
        _feedbackDiv = (int)(sampleRate / 128.0f + 0.5f);
        if(_feedbackDiv < 1)
            _feedbackDiv = 1;
        _feedbackPhase = 0;
    }

    void onReset(const ResetEvent &e) override
    {
        Module::onReset(e);
        // Every knob is back at its default; tell the page rather than leaving
        // it showing the patch that was there a moment ago.
        _dumpPending.store(true);
    }

    // -----------------------------------------------------------------------
    // Auto-configure midiOutput to the output device whose name matches
    // midiInput. On loopback drivers (loopMIDI / IAC Bus) the input and output
    // port share one device name, so the return path "just works" once the
    // input is chosen and there is no second menu to find.
    //
    // Safe on the audio thread: it runs once per session, guarded by
    // deviceId >= 0, after which it is a single int compare.
    // -----------------------------------------------------------------------
    void syncMidiOutput()
    {
        if(midiOutput.deviceId >= 0)
            return; // already configured, by this or by the menu
        if(midiInput.driverId < 0 || midiInput.deviceId < 0)
            return; // nothing to derive it from
        auto *drv = rack::midi::getDriver(midiInput.driverId);
        if(!drv)
            return;
        std::string wantName = drv->getInputDeviceName(midiInput.deviceId);
        if(wantName.empty())
            return;
        if(midiOutput.driverId != midiInput.driverId)
            midiOutput.setDriverId(midiInput.driverId);
        for(int outId : drv->getOutputDeviceIds())
        {
            if(drv->getOutputDeviceName(outId) == wantName)
            {
                midiOutput.setDeviceId(outId);
                return;
            }
        }
    }

    /// The current patch as raw (cc, value) pairs — the payload of a PATCH_DUMP
    /// and what a preset slot stores.
    ///
    /// Knob positions, not the gXxx goal values: unlike the firmware, whose
    /// dump reads the post-CV goal, this deliberately reports what the panel
    /// says. A CV-modulated parameter would otherwise stream a changed CC on
    /// every feedback tick and drag the configurator's slider around with the
    /// LFO.
    std::vector<std::pair<uint8_t, uint8_t>> _snapshotPairs()
    {
        std::vector<std::pair<uint8_t, uint8_t>> out;
        out.reserve(_ccOrder.size());
        for(const auto &e : _ccOrder)
        {
            const float t = params[e.second].getValue();
            int         v = (int)(t * 127.0f + 0.5f);
            v             = v < 0 ? 0 : (v > 127 ? 127 : v);
            out.push_back({e.first, (uint8_t)v});
        }
        return out;
    }

    void sendPatchDump()
    {
        syncMidiOutput();
        const auto           pairs = _snapshotPairs();
        std::vector<uint8_t> buf;
        buf.reserve(6 + pairs.size() * 2);
        buf.push_back(0xF0);
        buf.push_back(kSysExMfr);
        buf.push_back(kSysExDev0);
        buf.push_back(kSysExDev1);
        buf.push_back(kSysExPatchDump);
        for(const auto &p : pairs)
        {
            buf.push_back(p.first);
            buf.push_back(p.second);
            // The host now has this value; seed the outbound cache from the
            // same snapshot so the next feedback tick does not repeat the whole
            // dump as a dozen individual CCs.
            _lastFeedbackCC[p.first] = p.second;
        }
        buf.push_back(0xF7);
        rack::midi::Message msg;
        msg.bytes.assign(buf.begin(), buf.end());
        midiOutput.sendMessage(msg);
    }

    // -----------------------------------------------------------------------
    // CC feedback — emit changed parameters so the configurator's controls
    // follow the panel. Only CCs whose 7-bit value has changed are sent, which
    // is nothing at all on a patch nobody is touching.
    //
    // The loop back through the host is benign: the page sends CC → knob moves
    // → same 7-bit value → cache hit → nothing goes back out.
    // -----------------------------------------------------------------------
    void sendCCFeedback()
    {
        syncMidiOutput();
        if(midiOutput.deviceId < 0)
            return;
        for(const auto &e : _ccOrder)
        {
            const float t = params[e.second].getValue();
            int         v = (int)(t * 127.0f + 0.5f);
            v             = v < 0 ? 0 : (v > 127 ? 127 : v);
            if(_lastFeedbackCC[e.first] == (uint8_t)v)
                continue;
            _lastFeedbackCC[e.first] = (uint8_t)v;
            rack::midi::Message msg;
            msg.bytes[0] = 0xB0; // CC, channel 1
            msg.bytes[1] = e.first;
            msg.bytes[2] = (uint8_t)v;
            midiOutput.sendMessage(msg);
        }
    }

    /// Apply one (cc, value) pair — shared by the CC handler, APPLY_PATCH and
    /// preset recall.
    void _applyCC(uint8_t cc, uint8_t value)
    {
        cc &= 0x7f;
        value &= 0x7f;
        // The host already knows what it just sent us. Recording it as if we
        // had emitted it keeps the next feedback tick from sending it straight
        // back, which on a log parameter can land a step off after the 7-bit
        // round trip and visibly nudge the slider under the pointer.
        _lastFeedbackCC[cc] = value;

        const int8_t p = _ccToParam[cc];
        if(p >= 0)
        {
            params[p].setValue(value / 127.0f);
            return;
        }

        // Not a parameter — an action. Same set the firmware's
        // moduleHook_controlChange() handles, and it cannot shadow a manifest
        // CC because the table above was consulted first.
        if(cc == 123)
        {
            // All Notes Off / panic. The feedback loop is the only thing on
            // this module that can run away, so collapse it: gain to the bottom
            // of its travel and the echo's own feedback to zero.
            //
            // The firmware drops the gain to −60 dB, below anything the knob can
            // reach; here it lands on the knob's floor of −30 dB, which is the
            // same "off" as far as the loop is concerned and leaves the panel
            // telling the truth about where the parameter is.
            params[FBGAIN_PARAM].setValue(0.0f);
            params[ECHOFB_PARAM].setValue(0.0f);
            // Without this the collapse would glide over the gain's 50 ms t60.
            // That is not long, but a panic is the one gesture whose whole point
            // is to be immediate.
            _smoother.Snap();
        }
    }

    // -----------------------------------------------------------------------
    // Drain the input queue. Called once per frame so a message lands on the
    // frame it is timestamped for.
    // -----------------------------------------------------------------------
    void _readMidi(int64_t frame)
    {
        rack::midi::Message msg;
        while(midiInput.tryPop(&msg, frame))
        {
            const uint8_t status = msg.getStatus();
            if(status == 0x9 && msg.getValue() > 0)
            {
                // Note On. Alloy Coil is a drone/feedback instrument, not a
                // keyboard voice — there is no envelope and no gate, so a note
                // sets the resonator's pitch and leaves it there. That is what
                // upstream's Frequency parameter does, driven by a key instead
                // of a knob, and it is why Note Off is deliberately ignored:
                // releasing a key must not stop a drone sustaining on its own
                // feedback. Same rule as moduleHook_noteOn().
                const uint8_t note = msg.getNote();
                if(note >= 16 && note <= 72)
                    params[PITCH_PARAM].setValue((note - 16) / 56.0f);
            }
            else if(status == 0xb)
            {
                _applyCC(msg.getNote(), msg.getValue());
            }
            else if(!msg.bytes.empty() && msg.bytes[0] == 0xF0)
            {
                _handleSysEx(msg);
            }
        }
    }

    void _handleSysEx(const rack::midi::Message &msg)
    {
        // Shortest legal message is F0 7D <id0> <id1> <cmd> F7.
        if(msg.bytes.size() < 6 || msg.bytes[1] != kSysExMfr
           || msg.bytes.back() != 0xF7)
            return;

        const bool addressed
            = msg.bytes[2] == kSysExDev0 && msg.bytes[3] == kSysExDev1;
        const bool broadcast
            = msg.bytes[2] == kSysExAny && msg.bytes[3] == kSysExAny;
        if(!addressed && !broadcast)
            return;

        const uint8_t cmd = msg.bytes[4];
        const uint8_t arg = msg.bytes.size() >= 7 ? (msg.bytes[5] & 0x7f) : 0;

        // A broadcast reaches every module on the port, so it may only ask a
        // question — never change anything.
        if(broadcast && cmd != kSysExRequestDump)
            return;

        if(cmd == kSysExRequestDump)
        {
            _dumpPending.store(true);
        }
        else if(cmd == kSysExApplyPatch)
        {
            // Payload is interleaved (cc, value) from byte 5 up to the F7.
            for(size_t i = 5; i + 1 < msg.bytes.size() - 1; i += 2)
                _applyCC(msg.bytes[i], msg.bytes[i + 1]);
            _dumpPending.store(true);
            _smoother.Snap(); // a whole patch at once is a cut, not a glide
        }
        else if(cmd == kSysExPresetSave)
        {
            if(arg >= 1 && arg <= 9)
                _presets[arg - 1] = _snapshotPairs();
        }
        else if(cmd == kSysExPresetLoad)
        {
            if(arg >= 1 && arg <= 9 && !_presets[arg - 1].empty())
            {
                for(const auto &p : _presets[arg - 1])
                    _applyCC(p.first, p.second);
                _dumpPending.store(true);
                _smoother.Snap();
            }
        }
        else if(cmd == kSysExPresetReset)
        {
            // arg 0 resets the live patch, 1–9 clear one slot, 0x7F does both
            // for every slot — the same argument encoding as the firmware.
            if(arg == 0 || arg == 0x7f)
            {
                for(auto *pq : paramQuantities)
                    if(pq)
                        pq->reset();
                _dumpPending.store(true);
                _smoother.Snap();
            }
            if(arg >= 1 && arg <= 9)
                _presets[arg - 1].clear();
            else if(arg == 0x7f)
                for(auto &s : _presets)
                    s.clear();
        }
    }

    void process(const ProcessArgs &args) override
    {
        _readMidi(args.frame);
        if(_dumpPending.exchange(false))
            sendPatchDump();

        // Reading the panel into the goal values is control work, done at
        // ~1 kHz rather than per sample. Nothing here writes to the engine —
        // that is the smoother's job below, at its own rate, exactly as in the
        // firmware.
        if(_controlPhase++ >= kControlDiv)
        {
            _controlPhase = 0;
            fillCoilParams(_io);

            // LEDs, from the peaks accumulated since the previous tick. Same
            // call the firmware will make once AlloyCoil has an IHardwareIO.
            CoilLed::Signals sig;
            sig.peakL          = _peakL;
            sig.peakR          = _peakR;
            sig.exciter        = _peakExc;
            sig.exciterPatched = inputs[EXCITER_INPUT].isConnected();
            sig.shiftHeld      = _io.readButton(Btn::SHIFT);
            sig.warpHeld       = _io.readButton(Btn::WARP);
            _leds.update(sig, (float)(kControlDiv + 1) * args.sampleTime);
            _leds.writeTo(_io);
            _peakL = _peakR = _peakExc = 0.f;
        }

        // CC feedback at the firmware's 128 Hz, on its own divider — see
        // initForRate(). Sends nothing while no knob is moving.
        if(++_feedbackPhase >= _feedbackDiv)
        {
            _feedbackPhase = 0;
            sendCCFeedback();
        }

        // Parameter glide onto the goal values, with upstream's per-parameter
        // times. Same divider the firmware uses, so a knob move lands on the
        // same trajectory in Rack as it does on the module — which is the whole
        // point of the plugin being the A/B reference. See ControlSmoother.h.
        if(++_smoothPhase >= kSmoothFrames)
        {
            _smoothPhase = 0;
            _smoother.Step(_engine);
        }

        // Exciter, per sample — this is the part the firmware cannot do yet.
        //
        // Scaled by the *jack's* range, not Rack's ±5 V audio convention: the
        // hardware front end takes ±8 V (CvRange::kFmMaxV), so dividing by 5
        // here would make the same patch cable drive the string 4 dB harder in
        // Rack than on the module. A Eurorack source at ±5 V therefore only
        // reaches 0.625 — which is what EXCITE_PARAM's range past unity is for.
        const float exciter = inputs[EXCITER_INPUT].isConnected()
                                  ? inputs[EXCITER_INPUT].getVoltage()
                                        * CvRange::kFmToUnit
                                  : 0.0f;

        float outL = 0.f, outR = 0.f;
        _engine.Process(exciter, outL, outR);

        // The same output stage the firmware applies at its edge — upstream's
        // peak limiter. See OutputStage.h.
        _output.Process(outL, outR);

        outputs[L_OUTPUT].setVoltage(outL * 5.f);
        outputs[R_OUTPUT].setVoltage(outR * 5.f);

        // Peak-hold for the LEDs. Taken post-limiter, so the level pair shows
        // what leaves the module rather than what the engine wanted to send.
        const float aL = std::fabs(outL), aR = std::fabs(outR),
                    aE = std::fabs(exciter);
        if(aL > _peakL)
            _peakL = aL;
        if(aR > _peakR)
            _peakR = aR;
        if(aE > _peakExc)
            _peakExc = aE;
    }

    // -----------------------------------------------------------------------
    // Persist the MIDI port choice and the preset slots with the patch. The
    // firmware's equivalents live in flash; here they belong to the module
    // instance, so a saved rack comes back talking to the same port with the
    // same nine slots in it.
    // -----------------------------------------------------------------------
    json_t *dataToJson() override
    {
        json_t *rootJ = json_object();
        json_object_set_new(rootJ, "midiInput", midiInput.toJson());
        json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
        json_t *presetsJ = json_array();
        for(int s = 0; s < 9; s++)
        {
            json_t *slotJ = json_array();
            for(const auto &p : _presets[s])
            {
                json_t *pairJ = json_array();
                json_array_append_new(pairJ, json_integer(p.first));
                json_array_append_new(pairJ, json_integer(p.second));
                json_array_append_new(slotJ, pairJ);
            }
            json_array_append_new(presetsJ, slotJ);
        }
        json_object_set_new(rootJ, "presets", presetsJ);
        return rootJ;
    }

    void dataFromJson(json_t *rootJ) override
    {
        if(json_t *midiJ = json_object_get(rootJ, "midiInput"))
            midiInput.fromJson(midiJ);
        if(json_t *midiOutJ = json_object_get(rootJ, "midiOutput"))
            midiOutput.fromJson(midiOutJ);
        json_t *presetsJ = json_object_get(rootJ, "presets");
        if(presetsJ && json_is_array(presetsJ))
        {
            size_t  s;
            json_t *slotJ;
            json_array_foreach(presetsJ, s, slotJ)
            {
                if(s >= 9 || !json_is_array(slotJ))
                    continue;
                _presets[s].clear();
                size_t  p;
                json_t *pairJ;
                json_array_foreach(slotJ, p, pairJ)
                {
                    if(!json_is_array(pairJ) || json_array_size(pairJ) < 2)
                        continue;
                    _presets[s].push_back(
                        {(uint8_t)json_integer_value(json_array_get(pairJ, 0)),
                         (uint8_t)json_integer_value(json_array_get(pairJ, 1))});
                }
            }
        }
    }

  private:
    // ~1 kHz at 48 kHz host rate, close to the firmware's 128 Hz without
    // being so coarse that a knob feels stepped under Rack's smoothing.
    //
    // That last clause used to be doing real work: with no smoother, this
    // divider *was* the glide, and running it at the firmware's true 128 Hz
    // made Rack sound stepped in a way the module would have too. Now that both
    // interpolate at kSmoothFrames, this is only how often the panel is read,
    // and the exact figure no longer colours anything.
    static constexpr int kControlDiv = 47;

    /// Smoother step interval, in frames. Matches the firmware's kSmoothFrames
    /// so a knob move follows the same trajectory in both.
    static constexpr int kSmoothFrames = 32;

    infrasonic::FeedbackSynth::Engine          _engine;
    infrasonic::FeedbackSynth::ControlSmoother _smoother;
    infrasonic::FeedbackSynth::OutputStage     _output;
    VCVRackIO                                  _io;
    CoilLed::Engine                            _leds;
    int                                        _controlPhase = 0;
    int                                        _smoothPhase  = 0;

    /// CC feedback divider, in frames — set from the host rate so feedback
    /// runs at the firmware's 128 Hz whatever Rack is clocked at.
    int _feedbackDiv   = 375; // 128 Hz at 48 kHz, replaced by initForRate()
    int _feedbackPhase = 0;

    // Peak-hold accumulators, drained and reset on every control tick.
    float _peakL   = 0.f;
    float _peakR   = 0.f;
    float _peakExc = 0.f;
};

struct AlloyCoilWidget : ModuleWidget
{
    AlloyCoilWidget(AlloyCoil *module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/AlloyCoil.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(
            createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(
            Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(
            createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH,
                                         RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Positions come from PanelLayout, indexed by the same positional slot
        // the HAL uses. AlloyCoil and AlloyFlux share one PCB, so they share one
        // geometry — this is the only place it is written down.
        using namespace PanelLayout;

        // Top row: the resonator and its feedback loop.
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::PITCH), module, AlloyCoil::PITCH_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::FBBODY), module, AlloyCoil::FBBODY_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::FBGAIN), module, AlloyCoil::FBGAIN_PARAM));

        // Mid row: the echo.
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::ECHOTIME), module, AlloyCoil::ECHOTIME_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::ECHOSEND), module, AlloyCoil::ECHOSEND_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::ECHOFB), module, AlloyCoil::ECHOFB_PARAM));

        // Low row: space and tone. REV MIX and FB LPF each carry a
        // shift-secondary, exposed in the context menu below.
        //
        // Slot -> parameter is PanelMap's job, so these read straight: the
        // widget asks for the position of the parameter it draws. They were
        // briefly crossed here (REVMIX's slot drawing REVDECAY_PARAM) to get
        // the panel right after DECAY and MIX traded places, which looks
        // identical on screen and quietly breaks everything else that indexes
        // by slot — the shift pairs, PotTakeover, the future ADC driver.
        addParam(createParamCentered<Trimpot>(
            pot(Pot::REVDECAY), module, AlloyCoil::REVDECAY_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::REVMIX), module, AlloyCoil::REVMIX_PARAM));
        addParam(createParamCentered<Trimpot>(
            pot(Pot::FBLPF), module, AlloyCoil::FBLPF_PARAM));

        // --- Buttons (SW2 / SW3) ---
        addParam(createParamCentered<VCVButton>(
            button(Btn::WARP), module, AlloyCoil::WARP_PARAM));
        addParam(createParamCentered<VCVButton>(
            button(Btn::SHIFT), module, AlloyCoil::SHIFT_PARAM));

        // --- Jacks, in panel order (left to right, upper row then lower) ---
        // The panel labels the four modulation inputs CV 1..CV 4; what they
        // modulate is this module's choice. GATE stays unused, and FM IN is the
        // exciter — it sits between CV 3 and CV 4, not at the row's left end.
        addInput(createInputCentered<PJ301MPort>(
            at(kVOctMm), module, AlloyCoil::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kGateMm), module, AlloyCoil::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kMidiMm), module, AlloyCoil::MIDI_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kCv1Mm), module, AlloyCoil::FBBODY_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kCv2Mm), module, AlloyCoil::FBGAIN_CV_INPUT));

        addInput(createInputCentered<PJ301MPort>(
            at(kFmInMm), module, AlloyCoil::EXCITER_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kCv3Mm), module, AlloyCoil::ECHOSEND_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kCv4Mm), module, AlloyCoil::REVMIX_CV_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            at(kOutLMm), module, AlloyCoil::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            at(kOutRMm), module, AlloyCoil::R_OUTPUT));

        // --- LEDs (7 × RGB) — colour driven by CoilLed::Engine ---
        //
        // Aperture-shaped rather than Rack's round MediumLight: on the hardware
        // these are openings in the panel PCB's solder mask and the LED shines
        // through the bare substrate from behind. The outline is generated from
        // the panel artwork itself — see platform/vcv/PanelLed.hpp.
        using PanelLight = AlloyPanelLight<RedGreenBlueLight>;
        addChild(createLightCentered<PanelLight>(
            led(Led::LEVEL_L), module, AlloyCoil::LED1_R_LIGHT));
        addChild(createLightCentered<PanelLight>(
            led(Led::LEVEL_R), module, AlloyCoil::LED7_R_LIGHT));
        addChild(createLightCentered<PanelLight>(
            led(Led::LOOP_L), module, AlloyCoil::LED2_R_LIGHT));
        addChild(createLightCentered<PanelLight>(
            led(Led::LOOP_R), module, AlloyCoil::LED6_R_LIGHT));
        addChild(createLightCentered<PanelLight>(
            led(Led::ECHO), module, AlloyCoil::LED3_R_LIGHT));
        addChild(createLightCentered<PanelLight>(
            led(Led::SPACE), module, AlloyCoil::LED5_R_LIGHT));
        addChild(createLightCentered<PanelLight>(
            led(Led::CENTRE), module, AlloyCoil::LED4_R_LIGHT));
    }

    // The two SHIFT-secondaries. On hardware these are the same physical knob
    // read while SHIFT is held; Rack has no shift key to hold, so they live in
    // the context menu — the same arrangement AlloyFlux uses for its six.
    void appendContextMenu(Menu *menu) override
    {
        auto *m = dynamic_cast<AlloyCoil *>(module);
        if(!m)
            return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("SHIFT parameters"));

        menu->addChild(createMenuLabel("Volume  (SHIFT + REV MIX)"));
        auto *volSlider     = new SubMenuSlider;
        volSlider->quantity = m->getParamQuantity(AlloyCoil::VOL_PARAM);
        menu->addChild(volSlider);

        menu->addChild(createMenuLabel("Feedback HPF  (SHIFT + FB LPF)"));
        auto *hpfSlider     = new SubMenuSlider;
        hpfSlider->quantity = m->getParamQuantity(AlloyCoil::FBHPF_PARAM);
        menu->addChild(hpfSlider);

        menu->addChild(createMenuLabel("Exciter level  (SHIFT + FB GAIN)"));
        auto *excSlider     = new SubMenuSlider;
        excSlider->quantity = m->getParamQuantity(AlloyCoil::EXCITE_PARAM);
        menu->addChild(excSlider);

        // --- Sync + MIDI settings ---
        //
        // Pick the input and the output follows it, which is the whole setup on
        // a loopback port. "Sync to web" is the manual push for the case where
        // the page missed the automatic one — it sends the same PATCH_DUMP the
        // discovery probe asks for.
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Sync to web →",
                                      "Send PATCH_DUMP to web configurator",
                                      [=]() { m->_dumpPending.store(true); }));
        menu->addChild(createSubmenuItem(
            "MIDI Settings",
            "",
            [=](Menu *submenu)
            {
                submenu->addChild(createMenuLabel("MIDI Input"));
                rack::app::appendMidiMenu(submenu, &m->midiInput);
                submenu->addChild(new MenuSeparator);
                submenu->addChild(createMenuLabel("MIDI Output (web sync)"));
                rack::app::appendMidiMenu(submenu, &m->midiOutput);
            }));
    }
};

Model *modelAlloyCoil = createModel<AlloyCoil, AlloyCoilWidget>("AlloyCoil");
