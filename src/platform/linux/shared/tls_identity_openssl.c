#include "platform/linux/shared/tls_identity_backend.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <string.h>

#define CERTIFICATE_X509_VERSION_3 2
#define CERTIFICATE_SERIAL 1
#define CERTIFICATE_LIFETIME_SECONDS (10L * 365 * 24 * 3600)
#define CERTIFICATE_BACKDATE_SECONDS 3600

static EVP_PKEY *generateEd25519Key(void);
static X509 *buildSelfSignedCertificate(EVP_PKEY *key, const char *common_name);
static bool pemFromBio(BIO *pem, uint8_t *out, size_t out_size, size_t *out_length);

/* ---------- public ---------- */

bool TlsIdentityBackend_Generate(
    const char *common_name,
    uint8_t *certificate_pem,
    size_t certificate_pem_size,
    size_t *certificate_pem_length,
    uint8_t *key_pem,
    size_t key_pem_size,
    size_t *key_pem_length
)
{
    EVP_PKEY *key = generateEd25519Key();
    X509 *certificate;
    BIO *bio;
    bool generated = false;

    if (key == NULL) {
        return false;
    }

    certificate = buildSelfSignedCertificate(key, common_name);
    if (certificate == NULL) {
        EVP_PKEY_free(key);
        return false;
    }

    bio = BIO_new(BIO_s_mem());
    if (bio != NULL) {
        if (PEM_write_bio_PKCS8PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL) == 1
            && pemFromBio(bio, key_pem, key_pem_size, key_pem_length)) {
            BIO_free(bio);
            bio = BIO_new(BIO_s_mem());
            generated = bio != NULL
                && PEM_write_bio_X509(bio, certificate) == 1
                && pemFromBio(bio, certificate_pem, certificate_pem_size, certificate_pem_length);
        }
        BIO_free(bio);
    }

    X509_free(certificate);
    EVP_PKEY_free(key);

    return generated;
}

/* ---------- private ---------- */

static EVP_PKEY *generateEd25519Key(void)
{
    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(NID_ED25519, NULL);
    EVP_PKEY *key = NULL;

    if (context == NULL) {
        return NULL;
    }
    if (EVP_PKEY_keygen_init(context) <= 0 || EVP_PKEY_keygen(context, &key) <= 0) {
        EVP_PKEY_CTX_free(context);
        return NULL;
    }
    EVP_PKEY_CTX_free(context);

    return key;
}

static X509 *buildSelfSignedCertificate(EVP_PKEY *key, const char *common_name)
{
    X509 *certificate = X509_new();
    X509_NAME *name;
    bool built = false;

    if (certificate == NULL) {
        return NULL;
    }

    if (X509_set_version(certificate, CERTIFICATE_X509_VERSION_3) == 1
        && ASN1_INTEGER_set(X509_get_serialNumber(certificate), CERTIFICATE_SERIAL) == 1
        && X509_gmtime_adj(X509_getm_notBefore(certificate), -CERTIFICATE_BACKDATE_SECONDS) != NULL
        && X509_gmtime_adj(X509_getm_notAfter(certificate), CERTIFICATE_LIFETIME_SECONDS) != NULL
        && X509_set_pubkey(certificate, key) == 1) {
        name = X509_get_subject_name(certificate);
        built = X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)common_name, -1, -1, 0) == 1
            && X509_set_issuer_name(certificate, name) == 1
            && X509_sign(certificate, key, NULL) > 0;
    }

    if (!built) {
        X509_free(certificate);
        return NULL;
    }

    return certificate;
}

static bool pemFromBio(BIO *pem, uint8_t *out, size_t out_size, size_t *out_length)
{
    char *data = NULL;
    long size = BIO_get_mem_data(pem, &data);

    if (size <= 0 || (size_t)size > out_size) {
        return false;
    }
    memcpy(out, data, (size_t)size);
    *out_length = (size_t)size;

    return true;
}
