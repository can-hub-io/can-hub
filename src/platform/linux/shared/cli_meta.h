#pragma once

#include <stdbool.h>
#include <stdio.h>

/*
 * Shared --version / --help handling for the command-line entry points. When
 * either flag is present the binary must print and exit without doing anything
 * else, regardless of the other arguments. The usage callback renders the
 * binary's own usage text to the given stream.
 */
typedef void (*CliUsageFn)(FILE *stream, const char *program);

bool CliMeta_HandleVersionAndHelp(int argc, char **argv, const char *name, CliUsageFn usage);
