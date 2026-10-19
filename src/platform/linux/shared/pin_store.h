#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * TOFU fingerprint store: a known_hosts-style text file with one
 * `<key>[,<key>...] <fingerprint-hex>` entry per line, every key of an entry
 * naming the same peer. Keys are hub addresses on the agent and the client
 * (known_hubs), written by PinStore_HubKey so that one address spelled two
 * ways is one key.
 */

#define PIN_STORE_KEY_MAX 256
#define PIN_STORE_FINGERPRINT_HEX_SIZE 65

void PinStore_HubKey(const char *host, const char *port_text, char *key, size_t key_size);
bool PinStore_ParseFingerprint(const char *text, char *fingerprint_hex);

bool PinStore_Lookup(const char *path, const char *key, char *fingerprint_hex);
bool PinStore_KnowsFingerprint(const char *path, const char *fingerprint_hex);
bool PinStore_IsEmpty(const char *path);
bool PinStore_Pin(const char *path, const char *key, const char *fingerprint_hex);
