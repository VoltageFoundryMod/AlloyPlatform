#include "io/commands.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Command dispatch — the mechanism. The table it walks (kCommands /
// kCommandCount) is defined by the module, so every transport that can hand
// over a line of text shares one implementation regardless of which engine is
// built.
// ---------------------------------------------------------------------------

void commands_printHelp(Print &out)
{
    out.println(F("Commands:"));
    for(uint8_t i = 0; i < kCommandCount; i++)
    {
        out.print(F("  "));
        out.print(kCommands[i].name);
        out.print(' ');
        out.println(kCommands[i].help);
    }
}

void commands_dispatch(const char *cmd, Print &out)
{
    // Skip leading whitespace
    while(*cmd == ' ')
        cmd++;

    // Longest match wins: "filter type" must be found before "filter", and the
    // table is not required to be ordered for that to hold.
    const CommandEntry *best    = nullptr;
    size_t              bestLen = 0;
    for(uint8_t i = 0; i < kCommandCount; i++)
    {
        const char  *name = kCommands[i].name;
        const size_t nlen = strlen(name);
        if(strncmp(cmd, name, nlen) == 0
           && (cmd[nlen] == ' ' || cmd[nlen] == '\0'))
        {
            if(!best || nlen > bestLen)
            {
                best    = &kCommands[i];
                bestLen = nlen;
            }
        }
    }
    if(best)
    {
        const char *args = (cmd[bestLen] == ' ') ? cmd + bestLen + 1 : "";
        best->handler(args, out);
        return;
    }
    out.print(F("unknown: "));
    out.println(cmd);
    commands_printHelp(out);
}
