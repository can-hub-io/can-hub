#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>

#include <unistd.h>

#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_client_transport.h"
#include "platform/linux/shared/cli_meta.h"
#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/shared/hub_defaults.h"
#include "platform/linux/shared/log.h"
#include "platform/linux/shared/tls_identity.h"
#include "platform/linux/socketcan/socketcan_adapter.h"
#include "platform/linux/socketcand/socketcand_server.h"
#include "platform/linux/tcp/tcp_client_transport.h"
#include "platform/linux/tls/tls_client_transport.h"
#include "client/client.h"
#include "mirror/mirror_app.h"
#include "protocol/error_message.h"
#include "protocol/frame_message.h"
#include "protocol/interface_name.h"
#include "protocol/list_message.h"
#include "protocol/open_message.h"
#include "protocol/register_message.h"
#include "protocol/subscribe_message.h"
#include "socketcand/socketcand_app.h"

#define MAX_EPOLL_EVENTS 8
#define POLL_PERIOD_MS 100
#define TCP_SLOT 0
#define IDENTITY_NAME "client"
#define KNOWN_HUBS_FILE "known_hubs"
#define KNOWN_HUBS_PATH_MAX (TLS_IDENTITY_PATH_MAX + sizeof(KNOWN_HUBS_FILE))
#define PIN_KEY_MAX (CONNECT_URL_HOST_MAX + CONNECT_URL_PORT_TEXT_MAX)

#define SOCKETCAND_DEFAULT_PORT_TEXT "29536"
#define SOCKETCAND_BEACON_PORT 42000
#define SOCKETCAND_HUB_STREAM_SLOT 0
#define SC_TAG_HUB_STREAM 0xE0000000u
#define SC_TAG_HUB_QUIC_UDP 0xE0000001u
#define SC_TAG_HUB_QUIC_TIMER 0xE0000002u
#define SC_TAG_SC_LISTEN 0xE0000003u
#define SC_TAG_SC_SLOT_BASE 0xE0000010u
#define ATTACH_TAG_CAN 0xE0000020u

typedef enum tclient_command_e {
    kCLIENT_COMMAND_LIST = 0,
    kCLIENT_COMMAND_DUMP,
    kCLIENT_COMMAND_SEND,
    kCLIENT_COMMAND_SOCKETCAND,
    kCLIENT_COMMAND_ATTACH,
    kCLIENT_COMMAND_MAX,
} TCLIENT_COMMAND;

static TcpClientTransport transport;
static TlsClientTransport tls_transport;
static QuicClientTransport quic_transport;
static TransportPort *active_port;
static Client client;
static uint8_t command;
static uint8_t connect_scheme;
static uint8_t open_flags;
static bool complete_mode;
static uint32_t target_interface_id;
static char target_interface_text[INTERFACE_NAME_NAMESPACED_SIZE];
static bool target_is_namespaced;
static FrameMessage frame_to_send;
static CanFilter dump_filters[SUBSCRIBE_FILTERS_MAX];
static uint8_t dump_filter_count;
static int32_t exit_code = -1;
static const char *state_directory_override;
static const char *client_name = "";
static char state_directory[TLS_IDENTITY_PATH_MAX];
static char identity_certificate_path[TLS_IDENTITY_PATH_MAX];
static char identity_key_path[TLS_IDENTITY_PATH_MAX];
static char known_hubs_path[KNOWN_HUBS_PATH_MAX];
static char pin_key[PIN_KEY_MAX];
static char socketcand_bind_address[CONNECT_URL_HOST_MAX] = HUB_LOCAL_ADDRESS;
static char socketcand_port_text[CONNECT_URL_PORT_TEXT_MAX] = SOCKETCAND_DEFAULT_PORT_TEXT;
static bool beacon_enabled = true;
static SocketcandApp socketcand_app;
static SocketcandServer socketcand_server;
static char attach_interface_name[REGISTER_INTERFACE_NAME_SIZE];
static MirrorApp mirror_app;
static SocketCanAdapter mirror_can_adapter;

