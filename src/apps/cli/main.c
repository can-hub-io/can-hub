#include <stdio.h>

#include "version.h"

int main(void)
{
    printf("can-hub-cli %s\n", Version_String());
    return 0;
}
