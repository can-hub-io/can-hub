#include <stdio.h>

#include "version.h"

int main(void)
{
    printf("can-hub-agent %s\n", Version_String());
    return 0;
}
