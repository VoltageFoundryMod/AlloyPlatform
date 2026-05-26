#include "SubMenuSlider.hpp"
#include "SynthEngine.h"
#include "VCVRackIO.h" // VCV-specific IHardwareIO implementation (M37d)
#include "VoiceMode.h"
#include "dsp/ChorusEngine.h" // ChorusMode enum
#include "dsp/CurveEngine.h"  // EnvelopeType enum
#include "dsp/FilterEngine.h" // FilterMode, FilterType enums
#include "io/IOBridge.h"      // fillSynthParams() shared bridge
#include "plugin.hpp"
#include "scale_quantizer.h"   // M37j: ScaleId, quantizeNote()
#include <app/MidiDisplay.hpp> // M37i: appendMidiMenu()
#include <atomic>

// ---------------------------------------------------------------------------
// Hardware globals declared extern in params.h.
// In the VCV build, main.cpp is not compiled so we provide defaults here.
// ---------------------------------------------------------------------------
volatile bool gGatePatched = false;
volatile bool gGateHigh = false;

// Performance counters referenced by params.h; unused in VCV.
volatile bool gPerformancePrintEnabled = false;
volatile uint32_t gAudioElapsedUs = 0;
volatile uint32_t gAudioOverruns = 0;

// ---------------------------------------------------------------------------
// AlloyFlux VCV Rack module — M37e (all knobs + CV jacks)
// ---------------------------------------------------------------------------
struct AlloyFlux : Module {
    // -----------------------------------------------------------------------
    // Panel knobs (7 — mirrors hardware layout exactly)
    // SHIFT-secondary params are hidden from the panel; exposed in context menu.
    enum ParamId {
        ROOT_PARAM,     // [-4, 4] V/Oct
        RELATION_PARAM, // [0, 1] → 0–24 semitones in IOBridge
        SHAPE_PARAM,    // [0, 1]
        MOTION_PARAM,   // [0, 1]
        COLOR_PARAM,    // [0, 1]
        CURVE_PARAM,    // [0, 1]
        SPACE_PARAM,    // [0, 1] → 0–2 stereo width in IOBridge
        // SHIFT-secondary — context menu drag sliders (SHIFT+SHAPE/MOTION/SPACE)
        FATNESS_PARAM,    // [0, 1], default 0.4
        DRIFTSPEED_PARAM, // [0, 1], default 0.36  (~0.04 coeff normalised)
        VOL_PARAM,        // [0, 1], default 1.0
        CURVETIME_PARAM,  // [0.25, 4.0], default 1.0  (CC 88 — envelope time scale)
        // Panel buttons
        MODE_PARAM,  // momentary — cycles voice mode on release
        SHIFT_PARAM, // momentary — toggles drone mode (VCV only)
        // ---- M37g: Envelope (hidden, saved in patch) ----
        ENV_TYPE_PARAM,     // 0=AR, 1=ADSR
        ADSR_ATTACK_PARAM,  // [0.001, 4.0] s, default 0.05
        ADSR_DECAY_PARAM,   // [0.001, 4.0] s, default 0.10
        ADSR_SUSTAIN_PARAM, // [0.0, 1.0], default 0.8
        ADSR_RELEASE_PARAM, // [0.001, 8.0] s, default 0.30
        ADSR_LOOP_PARAM,    // 0=off, 1=loop
        // ---- M37h: Effects (hidden, saved in patch) ----
        CHORUS_MODE_PARAM,   // 0=OFF,1=I,2=II,3=I_II
        FILTER_MODE_PARAM,   // 0=OFF,1=LP,2=HP,3=BP,4=NOTCH,5=LP4
        FILTER_TYPE_PARAM,   // 0=SVF,1=LADDER
        FILTER_CUTOFF_PARAM, // [20, 16000] Hz, default 8000
        FILTER_RES_PARAM,    // [0, 1], default 0
        REV_MIX_PARAM,       // [0, 1], default 0
        REV_SIZE_PARAM,      // [0, 1], default 0.5
        REV_DAMPING_PARAM,   // [0, 1], default 0.5
        REV_MOD_SPEED_PARAM, // [0.1, 4] Hz, default 1.0
        REV_MOD_DEPTH_PARAM, // [0, 1], default 0.
        DELAY_MIX_PARAM,     // [0, 1], default 0
        DELAY_TIME_PARAM,    // [1, 1000] ms, default 100
        DELAY_FB_PARAM,      // [0, 0.99], default 0.5
        // ---- FX chain ordering ----
        FX_FILTER_POS_PARAM, // 0=pre-chorus, 1=post-chorus
        FX_DELAY_POS_PARAM,  // 0=pre-reverb, 1=post-reverb
        // ---- M37j: Scale quantizer (hidden, saved in patch) ----
        SCALE_PARAM,     // 0–14, ScaleId enum, default 0 (Chromatic = bypass)
        TRANSPOSE_PARAM, // [-24, 24] semitones, default 0
        // ---- M37m: Portamento, sub-octave, rev freeze, vel sens (hidden) ----
        GLIDE_ENABLE_PARAM, // 0=off, 1=on
        GLIDE_TIME_PARAM,   // [0, 2] s, default 0
        SUB_OCTAVE_PARAM,   // 0=1 oct below, 1=2 oct below
        REV_FROZEN_PARAM,   // 0=off, 1=frozen
        VEL_SENS_PARAM,     // 0=off (fixed 1.0), 1=on (follows MIDI vel)
        PARAMS_LEN
    };

    // CV input jacks (7)
    enum InputId {
        VOCT_INPUT,
        GATE_INPUT,
        MIDI_INPUT, // hardware TRS MIDI jack — visual only in VCV (software MIDI via InputQueue)
        REL_CV_INPUT,
        SHP_CV_INPUT,
        MTN_CV_INPUT,
        SPC_CV_INPUT,
        FM_IN_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        L_OUTPUT,
        R_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightId {
        LIGHTS_LEN
    };

    // -----------------------------------------------------------------------
    SynthEngine _engine;
    VCVRackIO _io;
    PolySlot _polySlots[6] = {
        {440.0f, 1.0f, 255},
        {440.0f, 1.0f, 255},
        {440.0f, 1.0f, 255},
        {440.0f, 1.0f, 255},
        {440.0f, 1.0f, 255},
        {440.0f, 1.0f, 255},
    };
    uint8_t _polyRR = 0;
    uint8_t _cvPolySlot = 255;                            // poly slot currently held by a CV gate trigger (255 = none)
    float _midiVelocity = 1.0f;                           // mono-mode last note velocity (0–1)
    std::vector<std::pair<uint8_t, uint8_t>> _presets[9]; // user preset slots 1–9; empty = not yet saved
    SynthParams _params;
    int _controlCounter = 0;
    int _controlDiv = 344; // ~128 Hz at 44100

    // Button state — edge detection at full sample rate
    VoiceMode _voiceMode = VoiceMode::PAIR;
    bool _droneMode = true; // true = free-running (no envelope gating)
    bool _modeWasDown = false;
    bool _shiftWasDown = false;
    bool _droneComboFired = false;
    bool _modeConsumed = false;

    // M37h: 1-frame dry buffer for inline reverb (avoids Core 1 split)
    int32_t _prevDryL = 0;
    int32_t _prevDryR = 0;

    // M37i: MIDI input/output
    rack::midi::InputQueue midiInput;
    rack::midi::Output midiOutput;         // outbound — PATCH_DUMP replies
    std::atomic<bool> _dumpPending{false}; // set from any thread, consumed in process()
    float _midiVoct = 0.f;
    bool _midiGate = false;
    bool _midiKeyHeld = false; // physical key still down
    bool _midiSustain = false;
    int _midiNote = -1;           // -1 = no note active (gate logic only)
    bool _midiEverPlayed = false; // stays true once any NoteOn received; holds pitch through release
    bool _prevGateHigh = false;   // tracks gate CV state for rising-edge drone exit

    // M37j: scale quantizer (synced from params each frame)
    ScaleId _quantizeScale = ScaleId::CHROMATIC;
    int8_t _transpose = 0;

    // CC feedback cache: 0xFF = never sent (forces first-tick emission)
    uint8_t _lastFeedbackCC[128];

