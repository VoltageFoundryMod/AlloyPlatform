#include "alloy_config.h"
#include "io/usb_midi.h" // gMidiChannel
#include "params.h"
#include <Arduino.h>
#include <string.h> // memset

// AlloyFlux's preset payload: the pack/apply half of flash persistence.
// The slot container, EEPROM mechanics, dirty check and rate limit are the
// platform's — see platform/src/config_store.cpp. This file only knows how to
// turn the gXxx globals into an AlloyConfig and back.

void packAlloyConfig(AlloyConfig &cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.baseFreq    = gBaseFreq;
    cfg.color       = gColor;
    cfg.relation    = gRelation;
    cfg.voiceMode   = (uint8_t)gVoiceMode;
    cfg.shape       = gShape;
    cfg.fatness     = gFatness;
    cfg.subOctave   = gSubOctave;
    cfg.motion      = gMotion;
    cfg.driftSpeed  = gDriftSpeed;
    cfg.chorusMode  = (uint8_t)gChorusMode;
    cfg.space       = gSpace;
    cfg.curve       = gCurve;
    cfg.curveTime   = gCurveTime;
    cfg.volume      = gVolume;
    cfg.midiChannel = gMidiChannel;
    // Filter
    cfg.filterCutoff = gFilterCutoff;
    cfg.filterRes    = gFilterRes;
    cfg.filterMode   = (uint8_t)gFilterMode;
    cfg.filterType   = (uint8_t)gFilterType;
    // Envelope
    cfg.envelopeType = (uint8_t)gEnvelopeType;
    cfg.adsrAttack   = gAdsrAttack;
    cfg.adsrDecay    = gAdsrDecay;
    cfg.adsrSustain  = gAdsrSustain;
    cfg.adsrRelease  = gAdsrRelease;
    cfg.adsrLoop     = gAdsrLoop;
    // Reverb
    cfg.revEnabled  = gRevEnabled;
    cfg.revMix      = gRevMix;
    cfg.revSize     = gRevSize;
    cfg.revDamping  = gRevDamping;
    cfg.revModSpeed = gRevModSpeed;
    cfg.revModDepth = gRevModDepth;
    cfg.revFrozen   = gRevFrozen;
    // Delay
    cfg.delayTime     = gDelayTime;
    cfg.delayFeedback = gDelayFeedback;
    cfg.delayMix      = gDelayMix;
    // FxOrder
    cfg.fxFilterPostChorus = gFxOrder.filterPostChorus;
    cfg.fxDelayPostReverb  = gFxOrder.delayPostReverb;
    // MIDI behaviour
    cfg.velocitySensitive = gVelocitySensitive;
    // Portamento / glide
    cfg.glideTime    = gGlideTime;
    cfg.glideEnabled = gGlideEnabled;
    // Scale quantizer (M49)
    cfg.quantizeScale = (uint8_t)gQuantizeScale;
    cfg.transpose     = gTranspose;
    // Knob takeover (M62)
    cfg.potTakeover = (uint8_t)gPotTakeoverMode;
    cfg.gateLength  = gGateLength;
}

