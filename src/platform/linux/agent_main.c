#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/signalfd.h>

#include "agent/agent_app.h"
#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_client_transport.h"
#include "platform/linux/shared/cli_meta.h"
#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/shared/log.h"
#include "platform/linux/shared/tls_identity.h"
#include "platform/linux/socketcan/socketcan_adapter.h"
#include "platform/linux/tcp/tcp_client_transport.h"
#include "platform/linux/tls/tls_client_transport.h"
#include "protocol/error_message.h"
#include "protocol/message_header.h"
#include "protocol/register_message.h"
#include "version.h"

#define MAX_EPOLL_EVENTS 16
#define TICK_PERIOD_MS 100
#define TRANSPORT_SLOT 0
#define IDENTITY_NAME "agent"
#define KNOWN_HUBS_FILE "known_hubs"
#define KNOWN_HUBS_PATH_MAX (TLS_IDENTITY_PATH_MAX + sizeof(KNOWN_HUBS_FILE))
#define PIN_KEY_MAX (CONNECT_URL_HOST_MAX + CONNECT_URL_PORT_TEXT_MAX)

static AgentApp app;
static QuicClientTransport quic_transport;
static TcpClientTransport tcp_transport;
static TlsClientTransport tls_transport;
static SocketCanAdapter can_adapter;
static RegisterMessage registration;
static uint8_t transport_scheme;
static EpollRegistry poll_registry;
static int32_t signal_fd = -1;
static bool should_shutdown;
static const char *state_directory_override;
static uint32_t pace_rate_override;
static char state_directory[TLS_IDENTITY_PATH_MAX];
static char identity_certificate_path[TLS_IDENTITY_PATH_MAX];
static char identity_key_path[TLS_IDENTITY_PATH_MAX];
static char known_hubs_path[KNOWN_HUBS_PATH_MAX];
static char pin_key[PIN_KEY_MAX];
static TransportEvents agent_core_events;
static uint32_t hub_connect_count;
static FrameMessage pending_can_frame[REGISTER_INTERFACES_MAX];
static bool can_interface_paused[REGISTER_INTERFACES_MAX];

static void printUsage(FILE *stream, const char *program);
static bool parseArguments(int argc, char **argv, char *host, char *port_text);
static TransportEvents loggingTransportEvents(void);
static void loggingOnConnected(void *context);
static void loggingOnDisconnected(void *context, uint64_t now_us);
static void loggingOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void logRegistered(const RegisterAckMessage *ack);
static const char *registerStatusText(uint8_t status);
static bool prepareSecurityMaterial(const char *host, const char *port_text);
static bool initTransport(const char *host, const char *port_text, const TransportEvents *transport_events);
static TransportPort *transportPort(void);
static bool setupSignalFd(void);
static bool registerStaticPollFds(void);
static void syncStreamRegistration(void);
static void dispatchEvent(const struct epoll_event *event, const CanEvents *can_events);
static void drainCanInterface(uint8_t interface_index, const CanEvents *can_events);
static void resumePausedCanInterfaces(const CanEvents *can_events);
static int32_t showIdentity(void);

int main(int argc, char **argv)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    char host[CONNECT_URL_HOST_MAX];
    char port_text[CONNECT_URL_PORT_TEXT_MAX];
    TransportEvents transport_events;
    CanEvents can_events;
    int32_t event_count;
    int32_t i;

    signal(SIGPIPE, SIG_IGN);

    if (CliMeta_HandleVersionAndHelp(argc, argv, "can-hub-agent", printUsage)) {
        return 0;
    }

    Log_InitFromArgs("can-hub-agent", argc, argv);

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) {
            state_directory_override = argv[++i];
        }
    }
    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--show-identity") == 0) {
            return showIdentity();
        }
    }

    if (!parseArguments(argc, argv, host, port_text)) {
        printUsage(stderr, argv[0]);
        return 1;
    }

    if (!SocketCanAdapter_Open(&can_adapter, &registration, true)) {
        LOG_ERROR("could not open CAN interfaces");
        return 1;
    }
    SocketCanAdapter_SetPaceRate(&can_adapter, pace_rate_override);

    agent_core_events = AgentApp_TransportEvents(&app);
    transport_events = loggingTransportEvents();
    can_events = AgentApp_CanEvents(&app);
    if (!initTransport(host, port_text, &transport_events)) {
        LOG_ERROR("could not initialize transport");
        return 1;
    }

    AgentApp_Init(&app, transportPort(), SocketCanAdapter_Port(&can_adapter), &registration);

    if (!setupSignalFd() || !EpollRegistry_Open(&poll_registry) || !registerStaticPollFds()) {
        return 1;
    }

    LOG_INFO("%s connecting to %s:%s", Version_String(), host, port_text);

    while (!should_shutdown) {
        syncStreamRegistration();
        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, TICK_PERIOD_MS);

        for(i=0; i<event_count; i++) {
            dispatchEvent(&events[i], &can_events);
        }

        resumePausedCanInterfaces(&can_events);
        AgentApp_Tick(&app, Clock_RealtimeUs());
    }

    transportPort()->disconnect(transportPort()->context);
    LOG_INFO("shutting down");

    return 0;
}

