// Output level of every voice mode, same patch, so they can be compared.
#include "SynthEngine.h"
#include <cmath>
#include <cstdio>

volatile bool gGatePatched = false;
volatile bool gGateHigh    = false;

static SynthEngine eng;
static PolySlot    slots[6];

static const char *kNames[7]
    = {"PAIR", "CLOUD", "CHORD", "CASCADE", "STRING", "POLY", "PLASMA"};

static void allNotesOff()
{
    for(int i = 0; i < 6; i++)
    {
        slots[i].freq     = 220.0f;
        slots[i].velocity = 1.0f;
        slots[i].midiNote = kPolySlotFree;
        if(eng.polyEnvs[i])
        {
            eng.polyEnvs[i]->setGate(false);
            eng.polyEnvs[i]->reset();
        }
    }
}

int main()
{
    eng.init(48000u, 128u);

    printf("\n  Same patch every mode: ROOT 220 Hz, SHAPE saw, RELATION 7,\n");
    printf("  COLOR 0.5, MOTION 0, FATNESS 0, VOLUME 1, no effects.\n");
    printf("  'V' assumes full scale (32767) = 4.4 V peak, as measured on the jacks.\n\n");
    printf("  mode      RMS    peak   %%FS   ~V peak   dB vs PAIR\n");

    float pairRms = 0.0f;
    for(int m = 0; m < 7; m++)
    {
        SynthParams p;
        p.voiceMode = (VoiceMode)m;
        p.baseFreq  = 220.0f;
        p.shape     = 0.5f; // saw
        p.relation  = 7.0f;
        p.color     = 0.5f;
        p.motion    = 0.0f;
        p.fatness   = 0.0f;
        p.volume    = 1.0f;
        // Drone for the mono modes; a held note for the slot-driven ones, so
        // each mode is measured doing the thing it actually does.
        const bool poly = modeUsesPolySlots((VoiceMode)m);
        p.gatePatched   = poly;
        p.gateHigh      = poly;
        gGatePatched    = poly;
        gGateHigh       = poly;

        allNotesOff();

        SynthControlOutput co;
        // Establish the mode FIRST. control()'s mode-change cleanup resets the
        // poly envelopes, so a note armed before this tick is wiped.
        for(int t = 0; t < 4; t++)
            eng.control(p, slots, co);
        if(poly)
        {
            slots[0].midiNote = 60;
            slots[0].freq     = 220.0f;
            eng.polyEnvs[0]->setGate(false); // clear the latch before re-arming
            eng.polyRetrigger(0, 220.0f, 0.5f);
        }

        for(int t = 0; t < 120; t++) // settle
        {
            eng.control(p, slots, co);
            for(int s = 0; s < 375; s++)
            {
                int32_t l, r, dl, dr;
                eng.audio(0, 0, 0.0f, false, &l, &r, &dl, &dr);
            }
        }
        double sum = 0.0;
        long   n   = 0;
        float  pk  = 0.0f;
        for(int t = 0; t < 256; t++)
        {
            eng.control(p, slots, co);
            for(int s = 0; s < 375; s++)
            {
                int32_t l, r, dl, dr;
                eng.audio(0, 0, 0.0f, false, &l, &r, &dl, &dr);
                sum += (double)l * l + (double)r * r;
                n += 2;
                if(fabsf((float)l) > pk) pk = fabsf((float)l);
                if(fabsf((float)r) > pk) pk = fabsf((float)r);
            }
        }
        const float rms = (float)sqrt(sum / (double)n);
        if(m == 0) pairRms = rms;
        printf("  %-8s %6.0f  %6.0f  %3.0f%%   %5.2f V   %+5.1f dB\n",
               kNames[m], rms, pk, 100.0f * pk / 32767.0f,
               4.4f * pk / 32767.0f,
               20.0f * log10f(rms / pairRms));
    }
    printf("\n");
    return 0;
}