static void printUsage(FILE *stream, const char *program);
static bool parseArguments(int argc, char **argv, char *host, char *port_text);
static bool parseSocketcandListen(const char *value);
static int32_t runSocketcandServer(const char *host, const char *port_text);
static int32_t runSocketcandLoop(void);
static int32_t runAttach(const char *host, const char *port_text);
static int32_t runAttachLoop(void);
static void drainCanFrames(void);
static void dispatchAttachEvent(const struct epoll_event *event);
static void syncHubStream(EpollRegistry *poll_registry);
static uint32_t socketcandSlotMask(uint8_t slot);
static void dispatchSocketcandEvent(const struct epoll_event *event);
static void handleSocketcandSlotEvent(uint8_t slot, const struct epoll_event *event);
static void handleHubStreamEvent(const struct epoll_event *event);
static void resolveDeviceName(char *out, size_t out_size);
static void buildBeaconUrl(char *out, size_t out_size);
static bool parseFrameSpec(const char *text, FrameMessage *frame);
static bool parseFilterSpec(const char *text, CanFilter *filter);
static bool parseHexPayload(const char *text, FrameMessage *frame);
static void sendFrameAndQuit(void);
static void startClientCommand(void);
static bool initTransport(const char *host, const char *port_text, const TransportEvents *events);
static bool prepareSecurityMaterial(const char *host, const char *port_text);
static int32_t showIdentity(void);
static bool initTlsTransport(const char *host, const char *port_text, const TransportEvents *events);
static bool initQuicTransport(const char *host, const char *port_text, const TransportEvents *events);
static int32_t runQuicEventLoop(void);
static void clientOnListReply(void *context, const ListReplyMessage *reply);
static void clientOnOpenResult(void *context, uint8_t status, uint8_t channel, uint32_t interface_id);
static void clientOnFrame(void *context, const FrameMessage *frame);
static void clientOnError(void *context, uint16_t code, const char *detail);
static void clientOnDisconnected(void *context);
static void printListReply(const ListReplyMessage *reply);
static void printFrame(const FrameMessage *frame);
static int32_t runEventLoop(void);

int main(int argc, char **argv)
{
    char host[CONNECT_URL_HOST_MAX];
    char port_text[CONNECT_URL_PORT_TEXT_MAX];
    int32_t i;
    TransportEvents events = Client_TransportEvents(&client);
    ClientEvents client_events = {
        .context = NULL,
        .on_list_reply = clientOnListReply,
        .on_open_result = clientOnOpenResult,
        .on_frame = clientOnFrame,
        .on_error = clientOnError,
        .on_disconnected = clientOnDisconnected,
    };

    signal(SIGPIPE, SIG_IGN);

    if (CliMeta_HandleVersionAndHelp(argc, argv, "can-hub-client", printUsage)) {
        return 0;
    }

    Log_InitFromArgs("can-hub-client", argc, argv);

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) {
            state_directory_override = argv[i + 1];
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

    if (command == kCLIENT_COMMAND_SOCKETCAND) {
        return runSocketcandServer(host, port_text);
    }
    if (command == kCLIENT_COMMAND_ATTACH) {
        return runAttach(host, port_text);
    }

    if (!initTransport(host, port_text, &events)) {
        LOG_ERROR("could not initialize transport");
        return 1;
    }
    Client_Init(&client, active_port, &client_events);
    Client_SetName(&client, client_name);
    startClientCommand();
    if (!active_port->connect(active_port->context)) {
        LOG_ERROR("could not connect to %s", host);
        return 1;
    }

    return runEventLoop();
}

/* ---------- private ---------- */

static void printUsage(FILE *stream, const char *program)
{
    fprintf(stream, "usage: %s [--connect quic://<host>:<port>|tls://<host>:<port>|tcp://<host>:<port>|unix://<path>]\n", program);
    fprintf(stream, "       [--state-dir <path>] [--name <label>] list | dump [--no-echo] [--reliable] <interface> [<id>[:<mask>] ...]\n");
    fprintf(stream, "       | send <interface> <can-id>#<hex-payload>   (cansend syntax, e.g. 123#DEADBEEF)\n");
    fprintf(stream, "       | socketcand [--listen [<bind-ip>:]<port>] [--no-beacon]\n");
    fprintf(stream, "                                           local socketcand server (default 127.0.0.1:" SOCKETCAND_DEFAULT_PORT_TEXT ")\n");
    fprintf(stream, "       | attach <interface> <vcan>   mirror a remote bus into a local pre-created vcan (bidirectional)\n");
    fprintf(stream, "       <interface> is the numeric id (from list) or the namespaced name agent/iface\n");
    fprintf(stream, "       %s --show-identity [--state-dir <path>]   print this client's TLS fingerprint\n", program);
    fprintf(stream, "       [--log-level error|warn|info|debug]\n");
    fprintf(stream, "       default: --connect unix://" HUB_DEFAULT_UNIX_SOCKET_PATH "\n");
}

