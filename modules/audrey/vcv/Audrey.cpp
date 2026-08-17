// Audrey II — VCV Rack module.
//
// The same engine the firmware runs, driven through the same IOBridge, so the
// plugin and the hardware are one implementation with two front ends. What
// differs is bandwidth: Rack gives the EXCITER input a sample per frame, where
// the firmware currently reads that jack at the control tick.
//
// Engine by Synthux Academy (Nick Donaldson / Roey Tsemah) — see
// modules/audrey/README.md, CREDITS.md and LICENSE.

#include "FeedbackSynthEngine.h"
#include "SubMenuSlider.hpp" // shift-secondaries as context-menu sliders
#include "PanelLayout.h"     // shared panel geometry (all modules, one PCB)
#include "VCVRackIO.h"       // platform: positional slots -> Rack indices
#include "io/AudreyLeds.h"   // the LED language, shared with the firmware
#include "io/IOBridge.h"
#include "io/PanelMap.h"
#include "params.h"
#include "plugin.hpp"

// ---------------------------------------------------------------------------
// Globals declared extern in params.h. main.cpp is not compiled into the
// plugin, so the definitions live here.
//
// One set per process, not per Module — a second Audrey in the same rack
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

volatile float gExciterIn = 0.0f;

struct Audrey : Module
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
        // ---- Panel buttons. Present because the hardware has them (SW2/SW3)
        //      and the panel art draws them; neither carries a gesture yet.
        //      SHIFT is what selects the shift-secondaries on hardware, and in
        //      Rack those are context-menu sliders instead — so the button is
        //      wired to the HAL and left inert rather than given a second,
        //      Rack-only meaning that the firmware would not share. ----
        MODE_PARAM,
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
        FBGAIN_CV_INPUT,
        ECHOSEND_CV_INPUT,
        ECHOFB_CV_INPUT,
        REVDECAY_CV_INPUT,
        EXCITER_INPUT,
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

    Audrey() : _io(this)
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Every knob is 0–1 and the range mapping lives in IOBridge, exactly as
        // it does for the hardware pots. Keeping the curve in one place is what
        // stops a knob and a MIDI CC landing on different values.
        configParam(PITCH_PARAM,
                    0.f,
                    1.f,
                    0.4286f,
                    "String pitch",
                    " note",
                    0.f,
                    56.f,
                    16.f);
        configParam(FBGAIN_PARAM,
                    0.f,
                    1.f,
                    0.f,
                    "Feedback gain",
                    " dB",
                    0.f,
                    42.f,
                    -30.f);
        configParam(FBBODY_PARAM, 0.f, 1.f, 0.f, "Body");
        configParam(FBLPF_PARAM, 0.f, 1.f, 1.f, "Feedback LPF");
        configParam(FBHPF_PARAM, 0.f, 1.f, 0.5397f, "Feedback HPF");
        configParam(ECHOSEND_PARAM, 0.f, 1.f, 0.f, "Echo send");
        configParam(ECHOTIME_PARAM, 0.f, 1.f, 0.5261f, "Echo time");
        configParam(ECHOFB_PARAM, 0.f, 1.f, 0.f, "Echo feedback");
        configParam(REVMIX_PARAM, 0.f, 1.f, 0.f, "Reverb mix");
        configParam(REVDECAY_PARAM, 0.f, 1.f, 0.f, "Reverb decay");
        configParam(VOL_PARAM, 0.f, 1.f, 0.7071f, "Volume");

        // Panel buttons — momentary, matching the hardware switches.
        configButton(MODE_PARAM, "Mode (unassigned)");
        configButton(SHIFT_PARAM,
                     "Shift — selects the secondary parameters on hardware; "
                     "in Rack they are in the context menu");

        configInput(VOCT_INPUT, "V/Oct");
        configInput(GATE_INPUT, "Gate (unused — reserved for VCA/envelope)");
        configInput(MIDI_INPUT, "MIDI (TRS — Rack uses software MIDI instead)");
        configInput(FBGAIN_CV_INPUT, "Feedback gain CV");
        configInput(ECHOSEND_CV_INPUT, "Echo send CV");
        configInput(ECHOFB_CV_INPUT, "Echo feedback CV");
        configInput(REVDECAY_CV_INPUT, "Reverb decay CV");
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

        _io.assignCV(Cv::VOCT, VOCT_INPUT);
        _io.assignCV(Cv::FBGAIN, FBGAIN_CV_INPUT);
        _io.assignCV(Cv::ECHOSEND, ECHOSEND_CV_INPUT);
        _io.assignCV(Cv::ECHOFB, ECHOFB_CV_INPUT);
        _io.assignCV(Cv::REVDECAY, REVDECAY_CV_INPUT);
        _io.assignCV(Cv::EXCITER, EXCITER_INPUT);
        // Cv::GATE is deliberately not assigned — see PanelMap.h.
        // MIDI_INPUT has no CV slot at all: it is a MIDI jack, not a CV one.

        _io.assignButton(Btn::MODE, MODE_PARAM);
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

        _engine.Init(APP->engine->getSampleRate());
    }

    void onSampleRateChange() override
    { _engine.Init(APP->engine->getSampleRate()); }

    void process(const ProcessArgs &args) override
    {
        // Control work at ~1 kHz rather than per sample, mirroring the
        // firmware's control tick. The engine smooths what needs smoothing.
        if(_controlPhase++ >= kControlDiv)
        {
            _controlPhase = 0;
            fillAudreyParams(_io);
            _engine.SetStringPitch(gStringPitch);
            _engine.SetFeedbackGain(gFeedbackGain);
            _engine.SetFeedbackDelay(gFeedbackDelay);
            _engine.SetFeedbackLPFCutoff(gFeedbackLPF);
            _engine.SetFeedbackHPFCutoff(gFeedbackHPF);
            _engine.SetEchoDelaySendAmount(gEchoSend);
            _engine.SetEchoDelayTime(gEchoTime);
            _engine.SetEchoDelayFeedback(gEchoFeedback);
            _engine.SetReverbMix(gReverbMix);
            _engine.SetReverbFeedback(gReverbDecay);
            _engine.SetOutputLevel(gOutputLevel);

            // LEDs, from the peaks accumulated since the previous tick. Same
            // call the firmware will make once Audrey has an IHardwareIO.
            AudreyLed::Signals sig;
            sig.peakL          = _peakL;
            sig.peakR          = _peakR;
            sig.exciter        = _peakExc;
            sig.exciterPatched = inputs[EXCITER_INPUT].isConnected();
            sig.shiftHeld      = _io.readButton(Btn::SHIFT);
            sig.modeHeld       = _io.readButton(Btn::MODE);
            _leds.update(sig, (float)(kControlDiv + 1) * args.sampleTime);
            _leds.writeTo(_io);
            _peakL = _peakR = _peakExc = 0.f;
        }

        // Exciter, per sample — this is the part the firmware cannot do yet.
        // Rack's ±5 V convention scaled to the engine's ±1.0.
        const float exciter = inputs[EXCITER_INPUT].isConnected()
                                  ? inputs[EXCITER_INPUT].getVoltage() * 0.2f
                                  : 0.0f;

        float outL = 0.f, outR = 0.f;
        _engine.Process(exciter, outL, outR);

        // Same master soft clip the firmware applies at its output edge, for
        // the same reason: the engine peaks past unity at useful settings.
#if AUDREY_OUTPUT_SOFTCLIP
        outL = daisysp::SoftClip(outL);
        outR = daisysp::SoftClip(outR);
#endif

        outputs[L_OUTPUT].setVoltage(outL * 5.f);
        outputs[R_OUTPUT].setVoltage(outR * 5.f);

        // Peak-hold for the LEDs. Taken post-clip, so the level pair shows what
        // leaves the module rather than what the engine wanted to send.
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
    static constexpr int kControlDiv = 47;

    infrasonic::FeedbackSynth::Engine _engine;
    VCVRackIO                         _io;
    AudreyLed::Engine                 _leds;
    int                               _controlPhase = 0;

    // Peak-hold accumulators, drained and reset on every control tick.
    float _peakL   = 0.f;
    float _peakR   = 0.f;
    float _peakExc = 0.f;
};

