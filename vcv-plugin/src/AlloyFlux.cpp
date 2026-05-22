#include "SubMenuSlider.hpp"
#include "SynthEngine.h"
#include "VCVRackIO.h" // VCV-specific IHardwareIO implementation (M37d)
#include "VoiceMode.h"
#include "dsp/ChorusEngine.h" // ChorusMode enum
#include "dsp/CurveEngine.h"  // EnvelopeType enum
#include "dsp/FilterEngine.h" // FilterMode, FilterType enums
#include "io/IOBridge.h"      // fillSynthParams() shared bridge
#include "plugin.hpp"

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
        // Panel buttons
        MODE_PARAM,  // momentary — cycles voice mode on release
        SHIFT_PARAM, // momentary — toggles drone mode (VCV only)
        // ---- M37g: Envelope (hidden, saved in patch) ----
        ENV_TYPE_PARAM,     // 0=AR, 1=ADSR
        ADSR_ATTACK_PARAM,  // [0.001, 2.0] s, default 0.05
        ADSR_DECAY_PARAM,   // [0.001, 2.0] s, default 0.10
        ADSR_SUSTAIN_PARAM, // [0.0, 1.0], default 0.8
        ADSR_RELEASE_PARAM, // [0.001, 4.0] s, default 0.30
        ADSR_LOOP_PARAM,    // 0=off, 1=loop
        // ---- M37h: Effects (hidden, saved in patch) ----
        CHORUS_MODE_PARAM,   // 0=OFF,1=I,2=II,3=I_II
        FILTER_MODE_PARAM,   // 0=OFF,1=LP,2=HP,3=BP,4=NOTCH,5=LP4
        FILTER_TYPE_PARAM,   // 0=SVF,1=LADDER
        FILTER_CUTOFF_PARAM, // [20, 16000] Hz, default 8000
        FILTER_RES_PARAM,    // [0, 1], default 0
        REV_ENABLED_PARAM,   // 0=off, 1=on
        REV_MIX_PARAM,       // [0, 1], default 0.35
        REV_SIZE_PARAM,      // [0, 1], default 0.5
        REV_DAMPING_PARAM,   // [0, 1], default 0.5
        DELAY_ENABLED_PARAM, // 0=off, 1=on
        DELAY_MIX_PARAM,     // [0, 1], default 0
        DELAY_TIME_PARAM,    // [1, 1000] ms, default 100
        DELAY_FB_PARAM,      // [0, 0.99], default 0.5
        PARAMS_LEN
    };

    // CV input jacks (7)
    enum InputId {
        VOCT_INPUT,
        GATE_INPUT,
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
    PolySlot _polySlots[4] = {};
    SynthParams _params;
    int _controlCounter = 0;
    int _controlDiv = 344; // ~128 Hz at 44100

    // Button state — edge detection at full sample rate
    VoiceMode _voiceMode = VoiceMode::PAIR;
    bool _droneMode = false; // true = free-running (no envelope gating)
    bool _modeWasDown = false;
    bool _shiftWasDown = false;
    bool _droneComboFired = false;
    bool _modeConsumed = false;

    // M37h: 1-frame dry buffer for inline reverb (avoids Core 1 split)
    int32_t _prevDryL = 0;
    int32_t _prevDryR = 0;

    AlloyFlux() : _io(this) {
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

        // CV inputs
        configInput(VOCT_INPUT, "V/Oct");
        configInput(GATE_INPUT, "Gate");
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
        configParam(ADSR_ATTACK_PARAM, 0.001f, 2.0f, 0.05f, "Attack", " s");
        configParam(ADSR_DECAY_PARAM, 0.001f, 2.0f, 0.10f, "Decay", " s");
        configParam(ADSR_SUSTAIN_PARAM, 0.0f, 1.0f, 0.8f, "Sustain");
        configParam(ADSR_RELEASE_PARAM, 0.001f, 4.0f, 0.30f, "Release", " s");
        configSwitch(ADSR_LOOP_PARAM, 0.f, 1.f, 0.f, "Envelope loop", {"Off", "On"});

        // M37h — Effects (hidden)
        configSwitch(CHORUS_MODE_PARAM, 0.f, 3.f, 3.f, "Chorus mode",
                     {"Off", "Chorus I", "Chorus II", "Chorus I+II"});
        configSwitch(FILTER_MODE_PARAM, 0.f, 5.f, 0.f, "Filter mode",
                     {"Off", "LP", "HP", "BP", "Notch", "LP4"});
        configSwitch(FILTER_TYPE_PARAM, 0.f, 1.f, 0.f, "Filter type", {"SVF", "Ladder"});
        configParam(FILTER_CUTOFF_PARAM, 20.f, 16000.f, 8000.f, "Filter cutoff", " Hz");
        configParam(FILTER_RES_PARAM, 0.f, 1.f, 0.f, "Filter resonance");
        configSwitch(REV_ENABLED_PARAM, 0.f, 1.f, 0.f, "Reverb", {"Off", "On"});
        configParam(REV_MIX_PARAM, 0.f, 1.f, 0.35f, "Reverb mix");
        configParam(REV_SIZE_PARAM, 0.f, 1.f, 0.5f, "Reverb size");
        configParam(REV_DAMPING_PARAM, 0.f, 1.f, 0.5f, "Reverb damping");
        configSwitch(DELAY_ENABLED_PARAM, 0.f, 1.f, 0.f, "Delay", {"Off", "On"});
        configParam(DELAY_MIX_PARAM, 0.f, 1.f, 0.f, "Delay mix");
        configParam(DELAY_TIME_PARAM, 1.f, 1000.f, 100.f, "Delay time", " ms");
        configParam(DELAY_FB_PARAM, 0.f, 0.99f, 0.5f, "Delay feedback");
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

    void process(const ProcessArgs &args) override {
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
                }
                _modeConsumed = false;
            }

            // SHIFT released → toggle drone mode
            if (_shiftWasDown && !shiftDown)
                _droneMode = !_droneMode;
        }

        _modeWasDown = modeDown;
        _shiftWasDown = shiftDown;

        // ---------------------------------------------------------------
        fillSynthParams(_io, _params);
        _params.voiceMode = _voiceMode;

        // Drone mode: bypass envelope by clearing gate flags.
        if (_droneMode) {
            _params.gatePatched = false;
            _params.gateHigh = false;
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
        _params.revEnabled = params[REV_ENABLED_PARAM].getValue() >= 0.5f;
        _params.revMix = params[REV_MIX_PARAM].getValue();
        _params.revSize = params[REV_SIZE_PARAM].getValue();
        _params.revDamping = params[REV_DAMPING_PARAM].getValue();
        _params.delayMix = params[DELAY_ENABLED_PARAM].getValue() >= 0.5f
                               ? params[DELAY_MIX_PARAM].getValue()
                               : 0.f;
        _params.delayTime = params[DELAY_TIME_PARAM].getValue();
        _params.delayFeedback = params[DELAY_FB_PARAM].getValue();

        gGatePatched = _params.gatePatched;
        gGateHigh = _params.gateHigh;

        if (++_controlCounter >= _controlDiv) {
            _controlCounter = 0;
            SynthControlOutput out;
            _engine.control(_params, _polySlots, out);
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
};

// ---------------------------------------------------------------------------
// Panel widget — 14 HP placeholder layout (no SVG yet)
// Column x (mm): c1=12, c2=24, c3=36, c4=48  (3.5 HP steps)
// Row y (mm):    r1=20, r2=42, r3=64, r4=85, rCV=100, rOut=114
// ---------------------------------------------------------------------------
struct AlloyFluxWidget : ModuleWidget {
    AlloyFluxWidget(AlloyFlux *module) {
        setModule(module);
        box.size = Vec(RACK_GRID_WIDTH * 14, RACK_GRID_HEIGHT);

        const float c1 = 12.0f, c2 = 25.0f, c3 = 38.0f, c4 = 51.0f;
        const float r1 = 20.0f, r2 = 42.0f, r3 = 64.0f;
        const float rCV = 90.0f, rOut = 112.0f;

        // --- Panel knobs (7) ---
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c1, r1)), module, AlloyFlux::ROOT_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c2, r1)), module, AlloyFlux::RELATION_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c3, r1)), module, AlloyFlux::COLOR_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c1, r2)), module, AlloyFlux::SHAPE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c2, r2)), module, AlloyFlux::MOTION_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c1, r3)), module, AlloyFlux::CURVE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c2, r3)), module, AlloyFlux::SPACE_PARAM));

        // --- CV inputs (7) — spread across rCV row ---
        const float jx0 = 5.0f, jStep = 8.9f;
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx0 + 0 * jStep, rCV)), module, AlloyFlux::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx0 + 1 * jStep, rCV)), module, AlloyFlux::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx0 + 2 * jStep, rCV)), module, AlloyFlux::REL_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx0 + 3 * jStep, rCV)), module, AlloyFlux::SHP_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx0 + 4 * jStep, rCV)), module, AlloyFlux::MTN_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx0 + 5 * jStep, rCV)), module, AlloyFlux::SPC_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx0 + 6 * jStep, rCV)), module, AlloyFlux::FM_IN_INPUT));

        // --- Buttons ---
        // MODE (left of output row) and SHIFT/TRIG (right)
        addParam(createParamCentered<VCVButton>(mm2px(Vec(c1, rOut)), module, AlloyFlux::MODE_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(c4, rOut)), module, AlloyFlux::SHIFT_PARAM));

        // --- Outputs ---
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c2, rOut)), module, AlloyFlux::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c3, rOut)), module, AlloyFlux::R_OUTPUT));
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
        int curRev = m->params[AlloyFlux::REV_ENABLED_PARAM].getValue() >= 0.5f ? 1 : 0;
        const char *revName = curRev ? "On" : "Off";
        menu->addChild(rack::createSubmenuItem(
            "Reverb", revName,
            [=](rack::ui::Menu *submenu) {
                bool en = m->params[AlloyFlux::REV_ENABLED_PARAM].getValue() >= 0.5f;
                submenu->addChild(rack::createCheckMenuItem(
                    "Enable", "",
                    [=]() { return en; },
                    [=]() { m->params[AlloyFlux::REV_ENABLED_PARAM].setValue(en ? 0.f : 1.f); }));

                submenu->addChild(new rack::ui::MenuSeparator);

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
            }));

        // --- Delay submenu ---
        int curDelay = m->params[AlloyFlux::DELAY_ENABLED_PARAM].getValue() >= 0.5f ? 1 : 0;
        const char *delayName = curDelay ? "On" : "Off";
        menu->addChild(rack::createSubmenuItem(
            "Delay", delayName,
            [=](rack::ui::Menu *submenu) {
                bool en = m->params[AlloyFlux::DELAY_ENABLED_PARAM].getValue() >= 0.5f;
                submenu->addChild(rack::createCheckMenuItem(
                    "Enable", "",
                    [=]() { return en; },
                    [=]() { m->params[AlloyFlux::DELAY_ENABLED_PARAM].setValue(en ? 0.f : 1.f); }));

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
    }
};

Model *modelAlloyFlux = createModel<AlloyFlux, AlloyFluxWidget>("AlloyFlux");
