#include "platform/linux/shared/tls_identity.h"

#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#define STATE_SUBDIRECTORY "\\can-hub"
#define CERTIFICATE_LIFETIME_SECONDS (20L * 365 * 24 * 3600)
#define CERTIFICATE_BACKDATE_SECONDS 86400
#define CERTIFICATE_SERIAL 1
#define CERTIFICATE_X509_VERSION_3 2
#define FINGERPRINT_SIZE 32

static bool makeDirectoryPath(const char *directory);
static bool filesExist(const char *first_path, const char *second_path);
static bool generateIdentity(const char *certificate_path, const char *key_path, const char *common_name);
static X509 *buildSelfSignedCertificate(EVP_PKEY *key, const char *common_name);
static bool exportPem(BIO *pem, const char *path);
static bool writePemFile(EVP_PKEY *key, X509 *certificate, const char *key_path, const char *certificate_path);

/* ---------- public ---------- */

bool TlsIdentity_ResolveStateDirectory(const char *override_directory, char *directory)
{
    const char *application_data;

    if (override_directory != NULL) {
        snprintf(directory, TLS_IDENTITY_PATH_MAX, "%s", override_directory);
        return makeDirectoryPath(directory);
    }

    application_data = getenv("LOCALAPPDATA");
    if (application_data == NULL) {
        application_data = getenv("APPDATA");
    }
    if (application_data == NULL) {
        return false;
    }
    snprintf(directory, TLS_IDENTITY_PATH_MAX, "%s%s", application_data, STATE_SUBDIRECTORY);

    return makeDirectoryPath(directory);
}

bool TlsIdentity_LoadOrCreate(
    const char *directory,
    const char *name,
    char *certificate_path,
    char *key_path
)
{
    snprintf(certificate_path, TLS_IDENTITY_PATH_MAX, "%s/%s.crt", directory, name);
    snprintf(key_path, TLS_IDENTITY_PATH_MAX, "%s/%s.key", directory, name);

    if (filesExist(certificate_path, key_path)) {
        return true;
    }

    return generateIdentity(certificate_path, key_path, name);
}

bool TlsIdentity_FingerprintOfDer(const uint8_t *certificate_der, size_t der_size, char *fingerprint_hex)
{
    uint8_t fingerprint[FINGERPRINT_SIZE];
    unsigned int fingerprint_size = 0;
    size_t i;

    if (!EVP_Digest(certificate_der, der_size, fingerprint, &fingerprint_size, EVP_sha256(), NULL)) {
        return false;
    }
    if (fingerprint_size != FINGERPRINT_SIZE) {
        return false;
    }

    for(i=0; i<FINGERPRINT_SIZE; i++) {
        snprintf(&fingerprint_hex[i * 2], 3, "%02x", fingerprint[i]);
    }

    return true;
}

bool TlsIdentity_FingerprintOfFile(const char *certificate_path, char *fingerprint_hex)
{
    FILE *file;
    X509 *certificate;
    uint8_t *der = NULL;
    int der_size;
    bool computed = false;

    file = fopen(certificate_path, "r");
    if (file == NULL) {
        return false;
    }
    certificate = PEM_read_X509(file, NULL, NULL, NULL);
    fclose(file);
    if (certificate == NULL) {
        return false;
    }

    der_size = i2d_X509(certificate, &der);
    if (der_size > 0) {
        computed = TlsIdentity_FingerprintOfDer(der, (size_t)der_size, fingerprint_hex);
        OPENSSL_free(der);
    }
    X509_free(certificate);

    return computed;
}

/* ---------- private ---------- */

static bool makeDirectoryPath(const char *directory)
{
    char partial[TLS_IDENTITY_PATH_MAX];
    char *separator;

    snprintf(partial, sizeof(partial), "%s", directory);
    separator = partial;
    while ((separator = strpbrk(separator + 1, "/\\")) != NULL) {
        *separator = '\0';
        _mkdir(partial);
        *separator = '\\';
    }
    _mkdir(partial);

    return _access(directory, 0) == 0;
}

static bool filesExist(const char *first_path, const char *second_path)
{
    return _access(first_path, 4) == 0 && _access(second_path, 4) == 0;
}

static bool generateIdentity(const char *certificate_path, const char *key_path, const char *common_name)
{
    EVP_PKEY *key;
    X509 *certificate;
    bool generated = false;

    key = EVP_PKEY_Q_keygen(NULL, NULL, "ED25519");
    if (key == NULL) {
        return false;
    }

    certificate = buildSelfSignedCertificate(key, common_name);
    if (certificate != NULL) {
        generated = writePemFile(key, certificate, key_path, certificate_path);
        X509_free(certificate);
    }
    EVP_PKEY_free(key);

    return generated;
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

static bool writePemFile(EVP_PKEY *key, X509 *certificate, const char *key_path, const char *certificate_path)
{
    BIO *key_pem = BIO_new(BIO_s_mem());
    BIO *certificate_pem = BIO_new(BIO_s_mem());
    bool written = false;

    if (key_pem != NULL && certificate_pem != NULL
        && PEM_write_bio_PKCS8PrivateKey(key_pem, key, NULL, NULL, 0, NULL, NULL) == 1
        && PEM_write_bio_X509(certificate_pem, certificate) == 1) {
        written = exportPem(key_pem, key_path) && exportPem(certificate_pem, certificate_path);
    }

    BIO_free(key_pem);
    BIO_free(certificate_pem);

    return written;
}

static bool exportPem(BIO *pem, const char *path)
{
    FILE *file;
    char *data = NULL;
    long size;
    size_t written;

    size = BIO_get_mem_data(pem, &data);
    if (size <= 0) {
        return false;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }

    written = fwrite(data, 1, (size_t)size, file);
    fclose(file);

    return written == (size_t)size;
}