void applyAlloyConfig(const AlloyConfig &cfg)
{
    gBaseFreq    = cfg.baseFreq;
    gColor       = cfg.color;
    gRelation    = cfg.relation;
    gVoiceMode   = (VoiceMode)cfg.voiceMode;
    gShape       = cfg.shape;
    gFatness     = cfg.fatness;
    gSubOctave   = (cfg.subOctave == 2) ? 2u : 1u;
    gMotion      = cfg.motion;
    gDriftSpeed  = cfg.driftSpeed;
    gChorusMode  = (ChorusMode)cfg.chorusMode;
    gSpace       = cfg.space;
    gCurve       = cfg.curve;
    gCurveTime   = cfg.curveTime;
    gVolume      = cfg.volume;
    gMidiChannel = cfg.midiChannel;
    // Filter
    gFilterCutoff = cfg.filterCutoff;
    gFilterRes    = cfg.filterRes;
    gFilterMode   = (FilterMode)cfg.filterMode;
    gFilterType   = (FilterType)cfg.filterType;
    // Envelope
    gEnvelopeType = (EnvelopeType)cfg.envelopeType;
    gAdsrAttack   = cfg.adsrAttack;
    gAdsrDecay    = cfg.adsrDecay;
    gAdsrSustain  = cfg.adsrSustain;
    gAdsrRelease  = cfg.adsrRelease;
    gAdsrLoop     = cfg.adsrLoop;
    // Reverb
    gRevEnabled  = cfg.revEnabled;
    gRevMix      = cfg.revMix;
    gRevSize     = cfg.revSize;
    gRevDamping  = cfg.revDamping;
    gRevModSpeed = cfg.revModSpeed;
    gRevModDepth = cfg.revModDepth;
    gRevFrozen   = cfg.revFrozen;
    // Delay
    gDelayTime     = cfg.delayTime;
    gDelayFeedback = cfg.delayFeedback;
    gDelayMix      = cfg.delayMix;
    // FxOrder
    gFxOrder.filterPostChorus = cfg.fxFilterPostChorus;
    gFxOrder.delayPostReverb  = cfg.fxDelayPostReverb;
    // MIDI behaviour
    gVelocitySensitive = cfg.velocitySensitive;
    // Portamento / glide
    gGlideTime    = cfg.glideTime;
    gGlideEnabled = cfg.glideEnabled;
    // Scale quantizer (M49)
    gQuantizeScale = (cfg.quantizeScale < (uint8_t)ScaleId::COUNT)
                         ? (ScaleId)cfg.quantizeScale
                         : ScaleId::CHROMATIC;
    gTranspose     = (int8_t)constrain((int)cfg.transpose, -24, 24);
    // Knob takeover (M62) — unknown values fall back to the SCALE default.
    gPotTakeoverMode = (cfg.potTakeover <= (uint8_t)PotTakeoverMode::SCALE)
                           ? (PotTakeoverMode)cfg.potTakeover
                           : PotTakeoverMode::SCALE;
    gGateLength      = constrain(cfg.gateLength, 0.0f, 2000.0f);
}

void applyAlloyDefaults()
{
    AlloyConfig d        = {};
    d.baseFreq           = 440.0f;
    d.color              = 0.0f;
    d.relation           = 0.0f;
    d.voiceMode          = (uint8_t)VoiceMode::PAIR;
    d.shape              = 0.0f;
    d.fatness            = 0.4f;
    d.subOctave          = 1;
    d.motion             = 0.0f;
    d.driftSpeed         = 0.04f;
    d.chorusMode         = (uint8_t)ChorusMode::I_II;
    d.space              = 1.0f;
    d.curve              = 0.5f;
    d.curveTime          = 1.0f;
    d.volume             = 1.0f;
    d.midiChannel        = 0;
    d.filterCutoff       = kDefaultFilterCutoff;
    d.filterRes          = 0.0f;
    d.filterMode         = (uint8_t)FilterMode::OFF;
    d.filterType         = (uint8_t)FilterType::SVF;
    d.envelopeType       = (uint8_t)EnvelopeType::AR;
    d.adsrAttack         = 0.05f;
    d.adsrDecay          = 0.10f;
    d.adsrSustain        = 0.8f;
    d.adsrRelease        = 0.30f;
    d.adsrLoop           = false;
    d.revEnabled         = false;
    d.revMix             = 0.0f;
    d.revSize            = 0.5f;
    d.revDamping         = 0.5f;
    d.revModSpeed        = 1.0f;
    d.revModDepth        = 1.0f;
    d.revFrozen          = false;
    d.delayTime          = 100.0f;
    d.delayFeedback      = 0.5f;
    d.delayMix           = 0.0f;
    d.fxFilterPostChorus = false;
    d.fxDelayPostReverb  = false;
    d.velocitySensitive  = true;
    d.glideTime          = 0.0f;
    d.glideEnabled       = false;
    d.quantizeScale      = (uint8_t)ScaleId::CHROMATIC;
    d.transpose          = 0;
    d.potTakeover        = (uint8_t)PotTakeoverMode::SCALE;
    d.gateLength         = 0.0f;
    applyAlloyConfig(d);
}