static bool parseArguments(int argc, char **argv, char *host, char *port_text)
{
    const char *connect_url = NULL;
    const char *command_name = NULL;
    int32_t i;

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            connect_url = argv[++i];
        } else if (strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) {
            state_directory_override = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            client_name = argv[++i];
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            i++;
        } else if (strcmp(argv[i], "--no-echo") == 0) {
            open_flags |= OPEN_FLAG_SUPPRESS_OWN_ECHO;
        } else if (strcmp(argv[i], "--reliable") == 0) {
            open_flags |= OPEN_FLAG_RELIABLE;
        } else if (strcmp(argv[i], "--no-beacon") == 0) {
            beacon_enabled = false;
        } else if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            if (!parseSocketcandListen(argv[++i])) {
                return false;
            }
        } else if (strcmp(argv[i], "--complete") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "interfaces") != 0) {
                return false;
            }
            command_name = "list";
            command = kCLIENT_COMMAND_LIST;
            complete_mode = true;
        } else if (command_name == NULL) {
            command_name = argv[i];
            if (strcmp(command_name, "list") == 0) {
                command = kCLIENT_COMMAND_LIST;
            } else if (strcmp(command_name, "socketcand") == 0) {
                command = kCLIENT_COMMAND_SOCKETCAND;
            } else if (strcmp(command_name, "dump") == 0 && i + 1 < argc) {
                command = kCLIENT_COMMAND_DUMP;
                snprintf(target_interface_text, sizeof(target_interface_text), "%s", argv[++i]);
            } else if (strcmp(command_name, "send") == 0 && i + 2 < argc) {
                command = kCLIENT_COMMAND_SEND;
                open_flags |= OPEN_FLAG_WANT_WRITE;
                snprintf(target_interface_text, sizeof(target_interface_text), "%s", argv[++i]);
                if (!parseFrameSpec(argv[++i], &frame_to_send)) {
                    return false;
                }
            } else if (strcmp(command_name, "attach") == 0 && i + 2 < argc) {
                command = kCLIENT_COMMAND_ATTACH;
                snprintf(target_interface_text, sizeof(target_interface_text), "%s", argv[++i]);
                snprintf(attach_interface_name, sizeof(attach_interface_name), "%s", argv[++i]);
            } else {
                return false;
            }
        } else if (command == kCLIENT_COMMAND_DUMP && dump_filter_count < SUBSCRIBE_FILTERS_MAX) {
            if (!parseFilterSpec(argv[i], &dump_filters[dump_filter_count])) {
                return false;
            }
            dump_filter_count++;
        }
    }

    if (command_name == NULL) {
        return false;
    }

    if (command == kCLIENT_COMMAND_DUMP || command == kCLIENT_COMMAND_SEND) {
        if (InterfaceName_IsNamespaced(target_interface_text)) {
            target_is_namespaced = true;
        } else {
            target_interface_id = (uint32_t)strtoul(target_interface_text, NULL, 10);
        }
    }

    if (connect_url == NULL) {
        connect_scheme = kCONNECT_SCHEME_UNIX;
        snprintf(host, CONNECT_URL_HOST_MAX, "%s", HUB_DEFAULT_UNIX_SOCKET_PATH);
        port_text[0] = '\0';
        return true;
    }

    return ConnectUrl_Parse(connect_url, &connect_scheme, host, port_text);
}

