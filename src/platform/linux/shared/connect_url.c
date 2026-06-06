#include "platform/linux/shared/connect_url.h"

#include <stdio.h>
#include <string.h>

#define TCP_URL_PREFIX "tcp://"
#define QUIC_URL_PREFIX "quic://"

bool ConnectUrl_ParseScheme(const char *url, uint8_t *scheme, const char **remainder)
{
    if (strncmp(url, TCP_URL_PREFIX, strlen(TCP_URL_PREFIX)) == 0) {
        *scheme = kCONNECT_SCHEME_TCP;
        *remainder = url + strlen(TCP_URL_PREFIX);
        return true;
    }
    if (strncmp(url, QUIC_URL_PREFIX, strlen(QUIC_URL_PREFIX)) == 0) {
        *scheme = kCONNECT_SCHEME_QUIC;
        *remainder = url + strlen(QUIC_URL_PREFIX);
        return true;
    }

    return false;
}

bool ConnectUrl_Parse(const char *url, uint8_t *scheme, char *host, char *port_text)
{
    const char *address;
    const char *separator;
    size_t host_length;

    if (!ConnectUrl_ParseScheme(url, scheme, &address)) {
        return false;
    }

    separator = strrchr(address, ':');
    if (separator == NULL || separator == address || separator[1] == '\0') {
        return false;
    }

    host_length = (size_t)(separator - address);
    if (host_length >= CONNECT_URL_HOST_MAX) {
        return false;
    }

    memcpy(host, address, host_length);
    host[host_length] = '\0';
    snprintf(port_text, CONNECT_URL_PORT_TEXT_MAX, "%s", separator + 1);

    return true;
}
