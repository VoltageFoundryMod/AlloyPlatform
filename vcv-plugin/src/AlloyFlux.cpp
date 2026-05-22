#include "SynthEngine.h"
#include "plugin.hpp"

// ---------------------------------------------------------------------------
// Hardware globals declared extern in params.h.
// In the VCV build, main.cpp is not compiled so we provide defaults here.
// ---------------------------------------------------------------------------
volatile bool gGatePatched = false; // drone mode — envelope bypassed
volatile bool gGateHigh = false;

// Performance counters referenced by params.h; unused in VCV.
volatile bool gPerformancePrintEnabled = false;
volatile uint32_t gAudioElapsedUs = 0;
volatile uint32_t gAudioOverruns = 0;

// ---------------------------------------------------------------------------
// AlloyFlux VCV Rack module
// ---------------------------------------------------------------------------
struct AlloyFlux : Module {
    enum ParamId {
        ROOT_PARAM, // V/Oct knob, 0 V = A4 (440 Hz)
        PARAMS_LEN
    };
    enum InputId {
        VOCT_INPUT,
        GATE_INPUT,
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

    SynthEngine _engine;
    PolySlot _polySlots[4] = {};
    SynthParams _params;
    int _controlCounter = 0;
    int _controlDiv = 344; // ~128 Hz at 44100

    AlloyFlux() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(ROOT_PARAM, -4.0f, 4.0f, 0.0f, "Root pitch", " V");
        configInput(VOCT_INPUT, "V/Oct pitch");
        configInput(GATE_INPUT, "Gate");
        configOutput(L_OUTPUT, "Left");
        configOutput(R_OUTPUT, "Right");
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
        // Gate input → drone or envelope
        bool gateConnected = inputs[GATE_INPUT].isConnected();
        gGatePatched = gateConnected;
        gGateHigh = gateConnected && (inputs[GATE_INPUT].getVoltage() >= 1.0f);

        // V/Oct pitch from knob + CV input
        float voct = params[ROOT_PARAM].getValue();
        if (inputs[VOCT_INPUT].isConnected())
            voct += inputs[VOCT_INPUT].getVoltage();
        _params.baseFreq = 440.0f * rack::dsp::exp2_taylor5(voct);

        // Control-rate tick (~128 Hz)
        if (++_controlCounter >= _controlDiv) {
            _controlCounter = 0;
            _params.gateHigh = gGateHigh;
            _params.gatePatched = gateConnected;
            SynthControlOutput out;
            _engine.control(_params, _polySlots, out);
        }

        // Audio sample
        int32_t outL = 0, outR = 0, dryL = 0, dryR = 0;
        _engine.audio(/*revWetL*/ 0, /*revWetR*/ 0, /*revMix*/ 0.0f, /*revEnabled*/ false,
                      &outL, &outR, &dryL, &dryR);

        // Scale ±32512 int32 → ±5 V
        constexpr float kScale = 5.0f / 32512.0f;
        outputs[L_OUTPUT].setVoltage((float)outL * kScale);
        outputs[R_OUTPUT].setVoltage((float)outR * kScale);
    }
};

// ---------------------------------------------------------------------------
// Panel widget (minimal — no SVG required to load in Rack)
// ---------------------------------------------------------------------------
struct AlloyFluxWidget : ModuleWidget {
    AlloyFluxWidget(AlloyFlux *module) {
        setModule(module);

        // 12 HP panel (no SVG for M37c — blank panel)
        box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT);

        addParam(createParamCentered<RoundBigBlackKnob>(
            mm2px(Vec(30.48f, 30.0f)), module, AlloyFlux::ROOT_PARAM));

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(15.0f, 55.0f)), module, AlloyFlux::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(30.48f, 55.0f)), module, AlloyFlux::GATE_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(20.0f, 110.0f)), module, AlloyFlux::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(40.0f, 110.0f)), module, AlloyFlux::R_OUTPUT));
    }
};

Model *modelAlloyFlux = createModel<AlloyFlux, AlloyFluxWidget>("AlloyFlux");
