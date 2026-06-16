#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/linux/tls/tls_channel.h"
#include "platform/linux/tls/tls_client_security.h"
#include "platform/linux/tls/tls_server_security.h"
#include "platform/linux/shared/pin_store.h"
#include "platform/linux/shared/tls_identity.h"
}

#define STATE_DIRECTORY "/tmp/can_hub_test_tls"
#define PIN_STORE_PATH STATE_DIRECTORY "/known_hubs"
#define PIN_KEY "localhost:7227"
#define WRONG_FINGERPRINT "0000000000000000000000000000000000000000000000000000000000000000"
#define PUMP_ROUNDS_MAX 64

static TlsServerSecurity server_security;
static TlsClientSecurity client_security;
static TlsChannel server_channel;
static TlsChannel client_channel;
static char hub_certificate[TLS_IDENTITY_PATH_MAX];
static char hub_key[TLS_IDENTITY_PATH_MAX];
static char client_certificate[TLS_IDENTITY_PATH_MAX];
static char client_key[TLS_IDENTITY_PATH_MAX];

static bool startLoopbackPair(void);
static bool pumpUntilEstablished(bool *client_failed);
static void closeLoopbackPair(void);
static void captureMessage(void *context, const uint8_t *message, size_t size);

static uint8_t captured_message[256];
static size_t captured_size;

describe("tls_channel", []() {
    beforeEach([]() {
        TlsChannel_Reset(&server_channel);
        TlsChannel_Reset(&client_channel);
        mkdir(STATE_DIRECTORY, 0755);
        TlsIdentity_LoadOrCreate(STATE_DIRECTORY, "hub", hub_certificate, hub_key);
        TlsIdentity_LoadOrCreate(STATE_DIRECTORY, "client", client_certificate, client_key);
        std::remove(PIN_STORE_PATH);
    });

    afterEach([]() {
        closeLoopbackPair();
        TlsServerSecurity_Free(&server_security);
        TlsClientSecurity_Free(&client_security);
    });

    it("completes a loopback handshake and carries a message", []() {
        const uint8_t message[] = { 0x7F, 0x00, 0x00, 0x00 };
        MessageSink sink = { NULL, captureMessage };
        bool client_failed = false;
        bool established;

        expect(startLoopbackPair()).toBe(true);
        established = pumpUntilEstablished(&client_failed);

        expect(established).toBe(true);
        expect(TlsChannel_Queue(&client_channel, message, sizeof(message))).toBe(true);
        expect(TlsChannel_Flush(&client_channel)).toBe(true);
        captured_size = 0;
        expect(TlsChannel_Receive(&server_channel, &sink)).toBe(true);
        expect(captured_size).toBe(sizeof(message));
        expect((const uint8_t *)captured_message).toEqualMemory(message, sizeof(message));
    });

    it("exposes the client fingerprint to the server after the handshake", []() {
        char fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE] = "";
        bool client_failed = false;

        expect(startLoopbackPair()).toBe(true);
        expect(pumpUntilEstablished(&client_failed)).toBe(true);

        expect(TlsChannel_PeerFingerprint(&server_channel, fingerprint)).toBe(true);
        expect(strlen(fingerprint)).toBe((size_t)64);
    });

    it("FingerprintOfFile matches the fingerprint seen on the wire", []() {
        char handshake_fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE] = "";
        char file_fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE] = "";
        bool client_failed = false;

        expect(startLoopbackPair()).toBe(true);
        expect(pumpUntilEstablished(&client_failed)).toBe(true);

        expect(TlsChannel_PeerFingerprint(&server_channel, handshake_fingerprint)).toBe(true);
        expect(TlsIdentity_FingerprintOfFile(client_certificate, file_fingerprint)).toBe(true);
        expect((const char *)file_fingerprint).toBe((const char *)handshake_fingerprint);
    });

    it("pins the server fingerprint on first contact", []() {
        char pinned[PIN_STORE_FINGERPRINT_HEX_SIZE];
        bool client_failed = false;

        expect(startLoopbackPair()).toBe(true);
        expect(pumpUntilEstablished(&client_failed)).toBe(true);

        expect(PinStore_Lookup(PIN_STORE_PATH, PIN_KEY, pinned)).toBe(true);
    });

    it("rejects the handshake when the pinned fingerprint differs", []() {
        bool client_failed = false;

        PinStore_Append(PIN_STORE_PATH, PIN_KEY, WRONG_FINGERPRINT);

        expect(startLoopbackPair()).toBe(true);
        pumpUntilEstablished(&client_failed);

        expect(client_failed).toBe(true);
        expect(TlsChannel_IsEstablished(&client_channel)).toBe(false);
    });
});

/* ---------- private ---------- */

static bool startLoopbackPair(void)
{
    TlsClientSecurityConfig config;
    SSL *server_session;
    SSL *client_session;
    int sockets[2];

    memset(&config, 0, sizeof(config));
    config.certificate_path = client_certificate;
    config.key_path = client_key;
    config.pin_store_path = PIN_STORE_PATH;
    config.pin_key = PIN_KEY;

    if (!TlsServerSecurity_Init(&server_security, hub_certificate, hub_key)) {
        return false;
    }
    if (!TlsClientSecurity_Init(&client_security, &config)) {
        return false;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets) != 0) {
        return false;
    }
    if (!TlsServerSecurity_NewSession(&server_security, &server_session)) {
        return false;
    }
    if (!TlsClientSecurity_NewSession(&client_security, "localhost", &client_session)) {
        return false;
    }

    TlsChannel_Bind(&server_channel, sockets[0], server_session);
    TlsChannel_Bind(&client_channel, sockets[1], client_session);

    return true;
}

static bool pumpUntilEstablished(bool *client_failed)
{
    uint8_t round;

    for(round=0; round<PUMP_ROUNDS_MAX; round++) {
        if (!TlsChannel_Pump(&client_channel)) {
            *client_failed = true;
            return false;
        }
        if (!TlsChannel_Pump(&server_channel)) {
            return false;
        }
        if (TlsChannel_IsEstablished(&client_channel) && TlsChannel_IsEstablished(&server_channel)) {
            return true;
        }
    }

    return false;
}

static void closeLoopbackPair(void)
{
    int32_t server_fd = server_channel.fd;
    int32_t client_fd = client_channel.fd;

    TlsChannel_Close(&server_channel);
    TlsChannel_Close(&client_channel);
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (client_fd >= 0) {
        close(client_fd);
    }
}

static void captureMessage(void *context, const uint8_t *message, size_t size)
{
    (void)context;
    memcpy(captured_message, message, size);
    captured_size = size;
}