    AlloyFlux() : _io(this) {
        memset(_lastFeedbackCC, 0xFF, sizeof(_lastFeedbackCC));
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Panel knobs
        configParam(ROOT_PARAM, -4.0f, 4.0f, 0.0f, "Root pitch", " V");
        configParam(RELATION_PARAM, 0.0f, 1.0f, 0.0f, "Relation");
        configParam(SHAPE_PARAM, 0.0f, 1.0f, 0.0f, "Shape");
        configParam(MOTION_PARAM, 0.0f, 1.0f, 0.0f, "Motion");
        configParam(COLOR_PARAM, 0.0f, 1.0f, 0.0f, "Color");
        configParam(CURVE_PARAM, 0.0f, 1.0f, 0.5f, "Curve");
        configParam(SPACE_PARAM, 0.0f, 1.0f, 0.5f, "Space");

        // Context menu secondary params (hidden from panel, saved in patch)
        configParam(FATNESS_PARAM, 0.0f, 1.0f, 0.4f, "Fatness");
        configParam(DRIFTSPEED_PARAM, 0.0f, 1.0f, 0.36f, "Drift speed");
        configParam(VOL_PARAM, 0.0f, 1.0f, 1.0f, "Volume");
        configParam(CURVETIME_PARAM, 0.25f, 4.0f, 1.0f, "Curve time", "\u00d7");

        // CV inputs
        configInput(VOCT_INPUT, "V/Oct");
        configInput(GATE_INPUT, "Gate");
        configInput(MIDI_INPUT, "MIDI (TRS — use context menu for software MIDI)");
        configInput(REL_CV_INPUT, "Relation CV");
        configInput(SHP_CV_INPUT, "Shape CV");
        configInput(MTN_CV_INPUT, "Motion CV");
        configInput(SPC_CV_INPUT, "Space CV");
        configInput(FM_IN_INPUT, "FM / Color CV");

        // Outputs
        configOutput(L_OUTPUT, "Left");
        configOutput(R_OUTPUT, "Right");

        // IO mappings — primary knobs (ROOT normalised via [-4,4] → 0–1)
        _io.assignPot(PotId::ROOT, ROOT_PARAM, -4.0f, 4.0f);
        _io.assignPot(PotId::RELATION, RELATION_PARAM, 0.0f, 1.0f);
        _io.assignPot(PotId::SHAPE, SHAPE_PARAM, 0.0f, 1.0f);
        _io.assignPot(PotId::MOTION, MOTION_PARAM, 0.0f, 1.0f);
        _io.assignPot(PotId::COLOR, COLOR_PARAM, 0.0f, 1.0f);
        _io.assignPot(PotId::CURVE, CURVE_PARAM, 0.0f, 1.0f);
        _io.assignPot(PotId::SPACE, SPACE_PARAM, 0.0f, 1.0f);
        // IO mappings — secondary (context menu)
        _io.assignPot(PotId::FATNESS, FATNESS_PARAM, 0.0f, 1.0f);
        _io.assignPot(PotId::DRIFTSPEED, DRIFTSPEED_PARAM, 0.0f, 1.0f);
        _io.assignPot(PotId::VOL, VOL_PARAM, 0.0f, 1.0f);

        // IO mappings — CV jacks
        _io.assignCV(CVId::VOCT, VOCT_INPUT);
        _io.assignCV(CVId::GATE, GATE_INPUT);
        _io.assignCV(CVId::REL_CV, REL_CV_INPUT);
        _io.assignCV(CVId::SHP_CV, SHP_CV_INPUT);
        _io.assignCV(CVId::MTN_CV, MTN_CV_INPUT);
        _io.assignCV(CVId::SPC_CV, SPC_CV_INPUT);
        _io.assignCV(CVId::FM_IN, FM_IN_INPUT);

        // Buttons
        configButton(MODE_PARAM, "Mode");
        configButton(SHIFT_PARAM, "Drone mode");
        _io.assignButton(ButtonId::MODE, MODE_PARAM);
        _io.assignButton(ButtonId::SHIFT, SHIFT_PARAM);

        // M37g — Envelope (hidden)
        configSwitch(ENV_TYPE_PARAM, 0.f, 1.f, 0.f, "Envelope type", {"AR", "ADSR"});
        configParam(ADSR_ATTACK_PARAM, 0.001f, 4.0f, 0.05f, "Attack", " s");
        configParam(ADSR_DECAY_PARAM, 0.001f, 4.0f, 0.10f, "Decay", " s");
        configParam(ADSR_SUSTAIN_PARAM, 0.0f, 1.0f, 0.8f, "Sustain");
        configParam(ADSR_RELEASE_PARAM, 0.001f, 8.0f, 0.30f, "Release", " s");
        configSwitch(ADSR_LOOP_PARAM, 0.f, 1.f, 0.f, "Envelope loop", {"Off", "On"});

        // M37h — Effects (hidden)
        configSwitch(CHORUS_MODE_PARAM, 0.f, 3.f, 3.f, "Chorus mode",
                     {"Off", "Chorus I", "Chorus II", "Chorus I+II"});
        configSwitch(FILTER_MODE_PARAM, 0.f, 5.f, 0.f, "Filter mode",
                     {"Off", "LP", "HP", "BP", "Notch", "LP4"});
        configSwitch(FILTER_TYPE_PARAM, 0.f, 1.f, 0.f, "Filter type", {"SVF", "Ladder"});
        configParam(FILTER_CUTOFF_PARAM, 20.f, 16000.f, 839.f, "Filter cutoff", " Hz");
        configParam(FILTER_RES_PARAM, 0.f, 1.f, 0.f, "Filter resonance");
        configParam(REV_MIX_PARAM, 0.f, 1.f, 0.f, "Reverb mix");
        configParam(REV_SIZE_PARAM, 0.f, 1.f, 0.5f, "Reverb size");
        configParam(REV_DAMPING_PARAM, 0.f, 1.f, 0.5f, "Reverb damping");
        configParam(REV_MOD_SPEED_PARAM, 0.1f, 4.0f, 1.0f, "Reverb mod speed");
        configParam(REV_MOD_DEPTH_PARAM, 0.0f, 1.0f, 1.0f, "Reverb mod depth");
        configParam(DELAY_MIX_PARAM, 0.f, 1.f, 0.f, "Delay mix");
        configParam(DELAY_TIME_PARAM, 1.f, 500.f, 100.f, "Delay time", " ms");
        configParam(DELAY_FB_PARAM, 0.f, 0.99f, 0.5f, "Delay feedback");
        configSwitch(FX_FILTER_POS_PARAM, 0.f, 1.f, 0.f, "Filter position", {"Pre Chorus", "Post Chorus"});
        configSwitch(FX_DELAY_POS_PARAM, 0.f, 1.f, 0.f, "Delay position", {"Pre Reverb", "Post Reverb"});

        // M37j — Scale quantizer (hidden)
        configSwitch(SCALE_PARAM, 0.f, 14.f, 0.f, "Input Quantizer",
                     {"Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor",
                      "Pentatonic Maj", "Pentatonic Min", "Blues", "Dorian", "Phrygian",
                      "Lydian", "Mixolydian", "Locrian", "Whole Tone", "Diminished"});
        configParam(TRANSPOSE_PARAM, -24.f, 24.f, 0.f, "Transpose", " st");
        // M37m — Portamento / glide, sub-octave, reverb freeze, velocity sensitivity
        configSwitch(GLIDE_ENABLE_PARAM, 0.f, 1.f, 0.f, "Portamento", {"Off", "On"});
        configParam(GLIDE_TIME_PARAM, 0.0f, 2.0f, 0.0f, "Glide time", " s");
        configSwitch(SUB_OCTAVE_PARAM, 0.f, 1.f, 0.f, "Sub octave", {"1 oct below", "2 oct below"});
        configSwitch(REV_FROZEN_PARAM, 0.f, 1.f, 0.f, "Reverb freeze", {"Off", "Frozen"});
        configSwitch(VEL_SENS_PARAM, 0.f, 1.f, 1.f, "Velocity sensitivity", {"Off", "On"});
    }

    void onSampleRateChange(const SampleRateChangeEvent &e) override {
        uint32_t sr = (uint32_t)e.sampleRate;
        _engine.setSampleRate(sr);
        _controlDiv = (int)(e.sampleRate / 128.0f + 0.5f);
        if (_controlDiv < 1)
            _controlDiv = 1;
    }

    void onAdd(const AddEvent &e) override {
        uint32_t sr = (uint32_t)APP->engine->getSampleRate();
        _engine.init(sr, 128u);
        _controlDiv = (int)(sr / 128.0f + 0.5f);
        if (_controlDiv < 1)
            _controlDiv = 1;
    }

    void onReset(const ResetEvent &e) override {
        Module::onReset(e);
        _voiceMode = VoiceMode::PAIR;
        _droneMode = true;
        _dumpPending.store(true);
    }

