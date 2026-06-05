#include <stdio.h>

#include "version.h"

int main(void)
{
    printf("can-hub %s\n", Version_String());
    return 0;
}