/* ---------- private ---------- */

static void printUsage(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "usage: %s --connect quic://<host>:<port>|tls://<host>:<port>|tcp://<host>:<port>"
        " --name <agent-name> [--state-dir <path>] [--log-level error|warn|info|debug]"
        " [--pace-rate <bps>] <can-if> [...]\n"
        "       %s --show-identity [--state-dir <path>]   print this agent's"
        " TLS fingerprint for the hub allowlist\n",
        program,
        program
    );
}

static TransportEvents loggingTransportEvents(void)
{
    TransportEvents events = agent_core_events;

    events.on_connected = loggingOnConnected;
    events.on_disconnected = loggingOnDisconnected;
    events.on_control = loggingOnControl;

    return events;
}

static void loggingOnConnected(void *context)
{
    hub_connect_count++;
    if (hub_connect_count == 1) {
        LOG_INFO("connected to hub");
    } else {
        LOG_INFO("reconnected to hub (session #%u)", hub_connect_count);
    }

    agent_core_events.on_connected(context);
}

static void loggingOnDisconnected(void *context, uint64_t now_us)
{
    uint32_t delay_seconds;

    agent_core_events.on_disconnected(context, now_us);

    delay_seconds = (AgentApp_PendingReconnectDelayMs(&app) + 999) / 1000;
    LOG_WARN("connection lost, reconnecting in %us", delay_seconds);
}

static void loggingOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    MessageHeader header;
    RegisterAckMessage ack;
    ErrorMessage error;

    if (MessageHeader_Decode(&header, data, size) && size >= (size_t)MESSAGE_HEADER_SIZE + header.length) {
        if (header.type == kMESSAGE_TYPE_REGISTER_ACK
            && RegisterAckMessage_Decode(&ack, data + MESSAGE_HEADER_SIZE, header.length)) {
            if (ack.status == REGISTER_STATUS_OK) {
                logRegistered(&ack);
            } else {
                LOG_ERROR("registration rejected: %s", registerStatusText(ack.status));
            }
        } else if (header.type == kMESSAGE_TYPE_ERROR
                   && ErrorMessage_Decode(&error, data + MESSAGE_HEADER_SIZE, header.length)) {
            LOG_ERROR("hub error: %.*s", (int)ERROR_DETAIL_SIZE, error.detail);
        }
    }

    agent_core_events.on_control(context, data, size, now_us);
}

static void logRegistered(const RegisterAckMessage *ack)
{
    char interfaces[256];
    size_t offset;
    int32_t written;
    uint8_t i;

    offset = 0;
    interfaces[0] = '\0';
    for(i=0; i<ack->interface_count && i<REGISTER_INTERFACES_MAX; i++) {
        written = snprintf(
            interfaces + offset,
            sizeof(interfaces) - offset,
            "%s%s=ch%u",
            (i == 0) ? "" : " ",
            registration.interface_names[i],
            (unsigned)ack->channels[i]
        );
        if (written < 0 || (size_t)written >= sizeof(interfaces) - offset) {
            break;
        }
        offset += (size_t)written;
    }

    LOG_INFO("registered as %s: %s", registration.agent_name, interfaces);
}

static const char *registerStatusText(uint8_t status)
{
    switch (status) {
        case REGISTER_STATUS_REJECTED:
            return "name/interface collision or hub registry full";
        case REGISTER_STATUS_IDENTITY_MISMATCH:
            return "agent name pinned to a different fingerprint";
        case REGISTER_STATUS_UNKNOWN_AGENT:
            return "fingerprint not in the hub allowlist";
        default:
            return "unknown reason";
    }
}

