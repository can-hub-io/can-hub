#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/stat.h>

#include "hub/hub_app.h"
#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_server_transport.h"
#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/shared/hub_defaults.h"
#include "platform/linux/shared/tls_identity.h"
#include "platform/linux/sqlite/identity_database.h"
#include "platform/linux/tcp/tcp_server_transport.h"
#include "platform/linux/tls/tls_server_transport.h"
#include "version.h"

#define MAX_EPOLL_EVENTS 32
#define POLL_PERIOD_MS 100
#define DEFAULT_UNIX_SOCKET_DIRECTORY_MODE 0755
#define IDENTITY_NAME "hub"
#define DATABASE_FILE "hub.db"
#define KNOWN_AGENTS_FILE "known_agents"
#define STATE_FILE_PATH_MAX (TLS_IDENTITY_PATH_MAX + 32)
#define TCP_PEER_ID_BASE 0x00000001
#define UNIX_PEER_ID_BASE 0x40000001
#define QUIC_PEER_ID_BASE 0x80000001
#define TLS_PEER_ID_BASE 0xC0000001
#define UNIX_SLOT_OFFSET TCP_SERVER_PEERS_MAX
#define TLS_SLOT_OFFSET (2 * TCP_SERVER_PEERS_MAX)

typedef enum thub_event_tag_e {
    kHUB_EVENT_TAG_TCP_LISTENER = 0x100,
    kHUB_EVENT_TAG_UNIX_LISTENER,
    kHUB_EVENT_TAG_QUIC_UDP,
    kHUB_EVENT_TAG_QUIC_TIMER,
    kHUB_EVENT_TAG_TLS_LISTENER,
    kHUB_EVENT_TAG_MAX,
} THUB_EVENT_TAG;

static HubApp app;
static TcpServerTransport tcp_transport;
static TcpServerTransport unix_transport;
static QuicServerTransport quic_transport;
static TlsServerTransport tls_transport;
static HubTransportPort mux_port;
static bool tcp_enabled;
static bool unix_enabled;
static bool quic_enabled;
static bool tls_enabled;
static EpollRegistry poll_registry;
static char state_directory[TLS_IDENTITY_PATH_MAX];
static char identity_certificate_path[TLS_IDENTITY_PATH_MAX];
static char identity_key_path[TLS_IDENTITY_PATH_MAX];
static IdentityDatabase identity_database;

static bool parseArguments(int argc, char **argv);
static bool loadIdentity(const char *state_directory_override);
static IdentityStorePort *identityStore(void);
static void importLegacyPinFile(void);
static bool startListeners(
    const char *tcp_port,
    const char *quic_port,
    const char *tls_port,
    const char *unix_path,
    const char *certificate,
    const char *key,
    bool explicit_listen
);
static bool muxSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static bool muxSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void muxClosePeer(void *context, uint32_t peer_id);
static HubTransportPort *portForPeer(uint32_t peer_id);
static bool registerStaticPollFds(void);
static void syncStreamSlotRegistrations(TcpServerTransport *transport, uint8_t slot_offset);
static void syncTlsSlotRegistrations(void);
static void dispatchEvent(const struct epoll_event *event);
static void dispatchStreamSlotEvent(const struct epoll_event *event);

int main(int argc, char **argv)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int32_t event_count;
    int32_t i;

    if (!parseArguments(argc, argv)) {
        fprintf(
            stderr,
            "usage: %s [--listen tls://<port>] [--listen quic://<port>] [--listen tcp://<port>]\n"
            "       [--listen unix://<path>] [--cert <pem> --key <pem>] [--state-dir <path>]\n"
            "       defaults: tls://" HUB_DEFAULT_PORT_TEXT ", quic://" HUB_DEFAULT_PORT_TEXT
            ", tcp://" HUB_DEFAULT_PLAIN_TCP_PORT_TEXT ", unix://" HUB_DEFAULT_UNIX_SOCKET_PATH
            "; TLS identity auto-generated\n",
            argv[0]
        );
        return 1;
    }

    if (!EpollRegistry_Open(&poll_registry) || !registerStaticPollFds()) {
        return 1;
    }

    fprintf(
        stderr,
        "can-hub %s ready (tls:%s quic:%s tcp:%s unix:%s)\n",
        Version_String(),
        tls_enabled ? "on" : "off",
        quic_enabled ? "on" : "off",
        tcp_enabled ? "on" : "off",
        unix_enabled ? "on" : "off"
    );

    for (;;) {
        if (tcp_enabled) {
            syncStreamSlotRegistrations(&tcp_transport, 0);
        }
        if (unix_enabled) {
            syncStreamSlotRegistrations(&unix_transport, UNIX_SLOT_OFFSET);
        }
        if (tls_enabled) {
            syncTlsSlotRegistrations();
        }
        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);

        for(i=0; i<event_count; i++) {
            dispatchEvent(&events[i]);
        }

        HubApp_Tick(&app, Clock_RealtimeUs());
    }
}