static bool parseFrameSpec(const char *text, FrameMessage *frame)
{
    const char *separator = strchr(text, '#');
    char *id_end = NULL;
    size_t id_digits;

    if (separator == NULL) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->can_id = (uint32_t)strtoul(text, &id_end, 16);
    if (id_end != separator || id_end == text) {
        return false;
    }

    id_digits = (size_t)(separator - text);
    if (id_digits > 3 || frame->can_id > FRAME_CAN_ID_SFF_MAX) {
        frame->can_id |= FRAME_CAN_ID_FLAG_EFF;
    }

    return parseHexPayload(separator + 1, frame);
}

static bool parseFilterSpec(const char *text, CanFilter *filter)
{
    const char *separator = strchr(text, ':');
    char *id_end = NULL;
    char *mask_end = NULL;

    memset(filter, 0, sizeof(*filter));
    filter->can_id = (uint32_t)strtoul(text, &id_end, 16);
    if (id_end == text) {
        return false;
    }

    if (separator == NULL) {
        filter->can_mask = FRAME_CAN_ID_MASK;
        return id_end == text + strlen(text);
    }
    if (id_end != separator) {
        return false;
    }

    filter->can_mask = (uint32_t)strtoul(separator + 1, &mask_end, 16);
    return mask_end != separator + 1 && mask_end == text + strlen(text);
}

static bool parseHexPayload(const char *text, FrameMessage *frame)
{
    char byte_text[3] = { 0, 0, 0 };
    char *byte_end = NULL;
    size_t digits = strlen(text);
    size_t i;

    if (digits % 2 != 0 || digits / 2 > FRAME_PAYLOAD_MAX_FD) {
        return false;
    }

    frame->payload_length = (uint8_t)(digits / 2);
    if (frame->payload_length > FRAME_PAYLOAD_MAX_CLASSIC) {
        frame->frame_flags |= FRAME_FLAG_FD;
    }

    for(i=0; i<frame->payload_length; i++) {
        byte_text[0] = text[i * 2];
        byte_text[1] = text[i * 2 + 1];
        frame->payload[i] = (uint8_t)strtoul(byte_text, &byte_end, 16);
        if (byte_end != byte_text + 2) {
            return false;
        }
    }

    return true;
}

static void sendFrameAndQuit(void)
{
    frame_to_send.timestamp_us = Clock_RealtimeUs();
    if (!Client_SendFrame(&client, &frame_to_send, frame_to_send.timestamp_us)) {
        LOG_ERROR("could not send the frame");
        exit_code = 1;
        return;
    }

    exit_code = 0;
}

static void startClientCommand(void)
{
    if (command == kCLIENT_COMMAND_LIST) {
        Client_RequestList(&client, 0);
        return;
    }

    if (dump_filter_count > 0) {
        Client_SetFilters(&client, dump_filters, dump_filter_count);
    }
    if (target_is_namespaced) {
        Client_OpenByName(&client, target_interface_text, open_flags);
        return;
    }
    Client_OpenById(&client, target_interface_id, open_flags);
}

