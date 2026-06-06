#ifndef PLATFORM_LINUX_SHARED_CONNECT_URL_H
#define PLATFORM_LINUX_SHARED_CONNECT_URL_H

#include <stdbool.h>
#include <stdint.h>

#define CONNECT_URL_HOST_MAX 256
#define CONNECT_URL_PORT_TEXT_MAX 16

typedef enum tconnect_scheme_e {
    kCONNECT_SCHEME_TCP = 0,
    kCONNECT_SCHEME_QUIC,
    kCONNECT_SCHEME_MAX,
} TCONNECT_SCHEME;

bool ConnectUrl_ParseScheme(const char *url, uint8_t *scheme, const char **remainder);
bool ConnectUrl_Parse(const char *url, uint8_t *scheme, char *host, char *port_text);

#endif
