#pragma once

#include <stdbool.h>

#include <openssl/ssl.h>

/*
 * The TLS profile every can-hub transport speaks: TLS 1.3 only, the three
 * AEAD ciphersuites, ALPN canhub/0. Certificate verification stays with the
 * caller: clients attach the TOFU verifier, servers accept any client
 * certificate and pin its fingerprint at the application layer.
 */

SSL_CTX *TlsDefaults_NewContext(const SSL_METHOD *method);
bool TlsDefaults_LoadIdentity(SSL_CTX *context, const char *certificate_path, const char *key_path);
void TlsDefaults_ConfigureServerContext(SSL_CTX *context);
void TlsDefaults_ConfigureClientSession(SSL *ssl, const char *server_host);
void TlsDefaults_ConfigureServerSession(SSL *ssl);