static bool initTransport(const char *host, const char *port_text, const TransportEvents *events)
{
    bool initialized;

    if (connect_scheme == kCONNECT_SCHEME_QUIC) {
        initialized = initQuicTransport(host, port_text, events);
        active_port = QuicClientTransport_Port(&quic_transport);
        return initialized;
    }

    if (connect_scheme == kCONNECT_SCHEME_TLS) {
        initialized = initTlsTransport(host, port_text, events);
        active_port = TlsClientTransport_Port(&tls_transport);
        return initialized;
    }

    if (connect_scheme == kCONNECT_SCHEME_UNIX) {
        initialized = TcpClientTransport_InitUnix(&transport, host, events);
    } else {
        initialized = TcpClientTransport_Init(&transport, host, port_text, events);
    }
    active_port = TcpClientTransport_Port(&transport);

    return initialized;
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

static bool initTlsTransport(const char *host, const char *port_text, const TransportEvents *events)
{
    TlsClientSecurityConfig security_config;

    if (!prepareSecurityMaterial(host, port_text)) {
        return false;
    }

    security_config.certificate_path = identity_certificate_path;
    security_config.key_path = identity_key_path;
    security_config.pin_store_path = known_hubs_path;
    security_config.pin_key = pin_key;
    security_config.pinned_fingerprint = NULL;

    return TlsClientTransport_Init(&tls_transport, host, port_text, events, &security_config);
}

static bool initQuicTransport(const char *host, const char *port_text, const TransportEvents *events)
{
    QuicClientSecurityConfig security_config;

    if (!prepareSecurityMaterial(host, port_text)) {
        return false;
    }

    security_config.certificate_path = identity_certificate_path;
    security_config.key_path = identity_key_path;
    security_config.pin_store_path = known_hubs_path;
    security_config.pin_key = pin_key;
    security_config.pinned_fingerprint = NULL;

    return QuicClientTransport_Init(&quic_transport, host, port_text, events, &security_config);
}

static void clientOnListReply(void *context, const ListReplyMessage *reply)
{
    uint8_t i;

    (void)context;

    if (complete_mode) {
        for(i=0; i<reply->count; i++) {
            printf("%s/%s\n", reply->entries[i].agent_name, reply->entries[i].interface_name);
        }
        exit_code = 0;
        return;
    }

    printListReply(reply);
    exit_code = 0;
}

static void clientOnOpenResult(void *context, uint8_t status, uint8_t channel, uint32_t interface_id)
{
    (void)context;

    if (status != OPEN_STATUS_OK) {
        LOG_ERROR("open rejected for interface %u", interface_id);
        exit_code = 1;
        return;
    }
    if (command == kCLIENT_COMMAND_SEND) {
        sendFrameAndQuit();
        return;
    }

    LOG_INFO("dumping interface %u (channel %u), ctrl-c to stop", interface_id, channel);
}

static void clientOnFrame(void *context, const FrameMessage *frame)
{
    (void)context;

    printFrame(frame);
}

static void clientOnError(void *context, uint16_t code, const char *detail)
{
    (void)context;

    if (code == CLIENT_ERROR_INTERFACE_NOT_FOUND) {
        LOG_ERROR("interface %s not found", detail);
    } else if (code == CLIENT_ERROR_MALFORMED_REPLY) {
        LOG_ERROR("%s", detail);
    } else {
        LOG_ERROR("hub error %u: %.*s", code, (int)ERROR_DETAIL_SIZE, detail);
    }

    exit_code = 1;
}

static void clientOnDisconnected(void *context)
{
    (void)context;

    LOG_WARN("connection lost");
    exit_code = 1;
}

static void printListReply(const ListReplyMessage *reply)
{
    uint8_t i;

    printf("%-10s %-32s %s\n", "id", "agent", "interface");
    for(i=0; i<reply->count; i++) {
        printf(
            "%-10u %-32s %s\n",
            reply->entries[i].interface_id,
            reply->entries[i].agent_name,
            reply->entries[i].interface_name
        );
    }
}

static void printFrame(const FrameMessage *frame)
{
    uint8_t i;

    printf(
        "(%llu.%06llu) %03X [%u] ",
        (unsigned long long)(frame->timestamp_us / 1000000),
        (unsigned long long)(frame->timestamp_us % 1000000),
        frame->can_id & FRAME_CAN_ID_MASK,
        frame->payload_length
    );
    for(i=0; i<frame->payload_length; i++) {
        printf("%02X ", frame->payload[i]);
    }
    printf("\n");
    fflush(stdout);
}

static int32_t runEventLoop(void)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    EpollRegistry poll_registry;
    int32_t event_count;
    uint32_t wanted_mask;
    int32_t current_fd;
    int32_t i;

    if (connect_scheme == kCONNECT_SCHEME_QUIC) {
        return runQuicEventLoop();
    }

    if (!EpollRegistry_Open(&poll_registry)) {
        return 1;
    }

    while (exit_code < 0) {
        if (connect_scheme == kCONNECT_SCHEME_TLS) {
            current_fd = TlsClientTransport_Fd(&tls_transport);
            wanted_mask = EPOLLIN;
            if (TlsClientTransport_WantsWritable(&tls_transport)) {
                wanted_mask |= EPOLLOUT;
            }
        } else {
            current_fd = TcpClientTransport_Fd(&transport);
            wanted_mask = EPOLLIN;
            if (TcpClientTransport_WantsWritable(&transport)) {
                wanted_mask |= EPOLLOUT;
            }
        }
        EpollRegistry_SyncSlot(&poll_registry, TCP_SLOT, current_fd, wanted_mask, (uint32_t)current_fd);

        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);
        for(i=0; i<event_count; i++) {
            if (connect_scheme == kCONNECT_SCHEME_TLS) {
                if (events[i].events & EPOLLOUT) {
                    TlsClientTransport_OnWritable(&tls_transport);
                }
                if (events[i].events & EPOLLIN) {
                    TlsClientTransport_OnReadable(&tls_transport);
                }
            } else {
                if (events[i].events & EPOLLOUT) {
                    TcpClientTransport_OnWritable(&transport);
                }
                if (events[i].events & EPOLLIN) {
                    TcpClientTransport_OnReadable(&transport);
                }
            }
        }
    }

    return exit_code;
}