static bool parseArguments(int argc, char **argv, char *host, char *port_text)
{
    const char *connect_url = NULL;
    const char *agent_name = "";
    int32_t i;

    memset(&registration, 0, sizeof(registration));

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            connect_url = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            agent_name = argv[++i];
        } else if (strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) {
            state_directory_override = argv[++i];
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            i++;
        } else if (strcmp(argv[i], "--pace-rate") == 0 && i + 1 < argc) {
            pace_rate_override = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else {
            if (registration.interface_count >= REGISTER_INTERFACES_MAX) {
                return false;
            }
            snprintf(
                registration.interface_names[registration.interface_count],
                REGISTER_INTERFACE_NAME_SIZE,
                "%s",
                argv[i]
            );
            registration.interface_count++;
        }
    }

    if (connect_url == NULL || agent_name[0] == '\0' || registration.interface_count == 0) {
        return false;
    }

    snprintf(registration.agent_name, REGISTER_AGENT_NAME_SIZE, "%s", agent_name);

    return ConnectUrl_Parse(connect_url, &transport_scheme, host, port_text);
}

static bool prepareSecurityMaterial(const char *host, const char *port_text)
{
    if (!TlsIdentity_ResolveStateDirectory(state_directory_override, state_directory)) {
        return false;
    }
    if (!TlsIdentity_LoadOrCreate(state_directory, IDENTITY_NAME, identity_certificate_path, identity_key_path)) {
        return false;
    }

    snprintf(known_hubs_path, sizeof(known_hubs_path), "%s/%s", state_directory, KNOWN_HUBS_FILE);
    snprintf(pin_key, sizeof(pin_key), "%s:%s", host, port_text);

    return true;
}

static bool initTransport(const char *host, const char *port_text, const TransportEvents *transport_events)
{
    QuicClientSecurityConfig quic_security_config;
    TlsClientSecurityConfig tls_security_config;

    if (transport_scheme == kCONNECT_SCHEME_QUIC || transport_scheme == kCONNECT_SCHEME_TLS) {
        if (!prepareSecurityMaterial(host, port_text)) {
            LOG_ERROR("could not load or create TLS identity");
            return false;
        }
    }

    if (transport_scheme == kCONNECT_SCHEME_QUIC) {
        quic_security_config.certificate_path = identity_certificate_path;
        quic_security_config.key_path = identity_key_path;
        quic_security_config.pin_store_path = known_hubs_path;
        quic_security_config.pin_key = pin_key;
        quic_security_config.pinned_fingerprint = NULL;
        return QuicClientTransport_Init(&quic_transport, host, port_text, transport_events, &quic_security_config);
    }
    if (transport_scheme == kCONNECT_SCHEME_TLS) {
        tls_security_config.certificate_path = identity_certificate_path;
        tls_security_config.key_path = identity_key_path;
        tls_security_config.pin_store_path = known_hubs_path;
        tls_security_config.pin_key = pin_key;
        tls_security_config.pinned_fingerprint = NULL;
        return TlsClientTransport_Init(&tls_transport, host, port_text, transport_events, &tls_security_config);
    }

    return TcpClientTransport_Init(&tcp_transport, host, port_text, transport_events);
}

static TransportPort *transportPort(void)
{
    if (transport_scheme == kCONNECT_SCHEME_QUIC) {
        return QuicClientTransport_Port(&quic_transport);
    }
    if (transport_scheme == kCONNECT_SCHEME_TLS) {
        return TlsClientTransport_Port(&tls_transport);
    }

    return TcpClientTransport_Port(&tcp_transport);
}

static bool setupSignalFd(void)
{
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
        return false;
    }

    signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

    return signal_fd >= 0;
}

static bool registerStaticPollFds(void)
{
    int32_t fd;
    uint8_t interface_index;

    if (!EpollRegistry_AddStatic(&poll_registry, signal_fd, (uint32_t)signal_fd)) {
        return false;
    }

    if (transport_scheme == kCONNECT_SCHEME_QUIC) {
        fd = QuicClientTransport_TimerFd(&quic_transport);
        if (!EpollRegistry_AddStatic(&poll_registry, fd, (uint32_t)fd)) {
            return false;
        }
    }

    for(interface_index=0; interface_index<registration.interface_count; interface_index++) {
        fd = SocketCanAdapter_Fd(&can_adapter, interface_index);
        if (!EpollRegistry_AddStatic(&poll_registry, fd, (uint32_t)fd)) {
            return false;
        }
    }

    return true;
}

