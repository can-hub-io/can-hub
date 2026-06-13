#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Inbound multi-peer contract, defined by the core and implemented by it
 * (see Hub_Events). The platform transport invokes these as peers connect,
 * disconnect and deliver data. Counterpart of HubTransportPort. A local
 * peer arrived over a transport restricted to the hub host (the unix
 * socket); only local peers may claim the admin role.
 */
typedef enum tpeer_transport_e {
    kPEER_TRANSPORT_UNKNOWN = 0,
    kPEER_TRANSPORT_UNIX,
    kPEER_TRANSPORT_TCP,
    kPEER_TRANSPORT_TLS,
    kPEER_TRANSPORT_QUIC,
    kPEER_TRANSPORT_MAX,
} TPEER_TRANSPORT;

typedef struct {
    const char *fingerprint_hex;
    const char *origin;
    uint8_t transport_kind;
    bool local;
} HubPeerConnectInfo;

typedef struct {
    void *context;
    void (*on_peer_connected)(void *context, uint32_t peer_id, const HubPeerConnectInfo *info, uint64_t now_us);
    void (*on_peer_disconnected)(void *context, uint32_t peer_id, uint64_t now_us);
    void (*on_peer_control)(void *context, uint32_t peer_id, const uint8_t *data, size_t size, uint64_t now_us);
    void (*on_peer_frame)(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
    void (*on_peer_writable)(void *context, uint32_t peer_id);
} HubTransportEvents;
