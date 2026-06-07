#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>

#include "platform/linux/clock/clock.h"
#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/shared/hub_defaults.h"
#include "platform/linux/shared/tls_identity.h"
#include "platform/linux/tcp/tcp_client_transport.h"
#include "platform/linux/tls/tls_client_transport.h"
#include "protocol/error_message.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"

#define MAX_EPOLL_EVENTS 8
#define POLL_PERIOD_MS 100
#define TCP_SLOT 0
#define IDENTITY_NAME "client"
#define KNOWN_HUBS_FILE "known_hubs"
#define KNOWN_HUBS_PATH_MAX (TLS_IDENTITY_PATH_MAX + sizeof(KNOWN_HUBS_FILE))
#define PIN_KEY_MAX (CONNECT_URL_HOST_MAX + CONNECT_URL_PORT_TEXT_MAX)

typedef enum tclient_command_e {
    kCLIENT_COMMAND_LIST = 0,
    kCLIENT_COMMAND_DUMP,
    kCLIENT_COMMAND_SEND,
    kCLIENT_COMMAND_MAX,
} TCLIENT_COMMAND;

static TcpClientTransport transport;
static TlsClientTransport tls_transport;
static TransportPort *active_port;
static uint8_t command;
static uint8_t connect_scheme;
static uint8_t open_flags;
static uint32_t target_interface_id;
static FrameMessage frame_to_send;
static int32_t exit_code = -1;
static const char *state_directory_override;
static char state_directory[TLS_IDENTITY_PATH_MAX];
static char identity_certificate_path[TLS_IDENTITY_PATH_MAX];
static char identity_key_path[TLS_IDENTITY_PATH_MAX];
static char known_hubs_path[KNOWN_HUBS_PATH_MAX];
static char pin_key[PIN_KEY_MAX];

static bool parseArguments(int argc, char **argv, char *host, char *port_text);
static bool parseFrameSpec(const char *text, FrameMessage *frame);
static bool parseHexPayload(const char *text, FrameMessage *frame);
static void sendFrameAndQuit(uint8_t channel);
static bool initTransport(const char *host, const char *port_text, const TransportEvents *events);
static bool initTlsTransport(const char *host, const char *port_text, const TransportEvents *events);
static void onConnected(void *context);
static void onDisconnected(void *context, uint64_t now_us);
static void onControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void onFrame(void *context, const uint8_t *data, size_t size);
static void printListReply(const uint8_t *payload, uint16_t payload_length);
static void printFrame(const FrameMessage *frame);
static int32_t runEventLoop(void);

int main(int argc, char **argv)
{
    char host[CONNECT_URL_HOST_MAX];
    char port_text[CONNECT_URL_PORT_TEXT_MAX];
    TransportEvents events = {
        .context = &transport,
        .on_connected = onConnected,
        .on_disconnected = onDisconnected,
        .on_control = onControl,
        .on_frame = onFrame,
    };

    if (!parseArguments(argc, argv, host, port_text)) {
        fprintf(stderr, "usage: %s [--connect tls://<host>:<port>|tcp://<host>:<port>|unix://<path>]\n", argv[0]);
        fprintf(stderr, "       [--state-dir <path>] list | dump [--no-echo] <interface-id>\n");
        fprintf(stderr, "       | send <interface-id> <can-id>#<hex-payload>   (cansend syntax, e.g. 123#DEADBEEF)\n");
        fprintf(stderr, "       default: --connect unix://" HUB_DEFAULT_UNIX_SOCKET_PATH "\n");
        return 1;
    }

    if (!initTransport(host, port_text, &events)) {
        fprintf(stderr, "could not initialize transport\n");
        return 1;
    }
    if (!active_port->connect(active_port->context)) {
        fprintf(stderr, "could not connect to %s\n", host);
        return 1;
    }

    return runEventLoop();
}

/* ---------- private ---------- */

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
        } else if (strcmp(argv[i], "--no-echo") == 0) {
            open_flags |= OPEN_FLAG_SUPPRESS_OWN_ECHO;
        } else if (command_name == NULL) {
            command_name = argv[i];
            if (strcmp(command_name, "list") == 0) {
                command = kCLIENT_COMMAND_LIST;
            } else if (strcmp(command_name, "dump") == 0 && i + 1 < argc) {
                command = kCLIENT_COMMAND_DUMP;
                target_interface_id = (uint32_t)strtoul(argv[++i], NULL, 10);
            } else if (strcmp(command_name, "send") == 0 && i + 2 < argc) {
                command = kCLIENT_COMMAND_SEND;
                target_interface_id = (uint32_t)strtoul(argv[++i], NULL, 10);
                if (!parseFrameSpec(argv[++i], &frame_to_send)) {
                    return false;
                }
            } else {
                return false;
            }
        }
    }

    if (command_name == NULL) {
        return false;
    }

    if (connect_url == NULL) {
        connect_scheme = kCONNECT_SCHEME_UNIX;
        snprintf(host, CONNECT_URL_HOST_MAX, "%s", HUB_DEFAULT_UNIX_SOCKET_PATH);
        port_text[0] = '\0';
        return true;
    }

    if (!ConnectUrl_Parse(connect_url, &connect_scheme, host, port_text)) {
        return false;
    }

    return connect_scheme != kCONNECT_SCHEME_QUIC;
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

