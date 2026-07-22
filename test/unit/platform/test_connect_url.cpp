#include <cest>

#include <cstring>

extern "C" {
#include "platform/linux/shared/connect_url.h"
}

static char bind_address[CONNECT_URL_HOST_MAX];
static char host[CONNECT_URL_HOST_MAX];
static char port_text[CONNECT_URL_PORT_TEXT_MAX];
static uint8_t scheme;

describe("connect_url_split_listen_address", []() {
    it("defaults to any address when only a port is given", []() {
        expect(ConnectUrl_SplitListenAddress("7228", bind_address, port_text)).toBe(true);
        expect((const char *)bind_address).toBe("0.0.0.0");
        expect((const char *)port_text).toBe("7228");
    });

    it("splits an explicit bind address and port", []() {
        expect(ConnectUrl_SplitListenAddress("10.0.0.5:7228", bind_address, port_text)).toBe(true);
        expect((const char *)bind_address).toBe("10.0.0.5");
        expect((const char *)port_text).toBe("7228");
    });

    it("splits a bracketed IPv6 bind address and port", []() {
        expect(ConnectUrl_SplitListenAddress("[::1]:7228", bind_address, port_text)).toBe(true);
        expect((const char *)bind_address).toBe("::1");
        expect((const char *)port_text).toBe("7228");
    });

    it("splits a bracketed IPv6 any address", []() {
        expect(ConnectUrl_SplitListenAddress("[::]:7227", bind_address, port_text)).toBe(true);
        expect((const char *)bind_address).toBe("::");
        expect((const char *)port_text).toBe("7227");
    });

    it("rejects a bracketed IPv6 address without a port", []() {
        expect(ConnectUrl_SplitListenAddress("[::1]", bind_address, port_text)).toBe(false);
    });

    it("rejects an unterminated IPv6 bracket", []() {
        expect(ConnectUrl_SplitListenAddress("[::1:7228", bind_address, port_text)).toBe(false);
    });

    it("rejects an empty port", []() {
        expect(ConnectUrl_SplitListenAddress("10.0.0.5:", bind_address, port_text)).toBe(false);
    });

    it("rejects an empty bind address", []() {
        expect(ConnectUrl_SplitListenAddress(":7228", bind_address, port_text)).toBe(false);
    });

    it("rejects an empty remainder", []() {
        expect(ConnectUrl_SplitListenAddress("", bind_address, port_text)).toBe(false);
    });
});

describe("connect_url_parse", []() {
    it("parses a hostname and port", []() {
        expect(ConnectUrl_Parse("tls://hub.example.com:7227", &scheme, host, port_text)).toBe(true);
        expect(scheme).toBe((uint8_t)kCONNECT_SCHEME_TLS);
        expect((const char *)host).toBe("hub.example.com");
        expect((const char *)port_text).toBe("7227");
    });

    it("parses an IPv4 address and port", []() {
        expect(ConnectUrl_Parse("quic://10.0.0.5:7227", &scheme, host, port_text)).toBe(true);
        expect(scheme).toBe((uint8_t)kCONNECT_SCHEME_QUIC);
        expect((const char *)host).toBe("10.0.0.5");
        expect((const char *)port_text).toBe("7227");
    });

    it("strips brackets from an IPv6 literal", []() {
        expect(ConnectUrl_Parse("quic://[::1]:7227", &scheme, host, port_text)).toBe(true);
        expect(scheme).toBe((uint8_t)kCONNECT_SCHEME_QUIC);
        expect((const char *)host).toBe("::1");
        expect((const char *)port_text).toBe("7227");
    });

    it("rejects a bracketed IPv6 literal without a port", []() {
        expect(ConnectUrl_Parse("tcp://[::1]", &scheme, host, port_text)).toBe(false);
    });

    it("rejects an unterminated IPv6 bracket", []() {
        expect(ConnectUrl_Parse("tcp://[::1:7227", &scheme, host, port_text)).toBe(false);
    });
});
