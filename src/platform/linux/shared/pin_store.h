#pragma once

#include <stdbool.h>

/*
 * TOFU fingerprint store: a known_hosts-style text file with one
 * `<key> <fingerprint-hex>` pair per line. Keys are hub addresses on the
 * agent (known_hubs) and agent names on the hub (known_agents).
 */

#define PIN_STORE_KEY_MAX 256
#define PIN_STORE_FINGERPRINT_HEX_SIZE 65

bool PinStore_Lookup(const char *path, const char *key, char *fingerprint_hex);
bool PinStore_Append(const char *path, const char *key, const char *fingerprint_hex);
