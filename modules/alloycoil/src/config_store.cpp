#include "coil_config.h"
#include "io/usb_midi.h"              // gMidiChannel
#include "param_manifest.generated.h" // applyParamDefaults()
#include "params.h"
#include <string.h> // memset

// Alloy Coil's preset payload. The slot container, EEPROM mechanics, dirty check
// and rate limit are the platform's — see platform/src/config_store.cpp.

void packCoilConfig(CoilConfig &cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.stringPitch   = gStringPitch;
    cfg.feedbackGain  = gFeedbackGain;
    cfg.feedbackDelay = gFeedbackDelay;
    cfg.feedbackLPF   = gFeedbackLPF;
    cfg.feedbackHPF   = gFeedbackHPF;
    cfg.echoSend      = gEchoSend;
    cfg.echoTime      = gEchoTime;
    cfg.echoFeedback  = gEchoFeedback;
    cfg.reverbMix     = gReverbMix;
    cfg.reverbDecay   = gReverbDecay;
    cfg.outputLevel   = gOutputLevel;
    cfg.exciterLevel  = gExciterLevel;
    cfg.midiChannel   = gMidiChannel;
}

void applyCoilConfig(const CoilConfig &cfg)
{
    gStringPitch   = cfg.stringPitch;
    gFeedbackGain  = cfg.feedbackGain;
    gFeedbackDelay = cfg.feedbackDelay;
    gFeedbackLPF   = cfg.feedbackLPF;
    gFeedbackHPF   = cfg.feedbackHPF;
    gEchoSend      = cfg.echoSend;
    gEchoTime      = cfg.echoTime;
    gEchoFeedback  = cfg.echoFeedback;
    gReverbMix     = cfg.reverbMix;
    gReverbDecay   = cfg.reverbDecay;
    gOutputLevel   = cfg.outputLevel;
    gExciterLevel  = cfg.exciterLevel;
    gMidiChannel   = (cfg.midiChannel <= 16) ? cfg.midiChannel : 0;
}

void applyCoilDefaults()
{
    // Straight off the manifest — every value here used to be a second copy of
    // params.json's `default` column, and the copy is what went stale.
    applyParamDefaults();
    // Not a parameter: the MIDI channel has no manifest row. 0 is omni.
    gMidiChannel = 0;
}