    // -----------------------------------------------------------------------
    // CC feedback — emit changed parameters to midiOutput at control rate.
    // Builds the same CC map as sendPatchDump() but only sends CCs whose
    // 7-bit value has changed since the last call.  Typical cost: 0-3 sends
    // per 128-sample control tick (one knob move = 1 CC per tick).
    // Feedback loop is benign: web sends CC → VCV param → same CC value →
    // cache hit → nothing sent back.
    // -----------------------------------------------------------------------
    void sendCCFeedback() {
        syncMidiOutput();
        if (midiOutput.deviceId < 0)
            return;

        // Helper: clamp float to [0,1] 7-bit CC
        auto cc7 = [](float lo, float hi, float val) -> uint8_t {
            float t = (val - lo) / (hi - lo);
            int v = (int)(t * 127.f + 0.5f);
            return (uint8_t)(v < 0 ? 0 : v > 127 ? 127
                                                 : v);
        };

        struct Feed {
            uint8_t cc;
            uint8_t val;
        };
        // Build snapshot — same order/encoding as sendPatchDump()
        Feed snap[] = {
            {16, cc7(-4.f, 4.f, params[ROOT_PARAM].getValue())},
            {1, cc7(0.f, 1.f, params[MOTION_PARAM].getValue())},
            {7, cc7(0.f, 1.f, params[VOL_PARAM].getValue())},
            {8, cc7(0.f, 1.f, params[SPACE_PARAM].getValue())},
            {78, cc7(0.f, 1.f, params[SHAPE_PARAM].getValue())},
            {84, cc7(0.f, 1.f, params[FATNESS_PARAM].getValue())},
            {89, cc7(0.f, 1.f, params[DRIFTSPEED_PARAM].getValue())},
            {92, cc7(0.f, 1.f, params[COLOR_PARAM].getValue())},
            {94, cc7(0.f, 1.f, params[RELATION_PARAM].getValue())},
            {71, cc7(0.f, 1.f, params[CURVE_PARAM].getValue())},
            {88, cc7(0.25f, 4.f, params[CURVETIME_PARAM].getValue())},
            {81, (uint8_t)(params[ENV_TYPE_PARAM].getValue() >= 0.5f ? 96 : 0)},
            {73, cc7(0.001f, 4.f, params[ADSR_ATTACK_PARAM].getValue())},
            {82, cc7(0.001f, 4.f, params[ADSR_DECAY_PARAM].getValue())},
            {83, cc7(0.f, 1.f, params[ADSR_SUSTAIN_PARAM].getValue())},
            {72, cc7(0.001f, 8.f, params[ADSR_RELEASE_PARAM].getValue())},
            {75, cc7(0.f, 1.f, params[FILTER_RES_PARAM].getValue())},
            {77, (uint8_t)(params[FILTER_TYPE_PARAM].getValue() >= 0.5f ? 96 : 0)},
            {91, cc7(0.f, 1.f, params[REV_MIX_PARAM].getValue())},
            {117, cc7(0.f, 1.f, params[REV_SIZE_PARAM].getValue())},
            {118, cc7(0.f, 1.f, params[REV_DAMPING_PARAM].getValue())},
            {112, cc7(0.1f, 4.f, params[REV_MOD_SPEED_PARAM].getValue())},
            {113, cc7(0.f, 1.f, params[REV_MOD_DEPTH_PARAM].getValue())},
            {95, cc7(0.f, 1.f, params[DELAY_MIX_PARAM].getValue())},
            {86, cc7(10.f, 500.f, params[DELAY_TIME_PARAM].getValue())},
            {87, cc7(0.f, 0.95f, params[DELAY_FB_PARAM].getValue())},
            {79, (uint8_t)(params[FX_FILTER_POS_PARAM].getValue() >= 0.5f ? 96 : 0)},
            {80, (uint8_t)(params[FX_DELAY_POS_PARAM].getValue() >= 0.5f ? 96 : 0)},
            {103, (uint8_t)std::max(0, std::min(14, (int)params[SCALE_PARAM].getValue()))},
            {104, (uint8_t)std::max(0, std::min(127, (int)roundf(params[TRANSPOSE_PARAM].getValue()) + 24))},
            // Voice mode: enum → band midpoint
            {115, [&]() -> uint8_t {
                       static const uint8_t m[] = {10,31,52,73,94,116};
                       return m[std::max(0,std::min(5,(int)_voiceMode))]; }()},
            // Filter mode: stored index 0-4 → 5-band midpoints
            {76, [&]() -> uint8_t {
                       static const uint8_t m[] = {12,38,63,89,114};
                       int fm = std::max(0,std::min(4,(int)params[FILTER_MODE_PARAM].getValue()));
                       return m[fm]; }()},
            // Chorus mode: stored index 0-3
            {93, [&]() -> uint8_t {
                       static const uint8_t m[] = {15,48,80,112};
                       int cm = std::max(0,std::min(3,(int)params[CHORUS_MODE_PARAM].getValue()));
                       return m[cm]; }()},
            // Filter cutoff: log inverse
            {74, [&]() -> uint8_t {
                       float hz = std::max(20.f,std::min(16000.f,params[FILTER_CUTOFF_PARAM].getValue()));
                       int v = (int)(std::log(hz/20.f)/std::log(800.f)*127.f+0.5f);
                       return (uint8_t)std::max(0,std::min(127,v)); }()},
        };

        for (auto &f : snap) {
            if (_lastFeedbackCC[f.cc] != f.val) {
                _lastFeedbackCC[f.cc] = f.val;
                rack::midi::Message msg;
                msg.bytes[0] = 0xB0; // CC on ch 1
                msg.bytes[1] = f.cc;
                msg.bytes[2] = f.val;
                midiOutput.sendMessage(msg);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Auto-configure midiOutput to the output device whose name matches
    // midiInput.  On loopback drivers (loopMIDI / IAC Bus / Windows MIDI)
    // the input and output port share the same device name, so this "just
    // works" without any manual MIDI Settings setup.
    // Safe to call from the audio thread: only runs once per session
    // (guarded by deviceId >= 0), after which it's a single int compare.
    // -----------------------------------------------------------------------
    void syncMidiOutput() {
        if (midiOutput.deviceId >= 0)
            return; // already configured
        if (midiInput.driverId < 0)
            return; // input has no driver
        if (midiInput.deviceId < 0)
            return; // input has no device
        auto *drv = rack::midi::getDriver(midiInput.driverId);
        if (!drv)
            return;
        std::string wantName = drv->getInputDeviceName(midiInput.deviceId);
        if (wantName.empty())
            return;
        if (midiOutput.driverId != midiInput.driverId)
            midiOutput.setDriverId(midiInput.driverId);
        for (int outId : drv->getOutputDeviceIds()) {
            if (drv->getOutputDeviceName(outId) == wantName) {
                midiOutput.setDeviceId(outId);
                return;
            }
        }
    }

    // -----------------------------------------------------------------------
    // M37i-out: PATCH_DUMP SysEx sent on midiOutput so the web configurator
    // can sync its displayed values to the current plugin state.
    // Called exclusively from the audio thread (via _dumpPending flag).
    // -----------------------------------------------------------------------
    // Build a snapshot of the current patch as raw CC pairs.
    // Shared by sendPatchDump(), PRESET_SAVE, and APPLY_PATCH.
    std::vector<std::pair<uint8_t, uint8_t>> _snapshotPairs() {
        using P = std::pair<uint8_t, uint8_t>;
        std::vector<P> out;
        out.reserve(42);
        auto cc7 = [&](uint8_t cc, float lo, float hi, float val) {
            int v = (int)std::round((val - lo) / (hi - lo) * 127.f);
            out.push_back({cc, (uint8_t)std::max(0, std::min(127, v))});
        };
        cc7(16, -4.f, 4.f, params[ROOT_PARAM].getValue());
        cc7(1, 0.f, 1.f, params[MOTION_PARAM].getValue());
        cc7(7, 0.f, 1.f, params[VOL_PARAM].getValue());
        cc7(8, 0.f, 1.f, params[SPACE_PARAM].getValue());
        cc7(78, 0.f, 1.f, params[SHAPE_PARAM].getValue());
        cc7(84, 0.f, 1.f, params[FATNESS_PARAM].getValue());
        cc7(89, 0.f, 1.f, params[DRIFTSPEED_PARAM].getValue());
        cc7(92, 0.f, 1.f, params[COLOR_PARAM].getValue());
        cc7(94, 0.f, 1.f, params[RELATION_PARAM].getValue());
        cc7(71, 0.f, 1.f, params[CURVE_PARAM].getValue());
        cc7(88, 0.25f, 4.0f, params[CURVETIME_PARAM].getValue());
        out.push_back({81, params[ENV_TYPE_PARAM].getValue() >= 0.5f ? (uint8_t)96 : (uint8_t)0});
        cc7(73, 0.001f, 4.0f, params[ADSR_ATTACK_PARAM].getValue());
        cc7(82, 0.001f, 4.0f, params[ADSR_DECAY_PARAM].getValue());
        cc7(83, 0.f, 1.f, params[ADSR_SUSTAIN_PARAM].getValue());
        cc7(72, 0.001f, 8.0f, params[ADSR_RELEASE_PARAM].getValue());
        {
            float hz = std::max(20.f, std::min(16000.f, params[FILTER_CUTOFF_PARAM].getValue()));
            int v = (int)std::round(std::log(hz / 20.f) / std::log(800.f) * 127.f);
            out.push_back({74, (uint8_t)std::max(0, std::min(127, v))});
        }
        cc7(75, 0.f, 1.f, params[FILTER_RES_PARAM].getValue());
        {
            static const uint8_t kFMMid[] = {12, 38, 63, 89, 114};
            int fm = std::max(0, std::min(4, (int)params[FILTER_MODE_PARAM].getValue()));
            out.push_back({76, kFMMid[fm]});
        }
        out.push_back({77, params[FILTER_TYPE_PARAM].getValue() >= 0.5f ? (uint8_t)96 : (uint8_t)0});
        {
            static const uint8_t kCMMid[] = {15, 48, 80, 112};
            int cm = std::max(0, std::min(3, (int)params[CHORUS_MODE_PARAM].getValue()));
            out.push_back({93, kCMMid[cm]});
        }
        cc7(91, 0.f, 1.f, params[REV_MIX_PARAM].getValue());
        cc7(117, 0.f, 1.f, params[REV_SIZE_PARAM].getValue());
        cc7(118, 0.f, 1.f, params[REV_DAMPING_PARAM].getValue());
        cc7(112, 0.1f, 4.f, params[REV_MOD_SPEED_PARAM].getValue());
        cc7(113, 0.f, 1.f, params[REV_MOD_DEPTH_PARAM].getValue());
        cc7(86, 10.f, 500.f, params[DELAY_TIME_PARAM].getValue());
        {
            float fb = params[DELAY_FB_PARAM].getValue();
            int v = (int)std::round(fb / 0.95f * 127.f);
            out.push_back({87, (uint8_t)std::max(0, std::min(127, v))});
        }
        cc7(95, 0.f, 1.f, params[DELAY_MIX_PARAM].getValue());
        out.push_back({79, params[FX_FILTER_POS_PARAM].getValue() >= 0.5f ? (uint8_t)96 : (uint8_t)0});
        out.push_back({80, params[FX_DELAY_POS_PARAM].getValue() >= 0.5f ? (uint8_t)96 : (uint8_t)0});
        {
            int sc = std::max(0, std::min(14, (int)params[SCALE_PARAM].getValue()));
            out.push_back({103, (uint8_t)sc});
        }
        {
            int tr = (int)std::round(params[TRANSPOSE_PARAM].getValue());
            out.push_back({104, (uint8_t)std::max(0, std::min(127, tr + 24))});
        }
        {
            static const uint8_t kVMMid[] = {10, 31, 52, 73, 94, 116};
            int vm = std::max(0, std::min(5, (int)_voiceMode));
            out.push_back({115, kVMMid[vm]});
        }
        cc7(5, 0.f, 2.f, params[GLIDE_TIME_PARAM].getValue());
        out.push_back({65, params[GLIDE_ENABLE_PARAM].getValue() >= 0.5f ? (uint8_t)96 : (uint8_t)0});
        out.push_back({90, params[SUB_OCTAVE_PARAM].getValue() >= 0.5f ? (uint8_t)96 : (uint8_t)0});
        out.push_back({102, params[VEL_SENS_PARAM].getValue() >= 0.5f ? (uint8_t)96 : (uint8_t)0});
        out.push_back({114, params[REV_FROZEN_PARAM].getValue() >= 0.5f ? (uint8_t)96 : (uint8_t)0});
        if (_droneMode)
            out.push_back({119, (uint8_t)96});
        return out;
    }

    void sendPatchDump() {
        syncMidiOutput();
        const auto pairs = _snapshotPairs();
        std::vector<uint8_t> buf;
        buf.reserve(6 + pairs.size() * 2);
        buf.push_back(0xF0);
        buf.push_back(0x7D);
        buf.push_back(0x41);
        buf.push_back(0x46);
        buf.push_back(0x02); // PATCH_DUMP
        for (auto &p : pairs) {
            buf.push_back(p.first);
            buf.push_back(p.second);
        }
        buf.push_back(0xF7);
        rack::midi::Message msg;
        msg.bytes.assign(buf.begin(), buf.end());
        midiOutput.sendMessage(msg);
    }

    // Apply a single CC/value pair — shared by MIDI CC handler, APPLY_PATCH, and PRESET_LOAD.
    void _applyCC(uint8_t cc, uint8_t value) {
        float norm = value / 127.0f;
        switch (cc) {
        case 16:
            params[ROOT_PARAM].setValue(-4.0f + norm * 8.0f);
            break;
        case 1:
            params[MOTION_PARAM].setValue(norm);
            break;
        case 7:
            params[VOL_PARAM].setValue(norm);
            break;
        case 8:
            params[SPACE_PARAM].setValue(norm);
            break;
        case 64:
            // Web "Drone" button sends CC 64 value 127.
            // Treat as a drone-mode trigger only — do NOT set _midiSustain,
            // otherwise notes never release (web never sends CC 64 off).
            if (value >= 64)
                _droneMode = true;
            break;
        case 78:
            params[SHAPE_PARAM].setValue(norm);
            break;
        case 84:
            params[FATNESS_PARAM].setValue(norm);
            break;
        case 89:
            params[DRIFTSPEED_PARAM].setValue(norm);
            break;
        case 92:
            params[COLOR_PARAM].setValue(norm);
            break;
        case 94:
            params[RELATION_PARAM].setValue(norm);
            break;
        case 71:
            params[CURVE_PARAM].setValue(norm);
            break;
        case 88:
            params[CURVETIME_PARAM].setValue(0.25f + norm * 3.75f);
            break;
        case 81:
            params[ENV_TYPE_PARAM].setValue(value < 64 ? 0.f : 1.f);
            break;
        case 73:
            params[ADSR_ATTACK_PARAM].setValue(0.001f + norm * 3.999f);
            break;
        case 82:
            params[ADSR_DECAY_PARAM].setValue(0.001f + norm * 3.999f);
            break;
        case 83:
            params[ADSR_SUSTAIN_PARAM].setValue(norm);
            break;
        case 72:
            params[ADSR_RELEASE_PARAM].setValue(0.001f + norm * 7.999f);
            break;
        case 74:
            params[FILTER_CUTOFF_PARAM].setValue(20.0f * powf(800.0f, norm));
            break;
        case 75:
            params[FILTER_RES_PARAM].setValue(norm);
            break;
        case 76: {
            int fm = value < 26 ? 0 : value < 51 ? 1
                                  : value < 77   ? 2
                                  : value < 102  ? 3
                                                 : 4;
            params[FILTER_MODE_PARAM].setValue((float)fm);
            break;
        }
        case 77:
            params[FILTER_TYPE_PARAM].setValue(value < 64 ? 0.f : 1.f);
            break;
        case 93: {
            int cm = value < 32 ? 0 : value < 64 ? 1
                                  : value < 96   ? 2
                                                 : 3;
            params[CHORUS_MODE_PARAM].setValue((float)cm);
            break;
        }
        case 91:
            params[REV_MIX_PARAM].setValue(norm);
            break;
        case 117:
            params[REV_SIZE_PARAM].setValue(norm);
            break;
        case 118:
            params[REV_DAMPING_PARAM].setValue(norm);
            break;
        case 112:
            params[REV_MOD_SPEED_PARAM].setValue(0.1f + norm * 3.9f);
            break;
        case 113:
            params[REV_MOD_DEPTH_PARAM].setValue(norm);
            break;
        case 86:
            params[DELAY_TIME_PARAM].setValue(10.0f + norm * 490.0f);
            break;
        case 87:
            params[DELAY_FB_PARAM].setValue(norm * 0.95f);
            break;
        case 95:
            params[DELAY_MIX_PARAM].setValue(norm);
            break;
        case 79:
            params[FX_FILTER_POS_PARAM].setValue(value < 64 ? 0.f : 1.f);
            break;
        case 80:
            params[FX_DELAY_POS_PARAM].setValue(value < 64 ? 0.f : 1.f);
            break;
        case 103: {
            uint8_t sv = value < (uint8_t)ScaleId::COUNT ? value : 0;
            params[SCALE_PARAM].setValue((float)sv);
            break;
        }
        case 104:
            params[TRANSPOSE_PARAM].setValue((float)((int)value - 24));
            break;
        case 115: {
            VoiceMode vm;
            if (value <= 20)
                vm = VoiceMode::PAIR;
            else if (value <= 41)
                vm = VoiceMode::CLOUD;
            else if (value <= 62)
                vm = VoiceMode::CHORD;
            else if (value <= 83)
                vm = VoiceMode::CASCADE;
            else if (value <= 104)
                vm = VoiceMode::STRING;
            else
                vm = VoiceMode::POLY;
            _voiceMode = vm;
            break;
        }
        case 5:
            params[GLIDE_TIME_PARAM].setValue(norm * 2.0f);
            break;
        case 65:
            params[GLIDE_ENABLE_PARAM].setValue(value >= 64 ? 1.f : 0.f);
            break;
        case 90:
            params[SUB_OCTAVE_PARAM].setValue(value >= 64 ? 1.f : 0.f);
            break;
        case 102:
            params[VEL_SENS_PARAM].setValue(value >= 64 ? 1.f : 0.f);
            break;
        case 114:
            params[REV_FROZEN_PARAM].setValue(value >= 64 ? 1.f : 0.f);
            break;
        case 119:
            _droneMode = true;
            break;
        case 123: // All Notes Off / Panic — silence everything, leave NOT in drone mode
            _midiGate = false;
            _midiNote = -1;
            _midiKeyHeld = false;
            _midiSustain = false;
            for (int i = 0; i < 6; i++) {
                if (_engine.polyEnvs[i]) {
                    _engine.polyEnvs[i]->setGate(false);
                    _engine.polyEnvs[i]->reset();
                }
                _polySlots[i].midiNote = 255;
            }
            _cvPolySlot = 255;
            _droneMode = false;
            break;
        default:
            break;
        }
    }

    void process(const ProcessArgs &args) override {
        // --- M37i: drain MIDI input queue ---
        {
            rack::midi::Message msg;
            while (midiInput.tryPop(&msg, args.frame)) {
                uint8_t status = msg.getStatus();
                uint8_t note = msg.getNote(); // also CC number for CC messages
                uint8_t value = msg.getValue();

                if (status == 0x9 && value > 0) {
                    // Note On
                    _droneMode = false;
                    if (_voiceMode == VoiceMode::POLY) {
                        // POLY: search for a free slot starting at _polyRR so
                        // voices are assigned in rotation (same as CV/gate path).
                        // Steal round-robin if all busy.
                        uint8_t slot = 255;
                        for (uint8_t i = 0; i < 6; i++) {
                            uint8_t idx = (_polyRR + i) % 6;
                            if (_polySlots[idx].midiNote == 255) {
                                slot = idx;
                                break;
                            }
                        }
                        if (slot == 255)
                            slot = _polyRR % 6;
                        _polyRR = (_polyRR + 1) % 6;
                        _polySlots[slot].freq = 440.0f * exp2f(((int)note - 69) / 12.0f);
                        _polySlots[slot].velocity = params[VEL_SENS_PARAM].getValue() >= 0.5f ? (value / 127.0f) : 1.0f;
                        _polySlots[slot].midiNote = note;
                        if (_engine.polyEnvs[slot])
                            _engine.polyEnvs[slot]->setGate(true);
                    } else {
                        // Monophonic path
                        _midiNote = note;
                        _midiVoct = ((int)note - 69) / 12.0f;
                        _midiGate = true;
                        _midiKeyHeld = true;
                        _midiEverPlayed = true;
                        _midiVelocity = value / 127.0f;
                    }
                } else if (status == 0x8 || (status == 0x9 && value == 0)) {
                    // Note Off
                    if (_voiceMode == VoiceMode::POLY) {
                        for (uint8_t i = 0; i < 6; i++) {
                            if (_polySlots[i].midiNote == note) {
                                if (_engine.polyEnvs[i])
                                    _engine.polyEnvs[i]->setGate(false);
                                _polySlots[i].midiNote = 255;
                            }
                        }
                    } else if ((int)note == _midiNote) {
                        _midiKeyHeld = false;
                        if (!_midiSustain) {
                            _midiGate = false;
                            _midiNote = -1;
                        }
                    }
                } else if (status == 0xb) {
                    _applyCC(note, value);
                } else if (msg.bytes[0] == 0xF0) {
                    // AlloyFlux SysEx handler
                    if (msg.bytes.size() >= 6 &&
                        msg.bytes[1] == 0x7D && msg.bytes[2] == 0x41 &&
                        msg.bytes[3] == 0x46 && msg.bytes.back() == 0xF7) {
                        const uint8_t cmd = msg.bytes[4];
                        const uint8_t arg = msg.bytes.size() >= 7 ? msg.bytes[5] : 0;
                        if (cmd == 0x01) { // REQUEST_DUMP
                            _dumpPending.store(true);
                        } else if (cmd == 0x03) { // APPLY_PATCH — apply CC pairs from body
                            for (size_t i = 5; i + 1 < msg.bytes.size() - 1; i += 2)
                                _applyCC(msg.bytes[i], msg.bytes[i + 1]);
                            _dumpPending.store(true);
                        } else if (cmd == 0x04) { // PRESET_SAVE — snapshot current params to slot
                            if (arg >= 1 && arg <= 9)
                                _presets[arg - 1] = _snapshotPairs();
                        } else if (cmd == 0x05) { // PRESET_LOAD — restore slot
                            if (arg >= 1 && arg <= 9 && !_presets[arg - 1].empty()) {
                                for (auto &p : _presets[arg - 1])
                                    _applyCC(p.first, p.second);
                                _dumpPending.store(true);
                            }
                        } else if (cmd == 0x06) { // PRESET_RESET
                            if (arg == 0 || arg == 0x7f) {
                                for (auto *pq : paramQuantities)
                                    if (pq)
                                        pq->reset();
                                _voiceMode = VoiceMode::PAIR;
                                _droneMode = true;
                                _dumpPending.store(true);
                            }
                            if (arg >= 1 && arg <= 9)
                                _presets[arg - 1].clear();
                            else if (arg == 0x7f)
                                for (auto &s : _presets)
                                    s.clear();
                        }
                    }
                }
            }
        }
        if (_dumpPending.exchange(false))
            sendPatchDump();

        // ---------------------------------------------------------------
        // Button edge detection
        // ---------------------------------------------------------------
        bool modeDown = _io.readButton(ButtonId::MODE);
        bool shiftDown = _io.readButton(ButtonId::SHIFT);

        // MODE + SHIFT held → also exits drone mode (hardware combo parity)
        if (modeDown && shiftDown) {
            if (!_droneComboFired) {
                _droneComboFired = true;
                _modeConsumed = true;
                _droneMode = false;
            }
        } else {
            _droneComboFired = false;

            // MODE released → cycle voice mode
            if (_modeWasDown && !modeDown) {
                if (!_modeConsumed) {
                    static const VoiceMode kModes[] = {
                        VoiceMode::PAIR, VoiceMode::CLOUD, VoiceMode::CHORD,
                        VoiceMode::CASCADE, VoiceMode::STRING, VoiceMode::POLY};
                    constexpr int kN = (int)(sizeof(kModes) / sizeof(kModes[0]));
                    int idx = 0;
                    for (int i = 0; i < kN; i++) {
                        if (kModes[i] == _voiceMode) {
                            idx = i;
                            break;
                        }
                    }
                    _voiceMode = kModes[(idx + 1) % kN];
                    // Reset poly allocator state on mode change (clear stuck notes).
                    for (int i = 0; i < 6; i++) {
                        _polySlots[i].midiNote = 255;
                        if (_engine.polyEnvs[i])
                            _engine.polyEnvs[i]->setGate(false);
                    }
                    _polyRR = 0;
                    _cvPolySlot = 255;
                }
                _modeConsumed = false;
            }

            // SHIFT released → enter drone mode (not toggle — mirrors hardware)
            if (_shiftWasDown && !shiftDown)
                _droneMode = true;
        }

        _modeWasDown = modeDown;
        _shiftWasDown = shiftDown;

        // ---------------------------------------------------------------
        fillSynthParams(_io, _params);
        _params.voiceMode = _voiceMode;
        // VCV Rack V/Oct convention: 0 V = C4 (261.626 Hz).
        // Internally, 0 V on the V/Oct path = A4 (440 Hz) — 9 semitones = 0.75 V higher.
        // Correct only when a cable is patched; free-running / ROOT-knob tuning is unaffected.
        if (inputs[VOCT_INPUT].isConnected())
            _params.baseFreq *= exp2f(-0.75f);

        // Gate rising/falling edge detection (drone-exit + POLY CV allocation).
        bool gateNow = inputs[GATE_INPUT].isConnected() &&
                       inputs[GATE_INPUT].getVoltage() >= 1.f;
        bool gateRising = gateNow && !_prevGateHigh;
        bool gateFalling = !gateNow && _prevGateHigh;
        if (gateRising)
            _droneMode = false;
        _prevGateHigh = gateNow;

        // Drone mode: bypass envelope by clearing gate flags.
        // Only suppress when truly idle (no CV gate, no MIDI note active).
        if (_droneMode && !gateNow && _midiNote < 0) {
            _params.gatePatched = false;
            _params.gateHigh = false;
        }

        // M37i: MIDI pitch/gate merge (monophonic modes only).
        // In POLY mode, SynthEngine reads _polySlots[] directly for pitch and
        // triggers per-voice envelopes via polyEnvs[i]->setGate() in the handlers
        // above — gateHigh/gatePatched are not used.
        if (_voiceMode != VoiceMode::POLY) {
            // Pitch: persists through release so the tail plays at the correct pitch.
            if (!inputs[VOCT_INPUT].isConnected() && _midiEverPlayed)
                _params.baseFreq = 440.0f * exp2f(_midiVoct);
            if (_midiNote >= 0) {
                _params.gatePatched = true;
                _params.gateHigh = _params.gateHigh || _midiGate;
            }
            // Hardware parity: absent gate jack = gate-low, not free-running.
            if (!_droneMode && !_params.gatePatched)
                _params.gatePatched = true;
        }

        // M37j: scale quantizer + transpose (applied to baseFreq every sample)
        {
            _quantizeScale = static_cast<ScaleId>((uint8_t)((int)params[SCALE_PARAM].getValue()));
            _transpose = (int8_t)(int)roundf(params[TRANSPOSE_PARAM].getValue());
            float voct = log2f(_params.baseFreq / 440.0f);
            int rawNote = (int)roundf(voct * 12.0f) + 69;
            rawNote = rawNote < 0 ? 0 : rawNote > 127 ? 127
                                                      : rawNote;
            uint8_t qNote = quantizeNote((uint8_t)rawNote, _quantizeScale, _transpose);
            _params.baseFreq = 440.0f * exp2f((float)((int)qNote - 69) / 12.0f);
        }

        // POLY mode + CV gate.
        // Sentinels: 255=free, 128=CV gate held, 129=CV releasing (ringing out).
        // Rising edge  → allocate round-robin; prefer free (255) then releasing (129).
        // Falling edge → call setGate(false) but keep slot as 129 so tail rings out.
        // Control tick → scan 129 slots; free when envelope level drops to silence.
        if (_voiceMode == VoiceMode::POLY && inputs[GATE_INPUT].isConnected()) {
            if (gateRising) {
                // Prefer a truly free slot, then a releasing one, then steal RR.
                uint8_t slot = 255;
                uint8_t relSlot = 255;
                for (uint8_t i = 0; i < 6; i++) {
                    if (_polySlots[i].midiNote == 255) {
                        slot = i;
                        break;
                    }
                    if (_polySlots[i].midiNote == 129 && relSlot == 255)
                        relSlot = i;
                }
                if (slot == 255)
                    slot = (relSlot != 255) ? relSlot : _polyRR % 6;
                _polyRR = (_polyRR + 1) % 6;
                _polySlots[slot].freq = _params.baseFreq;
                _polySlots[slot].velocity = 1.0f;
                _polySlots[slot].midiNote = 128; // gate held
                if (_engine.polyEnvs[slot])
                    _engine.polyEnvs[slot]->setGate(true);
                _cvPolySlot = slot;
            } else if (gateFalling && _cvPolySlot < 6) {
                // Release envelope but keep slot occupied so the tail rings out.
                if (_engine.polyEnvs[_cvPolySlot])
                    _engine.polyEnvs[_cvPolySlot]->setGate(false);
                _polySlots[_cvPolySlot].midiNote = 129; // releasing
                _cvPolySlot = 255;
            }
        }

        // M37g — envelope params
        _params.envelopeType = params[ENV_TYPE_PARAM].getValue() >= 0.5f
                                   ? EnvelopeType::ADSR
                                   : EnvelopeType::AR;
        _params.adsrAttack = params[ADSR_ATTACK_PARAM].getValue();
        _params.adsrDecay = params[ADSR_DECAY_PARAM].getValue();
        _params.adsrSustain = params[ADSR_SUSTAIN_PARAM].getValue();
        _params.adsrRelease = params[ADSR_RELEASE_PARAM].getValue();
        _params.adsrLoop = params[ADSR_LOOP_PARAM].getValue() >= 0.5f;

        // M37h — effects params
        _params.chorusMode = static_cast<ChorusMode>(
            (int)params[CHORUS_MODE_PARAM].getValue());
        _params.filterMode = static_cast<FilterMode>(
            (int)params[FILTER_MODE_PARAM].getValue());
        _params.filterType = static_cast<FilterType>(
            (int)params[FILTER_TYPE_PARAM].getValue());
        _params.filterCutoff = params[FILTER_CUTOFF_PARAM].getValue();
        _params.filterRes = params[FILTER_RES_PARAM].getValue();
        _params.revMix = params[REV_MIX_PARAM].getValue();
        _params.revEnabled = _params.revMix > 0.f;
        _params.revSize = params[REV_SIZE_PARAM].getValue();
        _params.revDamping = params[REV_DAMPING_PARAM].getValue();
        _params.revModSpeed = params[REV_MOD_SPEED_PARAM].getValue();
        _params.revModDepth = params[REV_MOD_DEPTH_PARAM].getValue();
        _params.delayMix = params[DELAY_MIX_PARAM].getValue();
        _params.delayTime = params[DELAY_TIME_PARAM].getValue();
        _params.delayFeedback = params[DELAY_FB_PARAM].getValue();
        _params.fxOrder.filterPostChorus = params[FX_FILTER_POS_PARAM].getValue() >= 0.5f;
        _params.fxOrder.delayPostReverb = params[FX_DELAY_POS_PARAM].getValue() >= 0.5f;
        // M37m — portamento/glide, sub-octave, reverb freeze, velocity sensitivity
        _params.glideEnabled = params[GLIDE_ENABLE_PARAM].getValue() >= 0.5f;
        _params.glideTime = params[GLIDE_TIME_PARAM].getValue();
        _params.curveTime = params[CURVETIME_PARAM].getValue();
        _params.subOctave = params[SUB_OCTAVE_PARAM].getValue() >= 0.5f ? 2 : 1;
        _params.revFrozen = params[REV_FROZEN_PARAM].getValue() >= 0.5f;
        _params.midiVelocity = params[VEL_SENS_PARAM].getValue() >= 0.5f ? _midiVelocity : 1.0f;

        gGatePatched = _params.gatePatched;
        gGateHigh = _params.gateHigh;

        if (++_controlCounter >= _controlDiv) {
            _controlCounter = 0;
            SynthControlOutput out;
            _engine.control(_params, _polySlots, out);
            sendCCFeedback();
            // Free CV poly slots that have fully decayed (sentinel 129 = releasing).
            for (int i = 0; i < 6; i++) {
                if (_polySlots[i].midiNote == 129 &&
                    _engine.polyEnvs[i] &&
                    _engine.polyEnvs[i]->level() < 0.001f)
                    _polySlots[i].midiNote = 255;
            }
        }

        // M37h — inline reverb: process last frame's dry signal, pass wet to audio().
        // 1-frame latency (~0.02 ms at 44100 Hz) is acoustically transparent.
        int32_t revWetL = 0, revWetR = 0;
        if (_params.revEnabled) {
            constexpr float kNorm = 1.0f / 32512.0f;
            float wetL, wetR;
            _engine.reverb->process(
                (float)_prevDryL * kNorm,
                (float)_prevDryR * kNorm,
                &wetL, &wetR);
            revWetL = (int32_t)(wetL * 32512.0f);
            revWetR = (int32_t)(wetR * 32512.0f);
        }

        int32_t outL = 0, outR = 0, dryL = 0, dryR = 0;
        _engine.audio(revWetL, revWetR, _params.revMix, _params.revEnabled,
                      &outL, &outR, &dryL, &dryR);

        _prevDryL = dryL;
        _prevDryR = dryR;

        constexpr float kScale = 5.0f / 32512.0f;
        outputs[L_OUTPUT].setVoltage((float)outL * kScale);
        outputs[R_OUTPUT].setVoltage((float)outR * kScale);
    }

    // M37i: persist MIDI port config; M37l: persist preset slots 1–9
    json_t *dataToJson() override {
        json_t *rootJ = json_object();
        json_object_set_new(rootJ, "midiInput", midiInput.toJson());
        json_object_set_new(rootJ, "midiOutput", midiOutput.toJson());
        json_t *presetsJ = json_array();
        for (int s = 0; s < 9; s++) {
            json_t *slotJ = json_array();
            for (auto &p : _presets[s]) {
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

    void dataFromJson(json_t *rootJ) override {
        json_t *midiJ = json_object_get(rootJ, "midiInput");
        if (midiJ)
            midiInput.fromJson(midiJ);
        json_t *midiOutJ = json_object_get(rootJ, "midiOutput");
        if (midiOutJ)
            midiOutput.fromJson(midiOutJ);
        json_t *presetsJ = json_object_get(rootJ, "presets");
        if (presetsJ && json_is_array(presetsJ)) {
            size_t s;
            json_t *slotJ;
            json_array_foreach(presetsJ, s, slotJ) {
                if (s >= 9 || !json_is_array(slotJ))
                    continue;
                _presets[s].clear();
                size_t p;
                json_t *pairJ;
                json_array_foreach(slotJ, p, pairJ) {
                    if (!json_is_array(pairJ) || json_array_size(pairJ) < 2)
                        continue;
                    uint8_t cc = (uint8_t)json_integer_value(json_array_get(pairJ, 0));
                    uint8_t val = (uint8_t)json_integer_value(json_array_get(pairJ, 1));
                    _presets[s].push_back({cc, val});
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Panel widget — coordinates from res/AlloyFlux.svg via AlloyFluxPanel.cpp
// ---------------------------------------------------------------------------
struct AlloyFluxWidget : ModuleWidget {
    AlloyFluxWidget(AlloyFlux *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/AlloyFlux.svg")));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // --- Panel knobs (7) ---
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(13.868, 20.883)), module, AlloyFlux::ROOT_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(56.962, 20.923)), module, AlloyFlux::RELATION_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(35.5, 25.423)), module, AlloyFlux::COLOR_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(13.862, 39.971)), module, AlloyFlux::SHAPE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(57.076, 40.388)), module, AlloyFlux::MOTION_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(35.48, 43.653)), module, AlloyFlux::CURVE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(35.477, 62.018)), module, AlloyFlux::SPACE_PARAM));

        // --- Buttons ---
        addParam(createParamCentered<VCVButton>(mm2px(Vec(22.3, 74.07)), module, AlloyFlux::MODE_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(48.6, 74.07)), module, AlloyFlux::SHIFT_PARAM));

        // --- CV inputs — row 1 (V/OCT, GATE, [MIDI jack — software only], REL CV, SHAPE CV) ---
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.511, 93.27)), module, AlloyFlux::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.256, 93.27)), module, AlloyFlux::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(35.4, 93.27)), module, AlloyFlux::MIDI_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.645, 93.27)), module, AlloyFlux::REL_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(61.389, 93.27)), module, AlloyFlux::SHP_CV_INPUT));

        // --- CV inputs — row 2 (MTN CV, FM IN, SPACE CV) ---
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.511, 107.242)), module, AlloyFlux::MTN_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.256, 107.474)), module, AlloyFlux::FM_IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(35.4, 107.474)), module, AlloyFlux::SPC_CV_INPUT));

        // --- Outputs ---
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(48.645, 107.369)), module, AlloyFlux::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(61.389, 107.437)), module, AlloyFlux::R_OUTPUT));
    }

    // -----------------------------------------------------------------------
    // Context menu
    // -----------------------------------------------------------------------
    void appendContextMenu(rack::ui::Menu *menu) override {
        ModuleWidget::appendContextMenu(menu);

        AlloyFlux *m = dynamic_cast<AlloyFlux *>(module);
        if (!m)
            return;

        menu->addChild(new rack::ui::MenuSeparator);

        // --- Voice mode submenu ---
        menu->addChild(rack::createSubmenuItem(
            "Voice mode", voiceModeName(m->_voiceMode),
            [=](rack::ui::Menu *submenu) {
                static const VoiceMode kModes[] = {
                    VoiceMode::PAIR, VoiceMode::CLOUD, VoiceMode::CHORD,
                    VoiceMode::CASCADE, VoiceMode::STRING, VoiceMode::POLY};
                for (int i = 0; i < (int)(sizeof(kModes) / sizeof(kModes[0])); i++) {
                    VoiceMode mode = kModes[i];
                    submenu->addChild(rack::createCheckMenuItem(
                        voiceModeName(mode), "",
                        [=]() { return m->_voiceMode == mode; },
                        [=]() { m->_voiceMode = mode; }));
                }
            }));

        // --- Scale submenu (M37j) ---
        static const char *const kScaleDisplay[(uint8_t)ScaleId::COUNT] = {
            "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor",
            "Pentatonic Maj", "Pentatonic Min", "Blues", "Dorian", "Phrygian",
            "Lydian", "Mixolydian", "Locrian", "Whole Tone", "Diminished"};
        int curScale = (int)m->params[AlloyFlux::SCALE_PARAM].getValue();
        int curTranspose = (int)roundf(m->params[AlloyFlux::TRANSPOSE_PARAM].getValue());
        std::string scaleLabel = kScaleDisplay[curScale];
        if (curTranspose != 0)
            scaleLabel += (curTranspose > 0 ? " +" : " ") + std::to_string(curTranspose);
        menu->addChild(rack::createSubmenuItem(
            "Input Quantization", scaleLabel,
            [=](rack::ui::Menu *submenu) {
                for (int i = 0; i < (int)ScaleId::COUNT; i++) {
                    int idx = i;
                    submenu->addChild(rack::createCheckMenuItem(
                        kScaleDisplay[idx], "",
                        [=]() { return (int)m->params[AlloyFlux::SCALE_PARAM].getValue() == idx; },
                        [=]() { m->params[AlloyFlux::SCALE_PARAM].setValue((float)idx); }));
                }
                submenu->addChild(new rack::ui::MenuSeparator);
                auto *tr = new SubMenuSlider;
                tr->text = "Transpose";
                tr->quantity = m->getParamQuantity(AlloyFlux::TRANSPOSE_PARAM);
                submenu->addChild(tr);
            }));

        // --- Shift parameters (top-level sliders) ---
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Shifted Controls"));

        auto *fatSlider = new SubMenuSlider;
        fatSlider->text = "Fatness";
        fatSlider->quantity = m->getParamQuantity(AlloyFlux::FATNESS_PARAM);
        menu->addChild(fatSlider);

        auto *driftSlider = new SubMenuSlider;
        driftSlider->text = "Drift speed";
        driftSlider->quantity = m->getParamQuantity(AlloyFlux::DRIFTSPEED_PARAM);
        menu->addChild(driftSlider);

        auto *volSlider = new SubMenuSlider;
        volSlider->text = "Volume";
        volSlider->quantity = m->getParamQuantity(AlloyFlux::VOL_PARAM);
        menu->addChild(volSlider);

        auto *ctSlider = new SubMenuSlider;
        ctSlider->text = "Curve time";
        ctSlider->quantity = m->getParamQuantity(AlloyFlux::CURVETIME_PARAM);
        menu->addChild(ctSlider);
        menu->addChild(rack::createMenuLabel("Additional Controls"));
        // --- Envelope submenu ---
        bool isAdsr = m->params[AlloyFlux::ENV_TYPE_PARAM].getValue() >= 0.5f;
        const char *envTypeName = isAdsr ? "ADSR" : "AR";
        menu->addChild(rack::createSubmenuItem(
            "Envelope", envTypeName,
            [=](rack::ui::Menu *submenu) {
                submenu->addChild(rack::createCheckMenuItem(
                    "AR", "",
                    [=]() { return !isAdsr; },
                    [=]() { m->params[AlloyFlux::ENV_TYPE_PARAM].setValue(0.f); }));
                submenu->addChild(rack::createCheckMenuItem(
                    "ADSR", "",
                    [=]() { return isAdsr; },
                    [=]() { m->params[AlloyFlux::ENV_TYPE_PARAM].setValue(1.f); }));

                submenu->addChild(new rack::ui::MenuSeparator);

                auto *atk = new SubMenuSlider;
                atk->text = "Attack";
                atk->quantity = m->getParamQuantity(AlloyFlux::ADSR_ATTACK_PARAM);
                submenu->addChild(atk);

                auto *dec = new SubMenuSlider;
                dec->text = "Decay";
                dec->quantity = m->getParamQuantity(AlloyFlux::ADSR_DECAY_PARAM);
                submenu->addChild(dec);

                auto *sus = new SubMenuSlider;
                sus->text = "Sustain";
                sus->quantity = m->getParamQuantity(AlloyFlux::ADSR_SUSTAIN_PARAM);
                submenu->addChild(sus);

                auto *rel = new SubMenuSlider;
                rel->text = "Release";
                rel->quantity = m->getParamQuantity(AlloyFlux::ADSR_RELEASE_PARAM);
                submenu->addChild(rel);

                bool loopOn = m->params[AlloyFlux::ADSR_LOOP_PARAM].getValue() >= 0.5f;
                submenu->addChild(rack::createCheckMenuItem(
                    "Loop", "",
                    [=]() { return loopOn; },
                    [=]() { m->params[AlloyFlux::ADSR_LOOP_PARAM].setValue(loopOn ? 0.f : 1.f); }));
            }));

        // --- Chorus submenu ---
        struct {
            const char *label;
            float val;
        } modes[] = {
            {"Off", 0.f}, {"I", 1.f}, {"II", 2.f}, {"I + II", 3.f}};
        int cur = m->params[AlloyFlux::CHORUS_MODE_PARAM].getValue();
        const char *chorusName = modes[cur].label;
        menu->addChild(rack::createSubmenuItem(
            "Chorus", chorusName,
            [=](rack::ui::Menu *submenu) {
                for (auto &mo : modes) {
                    float v = mo.val;
                    submenu->addChild(rack::createCheckMenuItem(
                        mo.label, "",
                        [=]() { return m->params[AlloyFlux::CHORUS_MODE_PARAM].getValue() == v; },
                        [=]() { m->params[AlloyFlux::CHORUS_MODE_PARAM].setValue(v); }));
                }
                (void)cur;
            }));

        // --- Filter submenu ---
        int curFilterType = (int)m->params[AlloyFlux::FILTER_TYPE_PARAM].getValue();
        const char *filterTypeName = curFilterType < 1 ? "SVF" : "Ladder";
        struct {
            const char *label;
            float val;
        } fmodes[] = {
            {"Off", 0.f}, {"LP", 1.f}, {"HP", 2.f}, {"BP", 3.f}, {"Notch", 4.f}, {"LP4", 5.f}};
        int curFilterMode = (int)m->params[AlloyFlux::FILTER_MODE_PARAM].getValue();
        std::string filterLabel = std::string(filterTypeName) + " (" + fmodes[curFilterMode].label + ")";

        menu->addChild(rack::createSubmenuItem(
            "Filter", filterLabel,
            [=](rack::ui::Menu *submenu) {
                // Position
                submenu->addChild(rack::createCheckMenuItem(
                    "Pre Chorus", "",
                    [=]() { return m->params[AlloyFlux::FX_FILTER_POS_PARAM].getValue() < 0.5f; },
                    [=]() { m->params[AlloyFlux::FX_FILTER_POS_PARAM].setValue(0.f); }));
                submenu->addChild(rack::createCheckMenuItem(
                    "Post Chorus", "",
                    [=]() { return m->params[AlloyFlux::FX_FILTER_POS_PARAM].getValue() >= 0.5f; },
                    [=]() { m->params[AlloyFlux::FX_FILTER_POS_PARAM].setValue(1.f); }));

                submenu->addChild(new rack::ui::MenuSeparator);

                // Type
                submenu->addChild(rack::createCheckMenuItem(
                    "SVF", "",
                    [=]() { return m->params[AlloyFlux::FILTER_TYPE_PARAM].getValue() < 0.5f; },
                    [=]() { m->params[AlloyFlux::FILTER_TYPE_PARAM].setValue(0.f); }));
                submenu->addChild(rack::createCheckMenuItem(
                    "Ladder", "",
                    [=]() { return m->params[AlloyFlux::FILTER_TYPE_PARAM].getValue() >= 0.5f; },
                    [=]() { m->params[AlloyFlux::FILTER_TYPE_PARAM].setValue(1.f); }));

                submenu->addChild(new rack::ui::MenuSeparator);

                // Mode
                for (auto &fm : fmodes) {
                    float v = fm.val;
                    submenu->addChild(rack::createCheckMenuItem(
                        fm.label, "",
                        [=]() { return m->params[AlloyFlux::FILTER_MODE_PARAM].getValue() == v; },
                        [=]() { m->params[AlloyFlux::FILTER_MODE_PARAM].setValue(v); }));
                }

                submenu->addChild(new rack::ui::MenuSeparator);

                auto *cut = new SubMenuSlider;
                cut->text = "Cutoff";
                cut->quantity = m->getParamQuantity(AlloyFlux::FILTER_CUTOFF_PARAM);
                submenu->addChild(cut);

                auto *res = new SubMenuSlider;
                res->text = "Resonance";
                res->quantity = m->getParamQuantity(AlloyFlux::FILTER_RES_PARAM);
                submenu->addChild(res);
            }));

        // --- Reverb submenu ---
        menu->addChild(rack::createSubmenuItem(
            "Reverb", "",
            [=](rack::ui::Menu *submenu) {
                auto *mix = new SubMenuSlider;
                mix->text = "Mix";
                mix->quantity = m->getParamQuantity(AlloyFlux::REV_MIX_PARAM);
                submenu->addChild(mix);

                auto *sz = new SubMenuSlider;
                sz->text = "Size";
                sz->quantity = m->getParamQuantity(AlloyFlux::REV_SIZE_PARAM);
                submenu->addChild(sz);

                auto *damp = new SubMenuSlider;
                damp->text = "Damping";
                damp->quantity = m->getParamQuantity(AlloyFlux::REV_DAMPING_PARAM);
                submenu->addChild(damp);

                auto *modSpeed = new SubMenuSlider;
                modSpeed->text = "Mod Speed";
                modSpeed->quantity = m->getParamQuantity(AlloyFlux::REV_MOD_SPEED_PARAM);
                submenu->addChild(modSpeed);

                auto *modDepth = new SubMenuSlider;
                modDepth->text = "Mod Depth";
                modDepth->quantity = m->getParamQuantity(AlloyFlux::REV_MOD_DEPTH_PARAM);
                submenu->addChild(modDepth);

                submenu->addChild(new rack::ui::MenuSeparator);
                bool frozen = m->params[AlloyFlux::REV_FROZEN_PARAM].getValue() >= 0.5f;
                submenu->addChild(rack::createCheckMenuItem(
                    "Freeze", "",
                    [=]() { return frozen; },
                    [=]() { m->params[AlloyFlux::REV_FROZEN_PARAM].setValue(frozen ? 0.f : 1.f); }));
            }));

        // --- Delay submenu ---
        menu->addChild(rack::createSubmenuItem(
            "Delay", "",
            [=](rack::ui::Menu *submenu) {
                // Position
                submenu->addChild(rack::createCheckMenuItem(
                    "Pre Reverb", "",
                    [=]() { return m->params[AlloyFlux::FX_DELAY_POS_PARAM].getValue() < 0.5f; },
                    [=]() { m->params[AlloyFlux::FX_DELAY_POS_PARAM].setValue(0.f); }));
                submenu->addChild(rack::createCheckMenuItem(
                    "Post Reverb", "",
                    [=]() { return m->params[AlloyFlux::FX_DELAY_POS_PARAM].getValue() >= 0.5f; },
                    [=]() { m->params[AlloyFlux::FX_DELAY_POS_PARAM].setValue(1.f); }));

                submenu->addChild(new rack::ui::MenuSeparator);

                auto *mix = new SubMenuSlider;
                mix->text = "Mix";
                mix->quantity = m->getParamQuantity(AlloyFlux::DELAY_MIX_PARAM);
                submenu->addChild(mix);

                auto *time = new SubMenuSlider;
                time->text = "Time";
                time->quantity = m->getParamQuantity(AlloyFlux::DELAY_TIME_PARAM);
                submenu->addChild(time);

                auto *fb = new SubMenuSlider;
                fb->text = "Feedback";
                fb->quantity = m->getParamQuantity(AlloyFlux::DELAY_FB_PARAM);
                submenu->addChild(fb);
            }));

        // --- Sub octave submenu ---
        menu->addChild(rack::createSubmenuItem(
            "Sub octave",
            m->params[AlloyFlux::SUB_OCTAVE_PARAM].getValue() >= 0.5f ? "2 oct" : "1 oct",
            [=](rack::ui::Menu *submenu) {
                submenu->addChild(rack::createCheckMenuItem(
                    "1 oct below", "",
                    [=]() { return m->params[AlloyFlux::SUB_OCTAVE_PARAM].getValue() < 0.5f; },
                    [=]() { m->params[AlloyFlux::SUB_OCTAVE_PARAM].setValue(0.f); }));
                submenu->addChild(rack::createCheckMenuItem(
                    "2 oct below", "",
                    [=]() { return m->params[AlloyFlux::SUB_OCTAVE_PARAM].getValue() >= 0.5f; },
                    [=]() { m->params[AlloyFlux::SUB_OCTAVE_PARAM].setValue(1.f); }));
            }));

        // --- Portamento submenu ---
        menu->addChild(rack::createSubmenuItem(
            "Portamento",
            m->params[AlloyFlux::GLIDE_ENABLE_PARAM].getValue() >= 0.5f ? "On" : "Off",
            [=](rack::ui::Menu *submenu) {
                bool glideOn = m->params[AlloyFlux::GLIDE_ENABLE_PARAM].getValue() >= 0.5f;
                submenu->addChild(rack::createCheckMenuItem(
                    "Enable", "",
                    [=]() { return glideOn; },
                    [=]() { m->params[AlloyFlux::GLIDE_ENABLE_PARAM].setValue(glideOn ? 0.f : 1.f); }));
                submenu->addChild(new rack::ui::MenuSeparator);
                auto *sl = new SubMenuSlider;
                sl->text = "Glide time";
                sl->quantity = m->getParamQuantity(AlloyFlux::GLIDE_TIME_PARAM);
                submenu->addChild(sl);
            }));

        // --- Velocity sensitivity ---
        bool velSens = m->params[AlloyFlux::VEL_SENS_PARAM].getValue() >= 0.5f;
        menu->addChild(rack::createCheckMenuItem(
            "Velocity sensitivity", "",
            [=]() { return velSens; },
            [=]() { m->params[AlloyFlux::VEL_SENS_PARAM].setValue(velSens ? 0.f : 1.f); }));

        // --- Presets (M37l) ---
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createSubmenuItem(
            "Presets", "",
            [=](rack::ui::Menu *submenu) {
                submenu->addChild(rack::createSubmenuItem(
                    "Save", "",
                    [=](rack::ui::Menu *saveMenu) {
                        for (int s = 1; s <= 9; s++) {
                            bool hasSave = !m->_presets[s - 1].empty();
                            saveMenu->addChild(rack::createMenuItem(
                                "Preset " + std::to_string(s),
                                hasSave ? "(overwrite)" : "",
                                [=]() { m->_presets[s - 1] = m->_snapshotPairs(); }));
                        }
                    }));

                submenu->addChild(rack::createSubmenuItem(
                    "Load", "",
                    [=](rack::ui::Menu *loadMenu) {
                        for (int s = 1; s <= 9; s++) {
                            bool hasSave = !m->_presets[s - 1].empty();
                            auto *item = rack::createMenuItem(
                                "Preset " + std::to_string(s),
                                hasSave ? "" : "(empty)",
                                [=]() {
                                    for (auto &p : m->_presets[s - 1])
                                        m->_applyCC(p.first, p.second);
                                    m->_dumpPending.store(true);
                                });
                            item->disabled = !hasSave;
                            loadMenu->addChild(item);
                        }
                    }));

                submenu->addChild(rack::createSubmenuItem(
                    "Clear", "",
                    [=](rack::ui::Menu *clearMenu) {
                        for (int s = 1; s <= 9; s++) {
                            bool hasSave = !m->_presets[s - 1].empty();
                            auto *item = rack::createMenuItem(
                                "Preset " + std::to_string(s),
                                hasSave ? "" : "(empty)",
                                [=]() { m->_presets[s - 1].clear(); });
                            item->disabled = !hasSave;
                            clearMenu->addChild(item);
                        }
                    }));
            }));

        // --- Sync + MIDI settings (M37i) ---
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuItem(
            "Sync to web \u2192", "Send PATCH_DUMP to web configurator",
            [=]() { m->_dumpPending.store(true); }));
        menu->addChild(rack::createSubmenuItem(
            "MIDI Settings", "",
            [=](rack::ui::Menu *submenu) {
                submenu->addChild(rack::createMenuLabel("MIDI Input"));
                rack::app::appendMidiMenu(submenu, &m->midiInput);
                submenu->addChild(new rack::ui::MenuSeparator);
                submenu->addChild(rack::createMenuLabel("MIDI Output (web sync)"));
                rack::app::appendMidiMenu(submenu, &m->midiOutput);
            }));
    }
};

Model *modelAlloyFlux = createModel<AlloyFlux, AlloyFluxWidget>("AlloyFlux");