struct AudreyWidget : ModuleWidget
{
    AudreyWidget(Audrey *module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Audrey.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(
            createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(
            Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(
            createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH,
                                         RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Positions come from PanelLayout, indexed by the same positional slot
        // the HAL uses. Audrey and AlloyFlux share one PCB, so they share one
        // geometry — this is the only place it is written down.
        using namespace PanelLayout;

        // Top row: the resonator and its feedback loop.
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::PITCH), module, Audrey::PITCH_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::FBBODY), module, Audrey::FBBODY_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::FBGAIN), module, Audrey::FBGAIN_PARAM));

        // Mid row: the echo.
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::ECHOSEND), module, Audrey::ECHOSEND_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::ECHOTIME), module, Audrey::ECHOTIME_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::ECHOFB), module, Audrey::ECHOFB_PARAM));

        // Low row: space and tone. DECAY and FB LPF each carry a
        // shift-secondary, exposed in the context menu below.
        addParam(createParamCentered<Trimpot>(
            pot(Pot::REVMIX), module, Audrey::REVMIX_PARAM));
        addParam(createParamCentered<Davies1900hBlackKnob>(
            pot(Pot::REVDECAY), module, Audrey::REVDECAY_PARAM));
        addParam(createParamCentered<Trimpot>(
            pot(Pot::FBLPF), module, Audrey::FBLPF_PARAM));

        // --- Buttons (SW2 / SW3) ---
        addParam(createParamCentered<VCVButton>(
            button(Btn::MODE), module, Audrey::MODE_PARAM));
        addParam(createParamCentered<VCVButton>(
            button(Btn::SHIFT), module, Audrey::SHIFT_PARAM));

        // --- Jacks, in panel order (left to right, upper row then lower) ---
        // The panel labels the four modulation inputs CV 1..CV 4; what they
        // modulate is this module's choice. GATE stays unused, and FM IN is the
        // exciter — it sits between CV 3 and CV 4, not at the row's left end.
        addInput(createInputCentered<PJ301MPort>(
            at(kVOctMm), module, Audrey::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kGateMm), module, Audrey::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kMidiMm), module, Audrey::MIDI_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kCv1Mm), module, Audrey::FBGAIN_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kCv2Mm), module, Audrey::ECHOSEND_CV_INPUT));

        addInput(createInputCentered<PJ301MPort>(
            at(kCv3Mm), module, Audrey::ECHOFB_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kFmInMm), module, Audrey::EXCITER_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            at(kCv4Mm), module, Audrey::REVDECAY_CV_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            at(kOutLMm), module, Audrey::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            at(kOutRMm), module, Audrey::R_OUTPUT));

        // --- LEDs (7 × RGB) — colour driven by AudreyLed::Engine ---
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
            led(Led::LEVEL_L), module, Audrey::LED1_R_LIGHT));
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
            led(Led::LEVEL_R), module, Audrey::LED7_R_LIGHT));
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
            led(Led::LOOP_L), module, Audrey::LED2_R_LIGHT));
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
            led(Led::LOOP_R), module, Audrey::LED6_R_LIGHT));
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
            led(Led::ECHO), module, Audrey::LED3_R_LIGHT));
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
            led(Led::SPACE), module, Audrey::LED5_R_LIGHT));
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
            led(Led::CENTRE), module, Audrey::LED4_R_LIGHT));
    }

    // The two SHIFT-secondaries. On hardware these are the same physical knob
    // read while SHIFT is held; Rack has no shift key to hold, so they live in
    // the context menu — the same arrangement AlloyFlux uses for its six.
    void appendContextMenu(Menu *menu) override
    {
        auto *m = dynamic_cast<Audrey *>(module);
        if(!m)
            return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("SHIFT parameters"));

        menu->addChild(createMenuLabel("Volume  (SHIFT + DECAY)"));
        auto *volSlider     = new SubMenuSlider;
        volSlider->quantity = m->getParamQuantity(Audrey::VOL_PARAM);
        menu->addChild(volSlider);

        menu->addChild(createMenuLabel("Feedback HPF  (SHIFT + FB LPF)"));
        auto *hpfSlider     = new SubMenuSlider;
        hpfSlider->quantity = m->getParamQuantity(Audrey::FBHPF_PARAM);
        menu->addChild(hpfSlider);
    }
};

Model *modelAudrey = createModel<Audrey, AudreyWidget>("Audrey");
