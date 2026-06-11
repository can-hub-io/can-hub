#include "platform/linux/shared/pinned_server_verifier.h"

#include <stdio.h>
#include <string.h>

#include <openssl/x509.h>

#include "platform/linux/shared/tls_identity.h"

static int verifyPinnedServer(X509_STORE_CTX *store_context, void *argument);
static bool peerFingerprint(X509_STORE_CTX *store_context, char *fingerprint_hex);

/* ---------- public ---------- */

void PinnedServerVerifier_Attach(
    PinnedServerVerifier *self,
    SSL_CTX *context,
    const char *pin_store_path,
    const char *pin_key
)
{
    snprintf(self->pin_store_path, sizeof(self->pin_store_path), "%s", pin_store_path);
    snprintf(self->pin_key, sizeof(self->pin_key), "%s", pin_key);
    SSL_CTX_set_cert_verify_callback(context, verifyPinnedServer, self);
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER, NULL);
}

/* ---------- private ---------- */

static int verifyPinnedServer(X509_STORE_CTX *store_context, void *argument)
{
    PinnedServerVerifier *self = argument;
    char fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];
    char pinned[PIN_STORE_FINGERPRINT_HEX_SIZE];

    if (!peerFingerprint(store_context, fingerprint)) {
        return 0;
    }

    if (!PinStore_Lookup(self->pin_store_path, self->pin_key, pinned)) {
        fprintf(stderr, "pinning hub %s fingerprint %s\n", self->pin_key, fingerprint);
        return PinStore_Append(self->pin_store_path, self->pin_key, fingerprint) ? 1 : 0;
    }
    if (strcmp(pinned, fingerprint) != 0) {
        fprintf(stderr, "hub %s fingerprint changed, rejecting connection\n", self->pin_key);
        return 0;
    }

    return 1;
}

static bool peerFingerprint(X509_STORE_CTX *store_context, char *fingerprint_hex)
{
    X509 *certificate = X509_STORE_CTX_get0_cert(store_context);
    uint8_t *der = NULL;
    int der_size;
    bool computed = false;

    if (certificate == NULL) {
        return false;
    }

    der_size = i2d_X509(certificate, &der);
    if (der_size > 0) {
        computed = TlsIdentity_FingerprintOfDer(der, (size_t)der_size, fingerprint_hex);
        OPENSSL_free(der);
    }

    return computed;
}
