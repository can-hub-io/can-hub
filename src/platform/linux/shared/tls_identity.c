#include "platform/linux/shared/tls_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include <gnutls/crypto.h>
#include <gnutls/x509.h>

#define SYSTEM_STATE_DIRECTORY "/var/lib/can-hub"
#define USER_STATE_SUBDIRECTORY "/.local/state/can-hub"
#define STATE_DIRECTORY_MODE 0755
#define PRIVATE_KEY_FILE_MODE 0600
#define CERTIFICATE_FILE_MODE 0644
#define CERTIFICATE_LIFETIME_SECONDS (20L * 365 * 24 * 3600)
#define CERTIFICATE_BACKDATE_SECONDS 86400
#define CERTIFICATE_SERIAL 1
#define PEM_BUFFER_SIZE 8192
#define FINGERPRINT_SIZE 32

static bool directoryUsable(const char *directory);
static bool makeDirectoryPath(const char *directory);
static bool filesExist(const char *first_path, const char *second_path);
static bool generateIdentity(const char *certificate_path, const char *key_path, const char *common_name);
static bool buildSelfSignedCertificate(
    gnutls_x509_crt_t certificate,
    gnutls_x509_privkey_t key,
    const char *common_name
);
static bool exportPrivateKeyPem(gnutls_x509_privkey_t key, const char *path);
static bool exportCertificatePem(gnutls_x509_crt_t certificate, const char *path);
static bool writeFileWithMode(const char *path, const uint8_t *data, size_t size, mode_t mode);

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

bool TlsIdentity_FingerprintOfDer(const gnutls_datum_t *certificate_der, char *fingerprint_hex)
{
    uint8_t fingerprint[FINGERPRINT_SIZE];
    size_t i;

    if (gnutls_hash_fast(GNUTLS_DIG_SHA256, certificate_der->data, certificate_der->size, fingerprint) != 0) {
        return false;
    }

    for(i=0; i<FINGERPRINT_SIZE; i++) {
        snprintf(&fingerprint_hex[i * 2], 3, "%02x", fingerprint[i]);
    }

    return true;
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
    gnutls_x509_privkey_t key = NULL;
    gnutls_x509_crt_t certificate = NULL;
    bool generated = false;

    if (gnutls_x509_privkey_init(&key) != 0) {
        return false;
    }
    if (gnutls_x509_crt_init(&certificate) != 0) {
        gnutls_x509_privkey_deinit(key);
        return false;
    }

    if (buildSelfSignedCertificate(certificate, key, common_name)) {
        generated = exportPrivateKeyPem(key, key_path) && exportCertificatePem(certificate, certificate_path);
    }

    gnutls_x509_crt_deinit(certificate);
    gnutls_x509_privkey_deinit(key);

    return generated;
}

static bool buildSelfSignedCertificate(
    gnutls_x509_crt_t certificate,
    gnutls_x509_privkey_t key,
    const char *common_name
)
{
    uint8_t serial = CERTIFICATE_SERIAL;
    time_t now = time(NULL);
    int32_t set_name_result;

    if (gnutls_x509_privkey_generate(key, GNUTLS_PK_EDDSA_ED25519, 0, 0) != 0) {
        return false;
    }
    if (gnutls_x509_crt_set_version(certificate, 3) != 0) {
        return false;
    }
    if (gnutls_x509_crt_set_serial(certificate, &serial, sizeof(serial)) != 0) {
        return false;
    }
    if (gnutls_x509_crt_set_activation_time(certificate, now - CERTIFICATE_BACKDATE_SECONDS) != 0) {
        return false;
    }
    if (gnutls_x509_crt_set_expiration_time(certificate, now + CERTIFICATE_LIFETIME_SECONDS) != 0) {
        return false;
    }

    set_name_result = gnutls_x509_crt_set_dn_by_oid(
        certificate,
        GNUTLS_OID_X520_COMMON_NAME,
        0,
        common_name,
        strlen(common_name)
    );
    if (set_name_result != 0) {
        return false;
    }
    if (gnutls_x509_crt_set_key(certificate, key) != 0) {
        return false;
    }

    return gnutls_x509_crt_sign2(certificate, certificate, key, GNUTLS_DIG_SHA512, 0) == 0;
}

static bool exportPrivateKeyPem(gnutls_x509_privkey_t key, const char *path)
{
    uint8_t pem[PEM_BUFFER_SIZE];
    size_t pem_size = sizeof(pem);

    if (gnutls_x509_privkey_export_pkcs8(key, GNUTLS_X509_FMT_PEM, NULL, GNUTLS_PKCS_PLAIN, pem, &pem_size) != 0) {
        return false;
    }

    return writeFileWithMode(path, pem, pem_size, PRIVATE_KEY_FILE_MODE);
}

static bool exportCertificatePem(gnutls_x509_crt_t certificate, const char *path)
{
    uint8_t pem[PEM_BUFFER_SIZE];
    size_t pem_size = sizeof(pem);

    if (gnutls_x509_crt_export(certificate, GNUTLS_X509_FMT_PEM, pem, &pem_size) != 0) {
        return false;
    }

    return writeFileWithMode(path, pem, pem_size, CERTIFICATE_FILE_MODE);
}

static bool writeFileWithMode(const char *path, const uint8_t *data, size_t size, mode_t mode)
{
    FILE *file = fopen(path, "w");
    size_t written;

    if (file == NULL) {
        return false;
    }

    written = fwrite(data, 1, size, file);
    fclose(file);
    chmod(path, mode);

    return written == size;
}