static int32_t runQuicEventLoop(void)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    EpollRegistry poll_registry;
    int32_t udp_fd = QuicClientTransport_UdpFd(&quic_transport);
    int32_t timer_fd = QuicClientTransport_TimerFd(&quic_transport);
    int32_t event_count;
    int32_t i;

    if (!EpollRegistry_Open(&poll_registry)) {
        return 1;
    }
    if (!EpollRegistry_AddStatic(&poll_registry, udp_fd, (uint32_t)udp_fd)) {
        return 1;
    }
    if (!EpollRegistry_AddStatic(&poll_registry, timer_fd, (uint32_t)timer_fd)) {
        return 1;
    }

    while (exit_code < 0) {
        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);
        for(i=0; i<event_count; i++) {
            if ((int32_t)events[i].data.u32 == udp_fd) {
                QuicClientTransport_OnUdpReadable(&quic_transport);
            }
            if ((int32_t)events[i].data.u32 == timer_fd) {
                QuicClientTransport_OnTimer(&quic_transport);
            }
        }
    }

    return exit_code;
}

static bool parseSocketcandListen(const char *value)
{
    const char *remainder = strstr(value, "://");

    remainder = (remainder != NULL) ? remainder + 3 : value;

    return ConnectUrl_SplitListenAddress(remainder, socketcand_bind_address, socketcand_port_text);
}

static void resolveDeviceName(char *out, size_t out_size)
{
    if (gethostname(out, out_size) != 0) {
        snprintf(out, out_size, "can-hub");
    }
    out[out_size - 1] = '\0';
}

static void buildBeaconUrl(char *out, size_t out_size)
{
    snprintf(out, out_size, "can://%s:%s", socketcand_bind_address, socketcand_port_text);
}

static int32_t runSocketcandServer(const char *host, const char *port_text)
{
    TransportEvents hub_events = SocketcandApp_TransportEvents(&socketcand_app);
    SocketcandServerEvents server_events = SocketcandApp_ServerEvents(&socketcand_app);
    char device_name[SOCKETCAND_DEVICE_NAME_SIZE];
    char beacon_url[SOCKETCAND_URL_SIZE];

    if (!initTransport(host, port_text, &hub_events)) {
        LOG_ERROR("could not initialize transport");
        return 1;
    }
    if (!SocketcandServer_Init(&socketcand_server, socketcand_bind_address, socketcand_port_text, SOCKETCAND_BEACON_PORT, &server_events)) {
        LOG_ERROR("could not bind socketcand server on %s:%s", socketcand_bind_address, socketcand_port_text);
        return 1;
    }
    if (strcmp(socketcand_bind_address, HUB_LOCAL_ADDRESS) != 0) {
        LOG_WARN("socketcand server bound to %s (not loopback) — socketcand clients are unauthenticated", socketcand_bind_address);
    }

    resolveDeviceName(device_name, sizeof(device_name));
    buildBeaconUrl(beacon_url, sizeof(beacon_url));
    SocketcandApp_Init(&socketcand_app, active_port, SocketcandServer_Port(&socketcand_server), device_name, beacon_url, beacon_enabled);
    SocketcandApp_SetName(&socketcand_app, client_name);

    LOG_INFO("socketcand server on %s:%s, hub %s, beacon %s",
            socketcand_bind_address, socketcand_port_text, host, beacon_enabled ? "on" : "off");

    return runSocketcandLoop();
}