static void syncStreamRegistration(void)
{
    int32_t current_fd;
    uint32_t wanted_mask = EPOLLIN;

    if (transport_scheme == kCONNECT_SCHEME_QUIC) {
        current_fd = QuicClientTransport_UdpFd(&quic_transport);
    } else if (transport_scheme == kCONNECT_SCHEME_TCP) {
        current_fd = TcpClientTransport_Fd(&tcp_transport);
        if (TcpClientTransport_WantsWritable(&tcp_transport)) {
            wanted_mask |= EPOLLOUT;
        }
    } else if (transport_scheme == kCONNECT_SCHEME_TLS) {
        current_fd = TlsClientTransport_Fd(&tls_transport);
        if (TlsClientTransport_WantsWritable(&tls_transport)) {
            wanted_mask |= EPOLLOUT;
        }
    } else {
        return;
    }

    EpollRegistry_SyncSlot(&poll_registry, TRANSPORT_SLOT, current_fd, wanted_mask, (uint32_t)current_fd);
}

static void dispatchEvent(const struct epoll_event *event, const CanEvents *can_events)
{
    int32_t event_fd = (int32_t)event->data.u32;
    struct signalfd_siginfo signal_info;
    uint8_t interface_index;

    if (event_fd == signal_fd) {
        (void)read(signal_fd, &signal_info, sizeof(signal_info));
        should_shutdown = true;
        return;
    }

    if (transport_scheme == kCONNECT_SCHEME_QUIC) {
        if (event_fd == QuicClientTransport_UdpFd(&quic_transport)) {
            QuicClientTransport_OnUdpReadable(&quic_transport);
            return;
        }
        if (event_fd == QuicClientTransport_TimerFd(&quic_transport)) {
            QuicClientTransport_OnTimer(&quic_transport);
            return;
        }
    } else if (transport_scheme == kCONNECT_SCHEME_TLS && event_fd == TlsClientTransport_Fd(&tls_transport)) {
        if (event->events & EPOLLOUT) {
            TlsClientTransport_OnWritable(&tls_transport);
        }
        if (event->events & EPOLLIN) {
            TlsClientTransport_OnReadable(&tls_transport);
        }
        return;
    } else if (transport_scheme == kCONNECT_SCHEME_TCP && event_fd == TcpClientTransport_Fd(&tcp_transport)) {
        if (event->events & EPOLLOUT) {
            TcpClientTransport_OnWritable(&tcp_transport);
        }
        if (event->events & EPOLLIN) {
            TcpClientTransport_OnReadable(&tcp_transport);
        }
        return;
    }

    for(interface_index=0; interface_index<registration.interface_count; interface_index++) {
        if (event_fd == SocketCanAdapter_Fd(&can_adapter, interface_index)) {
            drainCanInterface(interface_index, can_events);
            return;
        }
    }
}

static void drainCanInterface(uint8_t interface_index, const CanEvents *can_events)
{
    int32_t fd;

    while (SocketCanAdapter_ReadFrame(&can_adapter, interface_index, &pending_can_frame[interface_index])) {
        if (can_events->on_frame(can_events->context, interface_index, &pending_can_frame[interface_index])) {
            continue;
        }
        can_interface_paused[interface_index] = true;
        fd = SocketCanAdapter_Fd(&can_adapter, interface_index);
        EpollRegistry_SetStaticInterest(&poll_registry, fd, 0, (uint32_t)fd);
        return;
    }
}

static void resumePausedCanInterfaces(const CanEvents *can_events)
{
    uint8_t interface_index;
    int32_t fd;

    for(interface_index=0; interface_index<registration.interface_count; interface_index++) {
        if (!can_interface_paused[interface_index]) {
            continue;
        }
        if (!can_events->on_frame(can_events->context, interface_index, &pending_can_frame[interface_index])) {
            continue;
        }
        can_interface_paused[interface_index] = false;
        fd = SocketCanAdapter_Fd(&can_adapter, interface_index);
        EpollRegistry_SetStaticInterest(&poll_registry, fd, EPOLLIN, (uint32_t)fd);
        drainCanInterface(interface_index, can_events);
    }
}

static int32_t showIdentity(void)
{
    char fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];

    if (!prepareSecurityMaterial("identity", "0")) {
        LOG_ERROR("could not load or create TLS identity");
        return 1;
    }
    if (!TlsIdentity_FingerprintOfFile(identity_certificate_path, fingerprint)) {
        LOG_ERROR("could not read the identity fingerprint");
        return 1;
    }

    printf("%s\n", fingerprint);

    return 0;
}
