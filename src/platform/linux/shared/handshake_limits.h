#pragma once

/*
 * Bounds on connections that have not finished their handshake yet, shared by
 * the encrypted hub listeners: one source host may hold only a few of them,
 * and each listener keeps half its table for peers past the handshake.
 */

#define HANDSHAKES_PER_HOST_MAX 4
#define HANDSHAKE_TIMEOUT_US 10000000
