#include "audrey_config.h"
#include "io/usb_midi.h" // gMidiChannel
#include "params.h"
#include <string.h> // memset

// Audrey's preset payload. The slot container, EEPROM mechanics, dirty check
// and rate limit are the platform's — see platform/src/config_store.cpp.

void packAudreyConfig(AudreyConfig &cfg)
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

void applyAudreyConfig(const AudreyConfig &cfg)
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

void applyAudreyDefaults()
{
    // Upstream's registerParams() defaults — see params.json.
    AudreyConfig d  = {};
    d.stringPitch   = 40.0f;
    d.feedbackGain  = -30.0f;
    d.feedbackDelay = 0.001f;
    d.feedbackLPF   = 18000.0f;
    d.feedbackHPF   = 250.0f;
    d.echoSend      = 0.0f;
    d.echoTime      = 0.5f;
    d.echoFeedback  = 0.0f;
    d.reverbMix     = 0.0f;
    d.reverbDecay   = 0.2f;
    d.outputLevel   = 0.5f;
    d.exciterLevel  = 1.0f;
    d.midiChannel   = 0;
    applyAudreyConfig(d);
}
