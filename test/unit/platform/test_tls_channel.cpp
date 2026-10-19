#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include <sys/stat.h>

#include "platform/linux/tls/tls_channel.h"
#include "platform/linux/tls/tls_client_security.h"
#include "platform/linux/tls/tls_server_security.h"
#include "platform/linux/shared/pin_store.h"
#include "platform/linux/shared/tls_aead.h"
#include "platform/linux/shared/tls_identity.h"
}

#define STATE_DIRECTORY "/tmp/can_hub_test_tls"
#define PIN_STORE_PATH STATE_DIRECTORY "/known_hubs"
#define PIN_KEY "localhost:7227"
#define OTHER_ADDRESS_KEY "127.0.0.1:7227"
#define WRONG_FINGERPRINT "0000000000000000000000000000000000000000000000000000000000000000"
#define PUMP_ROUNDS_MAX 64
#define SHUTTLE_BUFFER_SIZE 32768

static TlsServerSecurity server_security;
static TlsClientSecurity client_security;
static TlsChannel server_channel;
static TlsChannel client_channel;
static char hub_certificate[TLS_IDENTITY_PATH_MAX];
static char hub_key[TLS_IDENTITY_PATH_MAX];
static char client_certificate[TLS_IDENTITY_PATH_MAX];
static char client_key[TLS_IDENTITY_PATH_MAX];
static char hub_fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];
static uint8_t pin_policy;
static const char *expected_fingerprint;

static bool startChannelPair(void);
static bool pumpUntilEstablished(bool *client_failed);
static bool shuttle(TlsChannel *from, TlsChannel *to, const MessageSink *sink);
static void closeChannelPair(void);
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
        TlsIdentity_FingerprintOfFile(hub_certificate, hub_fingerprint);
        std::remove(PIN_STORE_PATH);
        pin_policy = kPIN_POLICY_ANY_HUB;
        expected_fingerprint = NULL;
    });

    afterEach([]() {
        closeChannelPair();
        TlsServerSecurity_Free(&server_security);
        TlsClientSecurity_Free(&client_security);
    });

    it("completes a handshake", []() {
        bool client_failed = false;
        bool started;
        bool established;

        started = startChannelPair();

        established = pumpUntilEstablished(&client_failed);

        expect(started).toBe(true);
        expect(established).toBe(true);
    });

    it("carries a message once established", []() {
        const uint8_t message[] = { 0x7F, 0x00, 0x00, 0x00 };
        MessageSink sink = { NULL, captureMessage };
        bool client_failed = false;
        bool queued;
        bool flushed;
        bool shuttled;

        startChannelPair();
        pumpUntilEstablished(&client_failed);
        captured_size = 0;

        queued = TlsChannel_Queue(&client_channel, message, sizeof(message));
        flushed = TlsChannel_Flush(&client_channel);
        shuttled = shuttle(&client_channel, &server_channel, &sink);

        expect(queued).toBe(true);
        expect(flushed).toBe(true);
        expect(shuttled).toBe(true);
        expect(captured_size).toBe(sizeof(message));
        expect((const uint8_t *)captured_message).toEqualMemory(message, sizeof(message));
    });

    it("negotiates the suite the stream profile prefers", []() {
        ptls_cipher_suite_t *negotiated = NULL;
        bool client_failed = false;

        startChannelPair();

        pumpUntilEstablished(&client_failed);
        negotiated = ptls_get_cipher(client_channel.tls);

        expect(negotiated->id).toBe(TlsAead_CipherSuites(kTLS_TRANSPORT_STREAM)[0]->id);
    });

    it("exposes the client fingerprint to the server after the handshake", []() {
        char fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE] = "";
        bool client_failed = false;
        bool exposed;

        startChannelPair();
        pumpUntilEstablished(&client_failed);

        exposed = TlsChannel_PeerFingerprint(&server_channel, fingerprint);

        expect(exposed).toBe(true);
        expect(strlen(fingerprint)).toBe((size_t)64);
    });

    it("FingerprintOfFile matches the fingerprint seen on the wire", []() {
        char handshake_fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE] = "";
        char file_fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE] = "";
        bool client_failed = false;

        startChannelPair();
        pumpUntilEstablished(&client_failed);
        TlsChannel_PeerFingerprint(&server_channel, handshake_fingerprint);

        TlsIdentity_FingerprintOfFile(client_certificate, file_fingerprint);

        expect((const char *)file_fingerprint).toBe((const char *)handshake_fingerprint);
    });

    it("holds the first-contact pin back during the handshake", []() {
        char pinned[PIN_STORE_FINGERPRINT_HEX_SIZE];
        bool client_failed = false;
        bool found;

        startChannelPair();

        pumpUntilEstablished(&client_failed);
        found = PinStore_Lookup(PIN_STORE_PATH, PIN_KEY, pinned);

        expect(found).toBe(false);
    });

    it("stores the first-contact pin once the transport commits it", []() {
        char pinned[PIN_STORE_FINGERPRINT_HEX_SIZE];
        bool client_failed = false;
        bool found;

        startChannelPair();
        pumpUntilEstablished(&client_failed);

        TlsClientSecurity_CommitPin(&client_security);
        found = PinStore_Lookup(PIN_STORE_PATH, PIN_KEY, pinned);

        expect(found).toBe(true);
    });

    it("rejects the handshake when the pinned fingerprint differs", []() {
        bool client_failed = false;

        PinStore_Pin(PIN_STORE_PATH, PIN_KEY, WRONG_FINGERPRINT);
        startChannelPair();

        pumpUntilEstablished(&client_failed);

        expect(client_failed).toBe(true);
        expect(TlsChannel_IsEstablished(&client_channel)).toBe(false);
    });

    it("accepts a pinned hub under a new address and records the address", []() {
        char pinned_under_new_address[PIN_STORE_FINGERPRINT_HEX_SIZE];
        char pinned_under_old_address[PIN_STORE_FINGERPRINT_HEX_SIZE];
        bool client_failed = false;

        PinStore_Pin(PIN_STORE_PATH, OTHER_ADDRESS_KEY, hub_fingerprint);
        pin_policy = kPIN_POLICY_ONE_HUB;
        startChannelPair();

        pumpUntilEstablished(&client_failed);
        TlsClientSecurity_CommitPin(&client_security);
        PinStore_Lookup(PIN_STORE_PATH, PIN_KEY, pinned_under_new_address);
        PinStore_Lookup(PIN_STORE_PATH, OTHER_ADDRESS_KEY, pinned_under_old_address);

        expect(TlsChannel_IsEstablished(&client_channel)).toBe(true);
        expect((const char *)pinned_under_new_address).toBe((const char *)hub_fingerprint);
        expect((const char *)pinned_under_old_address).toBe((const char *)hub_fingerprint);
    });

    it("refuses a new hub under a new address when the agent trusts one hub", []() {
        bool client_failed = false;

        PinStore_Pin(PIN_STORE_PATH, OTHER_ADDRESS_KEY, WRONG_FINGERPRINT);
        pin_policy = kPIN_POLICY_ONE_HUB;
        startChannelPair();

        pumpUntilEstablished(&client_failed);

        expect(client_failed).toBe(true);
        expect(TlsChannel_IsEstablished(&client_channel)).toBe(false);
    });

    it("pins a new hub under a new address when any hub may be trusted", []() {
        bool client_failed = false;

        PinStore_Pin(PIN_STORE_PATH, OTHER_ADDRESS_KEY, WRONG_FINGERPRINT);
        startChannelPair();

        pumpUntilEstablished(&client_failed);

        expect(TlsChannel_IsEstablished(&client_channel)).toBe(true);
    });

    it("pins the first hub when the agent trusts one hub", []() {
        bool client_failed = false;

        pin_policy = kPIN_POLICY_ONE_HUB;
        startChannelPair();

        pumpUntilEstablished(&client_failed);

        expect(TlsChannel_IsEstablished(&client_channel)).toBe(true);
    });

    it("accepts the expected fingerprint without writing a pin", []() {
        bool client_failed = false;

        expected_fingerprint = hub_fingerprint;
        startChannelPair();

        pumpUntilEstablished(&client_failed);
        TlsClientSecurity_CommitPin(&client_security);

        expect(TlsChannel_IsEstablished(&client_channel)).toBe(true);
        expect(PinStore_IsEmpty(PIN_STORE_PATH)).toBe(true);
    });

    it("rejects a hub whose fingerprint is not the expected one", []() {
        bool client_failed = false;

        expected_fingerprint = WRONG_FINGERPRINT;
        startChannelPair();

        pumpUntilEstablished(&client_failed);

        expect(client_failed).toBe(true);
        expect(TlsChannel_IsEstablished(&client_channel)).toBe(false);
    });
});