/* ---------- private ---------- */

static bool parseArguments(int argc, char **argv)
{
    const char *tcp_port = NULL;
    const char *quic_port = NULL;
    const char *tls_port = NULL;
    const char *unix_path = NULL;
    const char *certificate = NULL;
    const char *key = NULL;
    const char *state_directory_override = NULL;
    const char *remainder;
    bool explicit_listen = false;
    bool explicit_quic = false;
    bool explicit_tls = false;
    uint8_t scheme;
    int32_t i;

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            if (!ConnectUrl_ParseScheme(argv[++i], &scheme, &remainder)) {
                return false;
            }
            if (scheme == kCONNECT_SCHEME_TCP) {
                tcp_port = remainder;
                explicit_listen = true;
            } else if (scheme == kCONNECT_SCHEME_QUIC) {
                quic_port = remainder;
                explicit_listen = true;
                explicit_quic = true;
            } else if (scheme == kCONNECT_SCHEME_TLS) {
                tls_port = remainder;
                explicit_listen = true;
                explicit_tls = true;
            } else {
                unix_path = remainder;
            }
        } else if (strcmp(argv[i], "--cert") == 0 && i + 1 < argc) {
            certificate = argv[++i];
        } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            key = argv[++i];
        } else if (strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) {
            state_directory_override = argv[++i];
        }
    }

    if (!explicit_listen) {
        tcp_port = HUB_DEFAULT_PLAIN_TCP_PORT_TEXT;
        quic_port = HUB_DEFAULT_PORT_TEXT;
        tls_port = HUB_DEFAULT_PORT_TEXT;
    }

    if ((quic_port != NULL || tls_port != NULL) && (certificate == NULL || key == NULL)) {
        if (loadIdentity(state_directory_override)) {
            certificate = identity_certificate_path;
            key = identity_key_path;
        } else {
            fprintf(stderr, "TLS listeners disabled: could not load or create TLS identity\n");
            if (explicit_quic || explicit_tls) {
                return false;
            }
            quic_port = NULL;
            tls_port = NULL;
        }
    }

    return startListeners(tcp_port, quic_port, tls_port, unix_path, certificate, key, explicit_listen);
}

static bool loadIdentity(const char *state_directory_override)
{
    if (!TlsIdentity_ResolveStateDirectory(state_directory_override, state_directory)) {
        return false;
    }

    return TlsIdentity_LoadOrCreate(state_directory, IDENTITY_NAME, identity_certificate_path, identity_key_path);
}