static void sendFrameAndQuit(uint8_t channel)
{
    uint8_t encoded[MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD];
    size_t encoded_size;

    frame_to_send.channel = channel;
    frame_to_send.timestamp_us = Clock_RealtimeUs();
    encoded_size = FrameMessage_Encode(&frame_to_send, encoded, sizeof(encoded));
    if (encoded_size == 0 || !active_port->send_frame(active_port->context, encoded, encoded_size)) {
        fprintf(stderr, "could not send the frame\n");
        exit_code = 1;
        return;
    }

    exit_code = 0;
}

static bool initTransport(const char *host, const char *port_text, const TransportEvents *events)
{
    bool initialized;

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

static bool initTlsTransport(const char *host, const char *port_text, const TransportEvents *events)
{
    TlsClientSecurityConfig security_config;

    if (!TlsIdentity_ResolveStateDirectory(state_directory_override, state_directory)) {
        return false;
    }
    if (!TlsIdentity_LoadOrCreate(state_directory, IDENTITY_NAME, identity_certificate_path, identity_key_path)) {
        return false;
    }

    snprintf(known_hubs_path, sizeof(known_hubs_path), "%s/%s", state_directory, KNOWN_HUBS_FILE);
    snprintf(pin_key, sizeof(pin_key), "%s:%s", host, port_text);

    security_config.certificate_path = identity_certificate_path;
    security_config.key_path = identity_key_path;
    security_config.pin_store_path = known_hubs_path;
    security_config.pin_key = pin_key;

    return TlsClientTransport_Init(&tls_transport, host, port_text, events, &security_config);
}

static void onConnected(void *context)
{
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_CLIENT, 0 };
    ListMessage list = { 0 };
    OpenMessage open = { target_interface_id, open_flags };
    uint8_t encoded[64];
    size_t encoded_size;

    (void)context;

    encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
    active_port->send_control(active_port->context, encoded, encoded_size);

    if (command == kCLIENT_COMMAND_LIST) {
        encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));
    } else {
        encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));
    }
    active_port->send_control(active_port->context, encoded, encoded_size);
}

static void onDisconnected(void *context, uint64_t now_us)
{
    (void)context;
    (void)now_us;

    fprintf(stderr, "connection lost\n");
    exit_code = 1;
}

static void onControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    MessageHeader header;
    OpenAckMessage ack;
    ErrorMessage error;

    (void)context;
    (void)now_us;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }

    if (header.type == kMESSAGE_TYPE_ERROR) {
        if (ErrorMessage_Decode(&error, data + MESSAGE_HEADER_SIZE, header.length)) {
            fprintf(stderr, "hub error %u: %s\n", error.code, error.detail);
        }
        exit_code = 1;
        return;
    }
    if (header.type == kMESSAGE_TYPE_LIST_REPLY) {
        printListReply(data + MESSAGE_HEADER_SIZE, header.length);
        exit_code = 0;
        return;
    }
    if (header.type == kMESSAGE_TYPE_OPEN_ACK) {
        OpenAckMessage_Decode(&ack, data + MESSAGE_HEADER_SIZE, header.length);
        if (ack.status != OPEN_STATUS_OK) {
            fprintf(stderr, "open rejected for interface %u\n", ack.interface_id);
            exit_code = 1;
            return;
        }
        if (command == kCLIENT_COMMAND_SEND) {
            sendFrameAndQuit(ack.channel);
            return;
        }
        fprintf(stderr, "dumping interface %u (channel %u), ctrl-c to stop\n", ack.interface_id, ack.channel);
    }
}

static void onFrame(void *context, const uint8_t *data, size_t size)
{
    MessageHeader header;
    FrameMessage frame;

    (void)context;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (!FrameMessage_Decode(&frame, data + MESSAGE_HEADER_SIZE, header.length)) {
        return;
    }

    printFrame(&frame);
}

static void printListReply(const uint8_t *payload, uint16_t payload_length)
{
    ListReplyMessage reply;
    uint8_t i;

    if (!ListReplyMessage_Decode(&reply, payload, payload_length)) {
        fprintf(stderr, "malformed list reply\n");
        return;
    }

    printf("%-10s %-32s %s\n", "id", "agent", "interface");
    for(i=0; i<reply.count; i++) {
        printf(
            "%-10u %-32s %s\n",
            reply.entries[i].interface_id,
            reply.entries[i].agent_name,
            reply.entries[i].interface_name
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