/* ---------- private ---------- */

static bool startChannelPair(void)
{
    TlsClientSecurityConfig config;
    ptls_t *server_session;
    ptls_t *client_session;

    memset(&config, 0, sizeof(config));
    config.certificate_path = client_certificate;
    config.key_path = client_key;
    config.pin_store_path = PIN_STORE_PATH;
    config.pin_key = PIN_KEY;
    config.pin_policy = pin_policy;
    config.pinned_fingerprint = expected_fingerprint;

    if (!TlsServerSecurity_Init(&server_security, hub_certificate, hub_key)) {
        return false;
    }
    if (!TlsClientSecurity_Init(&client_security, &config)) {
        return false;
    }
    if (!TlsServerSecurity_NewSession(&server_security, &server_session)) {
        return false;
    }
    if (!TlsClientSecurity_NewSession(&client_security, "localhost", &client_session)) {
        return false;
    }

    TlsChannel_Bind(&server_channel, server_session, NULL);
    TlsChannel_Bind(&client_channel, client_session, TlsClientSecurity_HandshakeProperties(&client_security));

    return true;
}

static bool pumpUntilEstablished(bool *client_failed)
{
    uint8_t round;

    if (!TlsChannel_Pump(&client_channel)) {
        *client_failed = true;
        return false;
    }

    for(round=0; round<PUMP_ROUNDS_MAX; round++) {
        if (!shuttle(&client_channel, &server_channel, NULL)) {
            return false;
        }
        if (!shuttle(&server_channel, &client_channel, NULL)) {
            *client_failed = true;
            return false;
        }
        if (TlsChannel_IsEstablished(&client_channel) && TlsChannel_IsEstablished(&server_channel)) {
            return true;
        }
    }

    return false;
}

static bool shuttle(TlsChannel *from, TlsChannel *to, const MessageSink *sink)
{
    uint8_t buffer[SHUTTLE_BUFFER_SIZE];
    size_t size = TlsChannel_PendingCiphertext(from);
    size_t offset = 0;
    size_t consumed;

    if (size == 0) {
        return true;
    }
    if (size > sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, TlsChannel_Ciphertext(from), size);
    TlsChannel_ConsumeCiphertext(from, size);

    while (offset < size) {
        if (!TlsChannel_PushCiphertext(to, buffer + offset, size - offset, &consumed, sink)) {
            return false;
        }
        offset += consumed;
        if (consumed == 0) {
            return true;
        }
    }

    return true;
}

static void closeChannelPair(void)
{
    TlsChannel_Close(&server_channel);
    TlsChannel_Close(&client_channel);
}

static void captureMessage(void *context, const uint8_t *message, size_t size)
{
    (void)context;
    memcpy(captured_message, message, size);
    captured_size = size;
}