static void syncHubStream(EpollRegistry *poll_registry)
{
    int32_t current_fd;
    uint32_t wanted_mask = EPOLLIN;

    if (connect_scheme == kCONNECT_SCHEME_QUIC) {
        return;
    }
    if (connect_scheme == kCONNECT_SCHEME_TLS) {
        current_fd = TlsClientTransport_Fd(&tls_transport);
        if (TlsClientTransport_WantsWritable(&tls_transport)) {
            wanted_mask |= EPOLLOUT;
        }
    } else {
        current_fd = TcpClientTransport_Fd(&transport);
        if (TcpClientTransport_WantsWritable(&transport)) {
            wanted_mask |= EPOLLOUT;
        }
    }
    EpollRegistry_SyncSlot(poll_registry, SOCKETCAND_HUB_STREAM_SLOT, current_fd, wanted_mask, SC_TAG_HUB_STREAM);
}

static uint32_t socketcandSlotMask(uint8_t slot)
{
    uint32_t mask = EPOLLIN;

    if (SocketcandServer_SlotFd(&socketcand_server, slot) < 0) {
        return 0;
    }
    if (SocketcandServer_SlotWantsWritable(&socketcand_server, slot)) {
        mask |= EPOLLOUT;
    }

    return mask;
}

static void handleSocketcandSlotEvent(uint8_t slot, const struct epoll_event *event)
{
    if (event->events & EPOLLOUT) {
        SocketcandServer_OnSlotWritable(&socketcand_server, slot);
    }
    if (event->events & EPOLLIN) {
        SocketcandServer_OnSlotReadable(&socketcand_server, slot);
    }
}

static void handleHubStreamEvent(const struct epoll_event *event)
{
    bool writable = (event->events & EPOLLOUT) != 0;
    bool readable = (event->events & EPOLLIN) != 0;

    if (connect_scheme == kCONNECT_SCHEME_TLS) {
        if (writable) {
            TlsClientTransport_OnWritable(&tls_transport);
        }
        if (readable) {
            TlsClientTransport_OnReadable(&tls_transport);
        }
        return;
    }
    if (writable) {
        TcpClientTransport_OnWritable(&transport);
    }
    if (readable) {
        TcpClientTransport_OnReadable(&transport);
    }
}

static void dispatchSocketcandEvent(const struct epoll_event *event)
{
    uint32_t tag = event->data.u32;

    if (tag == SC_TAG_SC_LISTEN) {
        SocketcandServer_OnAcceptReady(&socketcand_server);
        return;
    }
    if (tag >= SC_TAG_SC_SLOT_BASE && tag < SC_TAG_SC_SLOT_BASE + SOCKETCAND_SERVER_SLOTS_MAX) {
        handleSocketcandSlotEvent((uint8_t)(tag - SC_TAG_SC_SLOT_BASE), event);
        return;
    }
    if (tag == SC_TAG_HUB_QUIC_UDP) {
        QuicClientTransport_OnUdpReadable(&quic_transport);
        return;
    }
    if (tag == SC_TAG_HUB_QUIC_TIMER) {
        QuicClientTransport_OnTimer(&quic_transport);
        return;
    }
    if (tag == SC_TAG_HUB_STREAM) {
        handleHubStreamEvent(event);
    }
}

static int32_t runSocketcandLoop(void)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    EpollRegistry poll_registry;
    int32_t event_count;
    int32_t i;
    uint8_t slot;

    if (!EpollRegistry_Open(&poll_registry)) {
        return 1;
    }
    if (!EpollRegistry_AddStatic(&poll_registry, SocketcandServer_ListenFd(&socketcand_server), SC_TAG_SC_LISTEN)) {
        return 1;
    }
    if (connect_scheme == kCONNECT_SCHEME_QUIC) {
        EpollRegistry_AddStatic(&poll_registry, QuicClientTransport_UdpFd(&quic_transport), SC_TAG_HUB_QUIC_UDP);
        EpollRegistry_AddStatic(&poll_registry, QuicClientTransport_TimerFd(&quic_transport), SC_TAG_HUB_QUIC_TIMER);
    }

    for (;;) {
        syncHubStream(&poll_registry);
        for(slot=0; slot<SOCKETCAND_SERVER_SLOTS_MAX; slot++) {
            EpollRegistry_SyncSlot(
                &poll_registry,
                (uint8_t)(1 + slot),
                SocketcandServer_SlotFd(&socketcand_server, slot),
                socketcandSlotMask(slot),
                SC_TAG_SC_SLOT_BASE + slot
            );
        }

        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);
        for(i=0; i<event_count; i++) {
            dispatchSocketcandEvent(&events[i]);
        }
        SocketcandApp_Tick(&socketcand_app, Clock_RealtimeUs());
    }
}

