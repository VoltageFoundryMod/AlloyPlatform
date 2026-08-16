// Audrey's half of the platform's generic mechanisms.
// See platform/include/ModuleHooks.h for the contract.

#include "ModuleHooks.h"

#include "audrey_config.h"
#include "params.h"
#include <Arduino.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
const char *const kModuleManufacturer = "Voltage Foundry Modular";
const char *const kModuleProduct      = "Audrey II";
const char *const kModuleMidiName     = "Audrey II MIDI";

// SysEx device signature — 'A','U'. Must differ from AlloyFlux's 'A','F':
// the Web Configurator reads this to decide which parameter map to load.
const uint8_t kSysExDevId0 = 'A';
const uint8_t kSysExDevId1 = 'U';

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------

// Audrey is a drone/feedback instrument, not a keyboard voice: there is no
// envelope and no gate. A note sets the resonator's pitch and keeps it there —
// which is what upstream's Frequency parameter does, just driven by a key
// instead of a knob. Note Off is therefore deliberately silent: releasing a key
// must not stop a drone that is sustaining on its own feedback.
void moduleHook_noteOn(uint8_t note, uint8_t /*velocity*/)
{
    if(note >= 16 && note <= 72)
        gStringPitch = (float)note;
}

void moduleHook_noteOff(uint8_t /*note*/)
{
    // Intentionally empty — see above.
}

bool moduleHook_controlChange(uint8_t cc, uint8_t value)
{
    switch(cc)
    {
        case 123: // All Notes Off / panic — collapse the feedback loop, which
                  // is the only thing here that can run away.
            gFeedbackGain = -60.0f;
            gEchoFeedback = 0.0f;
            (void)value;
            return true;
        default: return false;
    }
}

void moduleHook_programChange(uint8_t /*program*/)
{
    // No program concept — presets are addressed by SysEx slot instead.
}

uint8_t moduleHook_extraPatchPairs(uint8_t * /*buf*/, uint8_t /*maxBytes*/)
{
    // Every Audrey parameter is in the generated manifest, so there is nothing
    // to add beyond it.
    return 0;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

const uint16_t kEngineId      = kAudreyEngineId;
const uint16_t kEngineVersion = kAudreyEngineVersion;

uint16_t moduleHook_packConfig(void *blob, uint16_t maxBytes)
{
    if(maxBytes < sizeof(AudreyConfig))
        return 0;
    AudreyConfig cfg;
    packAudreyConfig(cfg);
    memcpy(blob, &cfg, sizeof(cfg));
    return (uint16_t)sizeof(cfg);
}

void moduleHook_applyConfig(const void *blob, uint16_t bytes)
{
    if(bytes < sizeof(AudreyConfig))
        return;
    AudreyConfig cfg;
    memcpy(&cfg, blob, sizeof(cfg));
    applyAudreyConfig(cfg);
}

void moduleHook_applyDefaults()
{ applyAudreyDefaults(); }
