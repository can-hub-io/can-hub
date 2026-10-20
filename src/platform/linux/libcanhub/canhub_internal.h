#pragma once

#include "canhub.h"

/*
 * What the libraries' shared modules need from a session beyond the public
 * API, implemented by each platform's canhub.c and kept out of the exports.
 */
void CanHubSession_SetError(CanHubSession *session, const char *message);
