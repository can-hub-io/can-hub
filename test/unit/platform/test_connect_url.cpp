#include <cest>

#include <cstring>

extern "C" {
#include "platform/linux/shared/connect_url.h"
}

static char bind_address[CONNECT_URL_HOST_MAX];
static char port_text[CONNECT_URL_PORT_TEXT_MAX];

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