static bool startListeners(
    const char *tcp_port,
    const char *quic_port,
    const char *tls_port,
    const char *unix_path,
    const char *certificate,
    const char *key,
    bool explicit_listen
)
{
    HubTransportEvents transport_events = HubApp_Events(&app);
    bool explicit_unix = unix_path != NULL;

    mux_port.context = NULL;
    mux_port.send_control = muxSendControl;
    mux_port.send_frame = muxSendFrame;
    mux_port.close_peer = muxClosePeer;

    if (tcp_port != NULL) {
        if (TcpServerTransport_Init(&tcp_transport, tcp_port, TCP_PEER_ID_BASE, &transport_events)) {
            tcp_enabled = true;
        } else {
            fprintf(stderr, "could not open TCP listener on port %s\n", tcp_port);
            if (explicit_listen) {
                return false;
            }
        }
    }
    if (quic_port != NULL) {
        bool quic_started = QuicServerTransport_Init(
            &quic_transport,
            quic_port,
            certificate,
            key,
            QUIC_PEER_ID_BASE,
            &transport_events
        );
        if (quic_started) {
            quic_enabled = true;
        } else {
            fprintf(stderr, "could not open QUIC listener on port %s\n", quic_port);
            if (explicit_listen) {
                return false;
            }
        }
    }
    if (tls_port != NULL) {
        bool tls_started = TlsServerTransport_Init(
            &tls_transport,
            tls_port,
            certificate,
            key,
            TLS_PEER_ID_BASE,
            &transport_events
        );
        if (tls_started) {
            tls_enabled = true;
        } else {
            fprintf(stderr, "could not open TLS listener on port %s\n", tls_port);
            if (explicit_listen) {
                return false;
            }
        }
    }

    if (unix_path == NULL) {
        unix_path = HUB_DEFAULT_UNIX_SOCKET_PATH;
        mkdir(HUB_DEFAULT_UNIX_SOCKET_DIRECTORY, DEFAULT_UNIX_SOCKET_DIRECTORY_MODE);
    }
    if (TcpServerTransport_InitUnix(&unix_transport, unix_path, UNIX_PEER_ID_BASE, &transport_events)) {
        unix_enabled = true;
    } else {
        fprintf(stderr, "could not open unix socket listener on %s\n", unix_path);
        if (explicit_unix) {
            return false;
        }
    }

    if (!tcp_enabled && !quic_enabled && !tls_enabled && !unix_enabled) {
        return false;
    }

    HubApp_Init(&app, &mux_port, identityStore());

    return true;
}

static IdentityStorePort *identityStore(void)
{
    char database_path[STATE_FILE_PATH_MAX];

    if (state_directory[0] == '\0') {
        return NULL;
    }

    snprintf(database_path, sizeof(database_path), "%s/%s", state_directory, DATABASE_FILE);
    if (!IdentityDatabase_Open(&identity_database, database_path)) {
        fprintf(stderr, "agent identity pinning disabled: could not open %s\n", database_path);
        return NULL;
    }

    importLegacyPinFile();

    return IdentityDatabase_Port(&identity_database);
}

static void importLegacyPinFile(void)
{
    char pin_file_path[STATE_FILE_PATH_MAX];
    char imported_path[STATE_FILE_PATH_MAX];

    snprintf(pin_file_path, sizeof(pin_file_path), "%s/%s", state_directory, KNOWN_AGENTS_FILE);
    if (access(pin_file_path, R_OK) != 0) {
        return;
    }

    if (!IdentityDatabase_ImportPinFile(&identity_database, pin_file_path)) {
        fprintf(stderr, "could not import %s into the identity database\n", pin_file_path);
        return;
    }

    snprintf(imported_path, sizeof(imported_path), "%s/%s.imported", state_directory, KNOWN_AGENTS_FILE);
    rename(pin_file_path, imported_path);
    fprintf(stderr, "imported %s into the identity database\n", pin_file_path);
}

static bool muxSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    HubTransportPort *destination = portForPeer(peer_id);

    (void)context;

    return destination->send_control(destination->context, peer_id, data, size);
}

static bool muxSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    HubTransportPort *destination = portForPeer(peer_id);

    (void)context;

    return destination->send_frame(destination->context, peer_id, data, size);
}

static void muxClosePeer(void *context, uint32_t peer_id)
{
    HubTransportPort *destination = portForPeer(peer_id);

    (void)context;

    destination->close_peer(destination->context, peer_id);
}

static HubTransportPort *portForPeer(uint32_t peer_id)
{
    if (peer_id >= TLS_PEER_ID_BASE) {
        return TlsServerTransport_Port(&tls_transport);
    }
    if (peer_id >= QUIC_PEER_ID_BASE) {
        return QuicServerTransport_Port(&quic_transport);
    }
    if (peer_id >= UNIX_PEER_ID_BASE) {
        return TcpServerTransport_Port(&unix_transport);
    }

    return TcpServerTransport_Port(&tcp_transport);
}

