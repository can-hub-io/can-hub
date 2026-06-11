#include "platform/linux/shared/tls_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#define SYSTEM_STATE_DIRECTORY "/var/lib/can-hub"
#define USER_STATE_SUBDIRECTORY "/.local/state/can-hub"
#define STATE_DIRECTORY_MODE 0755
#define PRIVATE_KEY_FILE_MODE 0600
#define CERTIFICATE_FILE_MODE 0644
#define CERTIFICATE_LIFETIME_SECONDS (20L * 365 * 24 * 3600)
#define CERTIFICATE_BACKDATE_SECONDS 86400
#define CERTIFICATE_SERIAL 1
#define CERTIFICATE_X509_VERSION_3 2
#define FINGERPRINT_SIZE 32

static bool directoryUsable(const char *directory);
static bool makeDirectoryPath(const char *directory);
static bool filesExist(const char *first_path, const char *second_path);
static bool generateIdentity(const char *certificate_path, const char *key_path, const char *common_name);
static X509 *buildSelfSignedCertificate(EVP_PKEY *key, const char *common_name);
static bool exportPrivateKeyPem(EVP_PKEY *key, const char *path);
static bool exportCertificatePem(X509 *certificate, const char *path);
static bool writePemWithMode(const char *path, BIO *pem, mode_t mode);

/* ---------- public ---------- */

bool TlsIdentity_ResolveStateDirectory(const char *override_directory, char *directory)
{
    const char *home;

    if (override_directory != NULL) {
        snprintf(directory, TLS_IDENTITY_PATH_MAX, "%s", override_directory);
        return makeDirectoryPath(directory);
    }

    snprintf(directory, TLS_IDENTITY_PATH_MAX, "%s", SYSTEM_STATE_DIRECTORY);
    if (makeDirectoryPath(directory) && directoryUsable(directory)) {
        return true;
    }

    home = getenv("HOME");
    if (home == NULL) {
        return false;
    }
    snprintf(directory, TLS_IDENTITY_PATH_MAX, "%s%s", home, USER_STATE_SUBDIRECTORY);

    return makeDirectoryPath(directory) && directoryUsable(directory);
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

static bool directoryUsable(const char *directory)
{
    return access(directory, W_OK | X_OK) == 0;
}

static bool makeDirectoryPath(const char *directory)
{
    char partial[TLS_IDENTITY_PATH_MAX];
    char *separator;

    snprintf(partial, sizeof(partial), "%s", directory);
    separator = partial;
    while ((separator = strchr(separator + 1, '/')) != NULL) {
        *separator = '\0';
        mkdir(partial, STATE_DIRECTORY_MODE);
        *separator = '/';
    }
    mkdir(partial, STATE_DIRECTORY_MODE);

    return access(directory, F_OK) == 0;
}

static bool filesExist(const char *first_path, const char *second_path)
{
    return access(first_path, R_OK) == 0 && access(second_path, R_OK) == 0;
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
        generated = exportPrivateKeyPem(key, key_path) && exportCertificatePem(certificate, certificate_path);
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

static bool exportPrivateKeyPem(EVP_PKEY *key, const char *path)
{
    BIO *pem = BIO_new(BIO_s_mem());
    bool written = false;

    if (pem == NULL) {
        return false;
    }

    if (PEM_write_bio_PKCS8PrivateKey(pem, key, NULL, NULL, 0, NULL, NULL) == 1) {
        written = writePemWithMode(path, pem, PRIVATE_KEY_FILE_MODE);
    }
    BIO_free(pem);

    return written;
}

static bool exportCertificatePem(X509 *certificate, const char *path)
{
    BIO *pem = BIO_new(BIO_s_mem());
    bool written = false;

    if (pem == NULL) {
        return false;
    }

    if (PEM_write_bio_X509(pem, certificate) == 1) {
        written = writePemWithMode(path, pem, CERTIFICATE_FILE_MODE);
    }
    BIO_free(pem);

    return written;
}

static bool writePemWithMode(const char *path, BIO *pem, mode_t mode)
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
    chmod(path, mode);

    return written == (size_t)size;
}
