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
#include "SubMenuSlider.hpp" // shift-secondaries as context-menu sliders
#include "PanelLayout.h"     // shared panel geometry (all modules, one PCB)
#include "PanelLed.hpp"      // aperture-shaped lights, matching the panel art
#include "VCVRackIO.h"       // platform: positional slots -> Rack indices
#include "io/CoilLeds.h"   // the LED language, shared with the firmware
#include "io/IOBridge.h"
#include "io/PanelMap.h"
#include "param_manifest.generated.h" // knob defaults, see defaultPos()
#include "params.h"
#include "plugin.hpp"

#include <cstring>

// ---------------------------------------------------------------------------
// Globals declared extern in params.h. main.cpp is not compiled into the
// plugin, so the definitions live here.
//
// One set per process, not per Module — a second AlloyCoil in the same rack
// shares them. That is a real limitation and it is the same one AlloyFlux has;
// the engine instance itself is per-Module, so only the goal values collide,
// and they are overwritten from this module's own knobs every process() call.
// ---------------------------------------------------------------------------
float gStringPitch   = 40.0f;
float gFeedbackGain  = -30.0f;
float gFeedbackDelay = 0.001f;
float gFeedbackLPF   = 18000.0f;
float gFeedbackHPF   = 250.0f;
float gEchoSend      = 0.0f;
float gEchoTime      = 0.5f;
float gEchoFeedback  = 0.0f;
float gReverbMix     = 0.0f;
float gReverbDecay   = 0.2f;
float gOutputLevel   = 0.5f;
float gExciterLevel  = 1.0f;

volatile float gExciterIn = 0.0f;

// ---------------------------------------------------------------------------
// Knob boot position for a parameter, from the manifest.
//
// configParam() takes a *position*, not a value, so every default here used to
// be a hand-computed inverse of that knob's curve — and when M63i changed
// fbbody and echotime from log to square-law, the two numbers computed against
// the old curves stayed put. The module booted with its echo at 1.14 s instead
// of the 0.5 s params.json specifies, silently, in the build that is supposed
// to be the A/B reference for the hardware.
//
// ParamDescriptor::toPos() is exactly that inverse, derived from the same row
// the CC path and the web configurator read, so the class of bug is gone rather
// than the instance of it.
// ---------------------------------------------------------------------------
static float defaultPos(const char *name)
{
    for(uint8_t i = 0; i < kParamManifestCount; i++)
    {
        if(std::strcmp(kParamManifest[i].name, name) == 0)
            return kParamManifest[i].toPos(kParamManifest[i].defVal);
    }
    return 0.0f; // unreachable for a name that is in params.json
}

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

    AlloyCoil() : _io(this)
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Every knob is 0–1 and the range mapping lives in IOBridge, exactly as
        // it does for the hardware pots. Keeping the curve in one place is what
        // stops a knob and a MIDI CC landing on different values.
        //
        // Defaults come from the manifest via defaultPos() — they are knob
        // positions, and hand-computing the inverse of each curve is what let
        // echo time drift a full octave of delay time. The two display scalings
        // below stay literal because Rack's displayMultiplier/Offset are linear
        // only, and these are the two linear parameters.
        configParam(PITCH_PARAM,
                    0.f,
                    1.f,
                    defaultPos("pitch"),
                    "String pitch",
                    " note",
                    0.f,
                    56.f,
                    16.f);
        configParam(FBGAIN_PARAM,
                    0.f,
                    1.f,
                    defaultPos("fbgain"),
                    "Feedback gain",
                    " dB",
                    0.f,
                    42.f,
                    -30.f);
        configParam(FBBODY_PARAM, 0.f, 1.f, defaultPos("fbbody"), "Body");
        configParam(FBLPF_PARAM, 0.f, 1.f, defaultPos("fblpf"), "Feedback LPF");
        configParam(FBHPF_PARAM, 0.f, 1.f, defaultPos("fbhpf"), "Feedback HPF");
        configParam(
            ECHOSEND_PARAM, 0.f, 1.f, defaultPos("echosend"), "Echo send");
        configParam(
            ECHOTIME_PARAM, 0.f, 1.f, defaultPos("echotime"), "Echo time");
        configParam(ECHOFB_PARAM, 0.f, 1.f, defaultPos("echofb"), "Echo feedback");
        configParam(REVMIX_PARAM, 0.f, 1.f, defaultPos("revmix"), "Reverb mix");
        configParam(
            REVDECAY_PARAM, 0.f, 1.f, defaultPos("revdecay"), "Reverb decay");
        configParam(VOL_PARAM, 0.f, 1.f, defaultPos("vol"), "Volume");
        // 0–2 range: the jack's full swing maps to the engine's full scale at
        // the default, i.e. what it did before this control existed.
        configParam(
            EXCITE_PARAM, 0.f, 1.f, defaultPos("excite"), "Exciter level");

        // Panel buttons — momentary, matching the hardware switches.
        configButton(WARP_PARAM,
                     "Warp — hold to halve the echo time; the tail pitches up "
                     "on press and back down on release");
        configButton(SHIFT_PARAM,
                     "Shift — selects the secondary parameters on hardware; "
                     "in Rack they are in the context menu");

        configInput(VOCT_INPUT, "V/Oct");
        configInput(GATE_INPUT, "Gate (unused — reserved for VCA/envelope)");
        configInput(MIDI_INPUT, "MIDI (TRS — Rack uses software MIDI instead)");
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
    }

    void process(const ProcessArgs &args) override
    {
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
    }
};

Model *modelAlloyCoil = createModel<AlloyCoil, AlloyCoilWidget>("AlloyCoil");