static bool registerStaticPollFds(void)
{
    if (tcp_enabled) {
        int32_t listen_fd = TcpServerTransport_ListenFd(&tcp_transport);
        if (!EpollRegistry_AddStatic(&poll_registry, listen_fd, kHUB_EVENT_TAG_TCP_LISTENER)) {
            return false;
        }
    }
    if (unix_enabled) {
        int32_t listen_fd = TcpServerTransport_ListenFd(&unix_transport);
        if (!EpollRegistry_AddStatic(&poll_registry, listen_fd, kHUB_EVENT_TAG_UNIX_LISTENER)) {
            return false;
        }
    }
    if (quic_enabled) {
        int32_t udp_fd = QuicServerTransport_UdpFd(&quic_transport);
        int32_t timer_fd = QuicServerTransport_TimerFd(&quic_transport);
        if (!EpollRegistry_AddStatic(&poll_registry, udp_fd, kHUB_EVENT_TAG_QUIC_UDP)) {
            return false;
        }
        if (!EpollRegistry_AddStatic(&poll_registry, timer_fd, kHUB_EVENT_TAG_QUIC_TIMER)) {
            return false;
        }
    }
    if (tls_enabled) {
        int32_t listen_fd = TlsServerTransport_ListenFd(&tls_transport);
        if (!EpollRegistry_AddStatic(&poll_registry, listen_fd, kHUB_EVENT_TAG_TLS_LISTENER)) {
            return false;
        }
    }

    return true;
}

static void syncStreamSlotRegistrations(TcpServerTransport *transport, uint8_t slot_offset)
{
    int32_t current_fd;
    uint32_t wanted_mask;
    uint8_t slot;

    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        current_fd = TcpServerTransport_SlotFd(transport, slot);
        wanted_mask = EPOLLIN;
        if (TcpServerTransport_SlotWantsWritable(transport, slot)) {
            wanted_mask |= EPOLLOUT;
        }

        EpollRegistry_SyncSlot(&poll_registry, slot_offset + slot, current_fd, wanted_mask, slot_offset + slot);
    }
}

static void syncTlsSlotRegistrations(void)
{
    int32_t current_fd;
    uint32_t wanted_mask;
    uint8_t slot;

    for(slot=0; slot<TLS_SERVER_PEERS_MAX; slot++) {
        current_fd = TlsServerTransport_SlotFd(&tls_transport, slot);
        wanted_mask = EPOLLIN;
        if (TlsServerTransport_SlotWantsWritable(&tls_transport, slot)) {
            wanted_mask |= EPOLLOUT;
        }

        EpollRegistry_SyncSlot(&poll_registry, TLS_SLOT_OFFSET + slot, current_fd, wanted_mask, TLS_SLOT_OFFSET + slot);
    }
}

static void dispatchEvent(const struct epoll_event *event)
{
    if (event->data.u32 == kHUB_EVENT_TAG_TCP_LISTENER) {
        TcpServerTransport_OnAcceptReady(&tcp_transport);
        return;
    }
    if (event->data.u32 == kHUB_EVENT_TAG_UNIX_LISTENER) {
        TcpServerTransport_OnAcceptReady(&unix_transport);
        return;
    }
    if (event->data.u32 == kHUB_EVENT_TAG_QUIC_UDP) {
        QuicServerTransport_OnUdpReadable(&quic_transport);
        return;
    }
    if (event->data.u32 == kHUB_EVENT_TAG_QUIC_TIMER) {
        QuicServerTransport_OnTimer(&quic_transport);
        return;
    }
    if (event->data.u32 == kHUB_EVENT_TAG_TLS_LISTENER) {
        TlsServerTransport_OnAcceptReady(&tls_transport);
        return;
    }

    dispatchStreamSlotEvent(event);
}

static void dispatchStreamSlotEvent(const struct epoll_event *event)
{
    TcpServerTransport *transport = &tcp_transport;
    uint8_t slot = (uint8_t)event->data.u32;

    if (slot >= TLS_SLOT_OFFSET) {
        slot -= TLS_SLOT_OFFSET;
        if (event->events & EPOLLOUT) {
            TlsServerTransport_OnSlotWritable(&tls_transport, slot);
        }
        if (event->events & EPOLLIN) {
            TlsServerTransport_OnSlotReadable(&tls_transport, slot);
        }
        return;
    }

    if (slot >= UNIX_SLOT_OFFSET) {
        transport = &unix_transport;
        slot -= UNIX_SLOT_OFFSET;
    }

    if (event->events & EPOLLOUT) {
        TcpServerTransport_OnSlotWritable(transport, slot);
    }
    if (event->events & EPOLLIN) {
        TcpServerTransport_OnSlotReadable(transport, slot);
    }
}
