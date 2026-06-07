#include "platform/linux/shared/pinned_server_verifier.h"

#include <stdio.h>
#include <string.h>

#include "platform/linux/shared/tls_identity.h"

#define VERIFIERS_MAX 8

static PinnedServerVerifier *registered_verifiers[VERIFIERS_MAX];

static PinnedServerVerifier *findBySession(gnutls_session_t session);
static int verifyPinnedServer(gnutls_session_t session);

/* ---------- public ---------- */

void PinnedServerVerifier_Attach(
    PinnedServerVerifier *self,
    gnutls_session_t session,
    gnutls_certificate_credentials_t credentials,
    const char *pin_store_path,
    const char *pin_key
)
{
    uint8_t i;

    self->session = session;
    snprintf(self->pin_store_path, sizeof(self->pin_store_path), "%s", pin_store_path);
    snprintf(self->pin_key, sizeof(self->pin_key), "%s", pin_key);
    gnutls_certificate_set_verify_function(credentials, verifyPinnedServer);

    for(i=0; i<VERIFIERS_MAX; i++) {
        if (registered_verifiers[i] == self) {
            return;
        }
    }
    for(i=0; i<VERIFIERS_MAX; i++) {
        if (registered_verifiers[i] == NULL) {
            registered_verifiers[i] = self;
            return;
        }
    }
}

void PinnedServerVerifier_Detach(PinnedServerVerifier *self)
{
    uint8_t i;

    for(i=0; i<VERIFIERS_MAX; i++) {
        if (registered_verifiers[i] == self) {
            registered_verifiers[i] = NULL;
        }
    }
}

/* ---------- private ---------- */

static PinnedServerVerifier *findBySession(gnutls_session_t session)
{
    uint8_t i;

    for(i=0; i<VERIFIERS_MAX; i++) {
        if (registered_verifiers[i] != NULL && registered_verifiers[i]->session == session) {
            return registered_verifiers[i];
        }
    }

    return NULL;
}

static int verifyPinnedServer(gnutls_session_t session)
{
    PinnedServerVerifier *self = findBySession(session);
    const gnutls_datum_t *certificates;
    unsigned int certificate_count;
    char fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];
    char pinned[PIN_STORE_FINGERPRINT_HEX_SIZE];

    if (self == NULL) {
        return GNUTLS_E_CERTIFICATE_ERROR;
    }

    certificates = gnutls_certificate_get_peers(session, &certificate_count);
    if (certificates == NULL || certificate_count == 0) {
        return GNUTLS_E_CERTIFICATE_ERROR;
    }
    if (!TlsIdentity_FingerprintOfDer(&certificates[0], fingerprint)) {
        return GNUTLS_E_CERTIFICATE_ERROR;
    }

    if (!PinStore_Lookup(self->pin_store_path, self->pin_key, pinned)) {
        fprintf(stderr, "pinning hub %s fingerprint %s\n", self->pin_key, fingerprint);
        return PinStore_Append(self->pin_store_path, self->pin_key, fingerprint) ? 0 : GNUTLS_E_CERTIFICATE_ERROR;
    }
    if (strcmp(pinned, fingerprint) != 0) {
        fprintf(stderr, "hub %s fingerprint changed, rejecting connection\n", self->pin_key);
        return GNUTLS_E_CERTIFICATE_ERROR;
    }

    return 0;
}
