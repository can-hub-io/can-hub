#include "platform/linux/shared/connect_url.h"

#include <stdio.h>
#include <string.h>

#define TCP_URL_PREFIX "tcp://"
#define QUIC_URL_PREFIX "quic://"
#define UNIX_URL_PREFIX "unix://"
#define TLS_URL_PREFIX "tls://"
#define LISTEN_ANY_ADDRESS "0.0.0.0"

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
    if (strncmp(url, UNIX_URL_PREFIX, strlen(UNIX_URL_PREFIX)) == 0) {
        *scheme = kCONNECT_SCHEME_UNIX;
        *remainder = url + strlen(UNIX_URL_PREFIX);
        return true;
    }
    if (strncmp(url, TLS_URL_PREFIX, strlen(TLS_URL_PREFIX)) == 0) {
        *scheme = kCONNECT_SCHEME_TLS;
        *remainder = url + strlen(TLS_URL_PREFIX);
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

    if (*scheme == kCONNECT_SCHEME_UNIX) {
        if (address[0] == '\0' || strlen(address) >= CONNECT_URL_HOST_MAX) {
            return false;
        }
        snprintf(host, CONNECT_URL_HOST_MAX, "%s", address);
        port_text[0] = '\0';
        return true;
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

bool ConnectUrl_SplitListenAddress(const char *remainder, char *bind_address, char *port_text)
{
    const char *separator = strrchr(remainder, ':');
    size_t bind_length;

    if (separator == NULL) {
        if (remainder[0] == '\0') {
            return false;
        }
        snprintf(bind_address, CONNECT_URL_HOST_MAX, "%s", LISTEN_ANY_ADDRESS);
        snprintf(port_text, CONNECT_URL_PORT_TEXT_MAX, "%s", remainder);
        return true;
    }

    if (separator == remainder || separator[1] == '\0') {
        return false;
    }

    bind_length = (size_t)(separator - remainder);
    if (bind_length >= CONNECT_URL_HOST_MAX) {
        return false;
    }

    memcpy(bind_address, remainder, bind_length);
    bind_address[bind_length] = '\0';
    snprintf(port_text, CONNECT_URL_PORT_TEXT_MAX, "%s", separator + 1);

    return true;
}
