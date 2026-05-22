#include "SubMenuSlider.hpp"
#include "SynthEngine.h"
#include "VCVRackIO.h" // VCV-specific IHardwareIO implementation (M37d)
#include "VoiceMode.h"
#include "io/IOBridge.h" // fillSynthParams() shared bridge
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

        gGatePatched = _params.gatePatched;
        gGateHigh = _params.gateHigh;

        if (++_controlCounter >= _controlDiv) {
            _controlCounter = 0;
            SynthControlOutput out;
            _engine.control(_params, _polySlots, out);
        }

        int32_t outL = 0, outR = 0, dryL = 0, dryR = 0;
        _engine.audio(/*revWetL*/ 0, /*revWetR*/ 0, /*revMix*/ 0.0f, /*revEnabled*/ false,
                      &outL, &outR, &dryL, &dryR);

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
    // Context menu — SHIFT-secondary params as drag sliders
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
        menu->addChild(rack::createMenuLabel("Shift parameters"));

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
    }
};

Model *modelAlloyFlux = createModel<AlloyFlux, AlloyFluxWidget>("AlloyFlux");
