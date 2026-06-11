#include "platform/linux/shared/cli_meta.h"

#include <stdint.h>
#include <string.h>

#include "version.h"

/* ---------- public ---------- */

bool CliMeta_HandleVersionAndHelp(int argc, char **argv, const char *name, CliUsageFn usage)
{
    int32_t i;

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("%s %s\n", name, Version_String());
            return true;
        }
        if (strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return true;
        }
    }

    return false;
}
