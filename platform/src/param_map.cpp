#include "io/param_map.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Lookup and dispatch over the generated parameter tables.
//
// The tables themselves are the module's — each defines kParamTable /
// kParamCount / kEnumTable / kEnumCount from its own params.json. Everything
// here is the same work for any module, which is what lets USB MIDI, SysEx,
// the serial console and the Web Configurator all share one implementation.
// ---------------------------------------------------------------------------

const ParamDescriptor *paramMap_findByCC(uint8_t cc)
{
    for(uint8_t i = 0; i < kParamCount; i++)
    {
        if(kParamTable[i].cc == cc)
            return &kParamTable[i];
    }
    return nullptr;
}

const ParamDescriptor *paramMap_findByName(const char *name)
{
    if(!name)
        return nullptr;
    for(uint8_t i = 0; i < kParamCount; i++)
    {
        if(strcmp(kParamTable[i].name, name) == 0)
            return &kParamTable[i];
    }
    return nullptr;
}

const EnumParamDescriptor *paramMap_findEnumByCC(uint8_t cc)
{
    for(uint8_t i = 0; i < kEnumCount; i++)
    {
        if(kEnumTable[i].cc == cc)
            return &kEnumTable[i];
    }
    return nullptr;
}

const EnumParamDescriptor *paramMap_findEnumByName(const char *name)
{
    if(!name)
        return nullptr;
    for(uint8_t i = 0; i < kEnumCount; i++)
    {
        if(strcmp(kEnumTable[i].name, name) == 0)
            return &kEnumTable[i];
    }
    return nullptr;
}

bool paramMap_dispatchCC(uint8_t cc, uint8_t value)
{
    if(const ParamDescriptor *p = paramMap_findByCC(cc))
    {
        *p->target = p->fromCC(value);
        return true;
    }
    if(const EnumParamDescriptor *e = paramMap_findEnumByCC(cc))
    {
        *e->target = e->fromCC(value);
        if(e->onChange)
            e->onChange();
        return true;
    }
    return false;
}
