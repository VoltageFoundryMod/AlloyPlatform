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
    cfg.stringPitch   = gCoilParams.stringPitch;
    cfg.feedbackGain  = gCoilParams.feedbackGain;
    cfg.feedbackDelay = gCoilParams.feedbackDelay;
    cfg.feedbackLPF   = gCoilParams.feedbackLPF;
    cfg.feedbackHPF   = gCoilParams.feedbackHPF;
    cfg.echoSend      = gCoilParams.echoSend;
    cfg.echoTime      = gCoilParams.echoTime;
    cfg.echoFeedback  = gCoilParams.echoFeedback;
    cfg.reverbMix     = gCoilParams.reverbMix;
    cfg.reverbDecay   = gCoilParams.reverbDecay;
    cfg.outputLevel   = gCoilParams.outputLevel;
    cfg.exciterLevel  = gCoilParams.exciterLevel;
    cfg.midiChannel   = gMidiChannel;
}

void applyCoilConfig(const CoilConfig &cfg)
{
    gCoilParams.stringPitch   = cfg.stringPitch;
    gCoilParams.feedbackGain  = cfg.feedbackGain;
    gCoilParams.feedbackDelay = cfg.feedbackDelay;
    gCoilParams.feedbackLPF   = cfg.feedbackLPF;
    gCoilParams.feedbackHPF   = cfg.feedbackHPF;
    gCoilParams.echoSend      = cfg.echoSend;
    gCoilParams.echoTime      = cfg.echoTime;
    gCoilParams.echoFeedback  = cfg.echoFeedback;
    gCoilParams.reverbMix     = cfg.reverbMix;
    gCoilParams.reverbDecay   = cfg.reverbDecay;
    gCoilParams.outputLevel   = cfg.outputLevel;
    gCoilParams.exciterLevel  = cfg.exciterLevel;
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