static int32_t runAttach(const char *host, const char *port_text)
{
    TransportEvents events = MirrorApp_TransportEvents(&mirror_app);
    RegisterMessage registration;

    memset(&registration, 0, sizeof(registration));
    registration.interface_count = 1;
    snprintf(registration.interface_names[0], REGISTER_INTERFACE_NAME_SIZE, "%s", attach_interface_name);

    if (!initTransport(host, port_text, &events)) {
        LOG_ERROR("could not initialize transport");
        return 1;
    }
    if (!SocketCanAdapter_Open(&mirror_can_adapter, &registration, false)) {
        LOG_ERROR("could not open vcan %s (does it exist?)", attach_interface_name);
        return 1;
    }

    if (InterfaceName_IsNamespaced(target_interface_text)) {
        MirrorApp_Init(&mirror_app, active_port, SocketCanAdapter_Port(&mirror_can_adapter), 0, target_interface_text);
    } else {
        MirrorApp_Init(
            &mirror_app,
            active_port,
            SocketCanAdapter_Port(&mirror_can_adapter),
            (uint32_t)strtoul(target_interface_text, NULL, 10),
            NULL
        );
    }
    MirrorApp_SetName(&mirror_app, client_name);
    MirrorApp_SetReliable(&mirror_app, (open_flags & OPEN_FLAG_RELIABLE) != 0);

    if (!active_port->connect(active_port->context)) {
        LOG_ERROR("could not connect to %s", host);
        return 1;
    }

    LOG_INFO("mirroring interface %s to %s, ctrl-c to stop", target_interface_text, attach_interface_name);

    return runAttachLoop();
}

static int32_t runAttachLoop(void)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    EpollRegistry poll_registry;
    int32_t event_count;
    int32_t i;

    if (!EpollRegistry_Open(&poll_registry)) {
        return 1;
    }
    if (connect_scheme == kCONNECT_SCHEME_QUIC) {
        EpollRegistry_AddStatic(&poll_registry, QuicClientTransport_UdpFd(&quic_transport), SC_TAG_HUB_QUIC_UDP);
        EpollRegistry_AddStatic(&poll_registry, QuicClientTransport_TimerFd(&quic_transport), SC_TAG_HUB_QUIC_TIMER);
    }
    EpollRegistry_AddStatic(&poll_registry, SocketCanAdapter_Fd(&mirror_can_adapter, 0), ATTACH_TAG_CAN);

    while (MirrorApp_State(&mirror_app) != kMIRROR_FAILED) {
        syncHubStream(&poll_registry);
        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);
        for(i=0; i<event_count; i++) {
            dispatchAttachEvent(&events[i]);
        }
    }

    LOG_WARN("connection lost");

    return 1;
}

static void drainCanFrames(void)
{
    FrameMessage frame;

    while (SocketCanAdapter_ReadFrame(&mirror_can_adapter, 0, &frame)) {
        MirrorApp_OnCanFrame(&mirror_app, &frame);
    }
}

static void dispatchAttachEvent(const struct epoll_event *event)
{
    uint32_t tag = event->data.u32;

    if (tag == ATTACH_TAG_CAN) {
        drainCanFrames();
        return;
    }
    if (tag == SC_TAG_HUB_QUIC_UDP) {
        QuicClientTransport_OnUdpReadable(&quic_transport);
        return;
    }
    if (tag == SC_TAG_HUB_QUIC_TIMER) {
        QuicClientTransport_OnTimer(&quic_transport);
        return;
    }
    if (tag == SC_TAG_HUB_STREAM) {
        handleHubStreamEvent(event);
    }
}
