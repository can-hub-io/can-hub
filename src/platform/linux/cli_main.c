#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/epoll.h>

#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/shared/hub_defaults.h"
#include "platform/linux/tcp/tcp_client_transport.h"
#include "protocol/admin_message.h"
#include "protocol/hello_message.h"
#include "protocol/message_header.h"

#define MAX_EPOLL_EVENTS 8
#define POLL_PERIOD_MS 100
#define TCP_SLOT 0
#define COMMAND_BUFFER_SIZE 256

typedef enum tcli_command_e {
    kCLI_COMMAND_STATUS = 0,
    kCLI_COMMAND_PEERS,
    kCLI_COMMAND_KICK,
    kCLI_COMMAND_PINS,
    kCLI_COMMAND_FORGET,
    kCLI_COMMAND_MAX,
} TCLI_COMMAND;

static TcpClientTransport transport;
static uint8_t command;
static uint8_t connect_scheme;
static char target_agent[REGISTER_AGENT_NAME_SIZE];
static uint16_t page_offset;
static bool page_header_printed;
static int32_t exit_code = -1;

static bool parseArguments(int argc, char **argv, char *host, char *port_text);
static bool parseCommand(const char *name, const char *argument);
static bool initTransport(const char *host, const char *port_text, const TransportEvents *events);
static void onConnected(void *context);
static void onDisconnected(void *context, uint64_t now_us);
static void onControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void onFrame(void *context, const uint8_t *data, size_t size);
static void sendRequest(void);
static void printStatusReply(const uint8_t *payload, uint16_t payload_length);
static void printPeersReply(const uint8_t *payload, uint16_t payload_length);
static void printPinsReply(const uint8_t *payload, uint16_t payload_length);
static void printActionReply(uint8_t status, const char *action);
static const char *roleName(uint8_t role);
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
        fprintf(stderr, "usage: %s [--connect tcp://<host>:<port>|unix://<path>] <command>\n", argv[0]);
        fprintf(stderr, "commands: status | peers | kick <agent> | pins | forget <agent>\n");
        fprintf(stderr, "default: --connect unix://" HUB_DEFAULT_UNIX_SOCKET_PATH "\n");
        return 1;
    }

    if (!initTransport(host, port_text, &events)) {
        fprintf(stderr, "could not initialize transport\n");
        return 1;
    }
    if (!transport.port.connect(transport.port.context)) {
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
        } else if (command_name == NULL) {
            command_name = argv[i];
            if (!parseCommand(command_name, i + 1 < argc ? argv[i + 1] : NULL)) {
                return false;
            }
            if (command == kCLI_COMMAND_KICK || command == kCLI_COMMAND_FORGET) {
                i++;
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

static bool parseCommand(const char *name, const char *argument)
{
    if (strcmp(name, "status") == 0) {
        command = kCLI_COMMAND_STATUS;
        return true;
    }
    if (strcmp(name, "peers") == 0) {
        command = kCLI_COMMAND_PEERS;
        return true;
    }
    if (strcmp(name, "pins") == 0) {
        command = kCLI_COMMAND_PINS;
        return true;
    }
    if (strcmp(name, "kick") == 0 && argument != NULL) {
        command = kCLI_COMMAND_KICK;
        snprintf(target_agent, sizeof(target_agent), "%s", argument);
        return true;
    }
    if (strcmp(name, "forget") == 0 && argument != NULL) {
        command = kCLI_COMMAND_FORGET;
        snprintf(target_agent, sizeof(target_agent), "%s", argument);
        return true;
    }

    return false;
}

static bool initTransport(const char *host, const char *port_text, const TransportEvents *events)
{
    if (connect_scheme == kCONNECT_SCHEME_UNIX) {
        return TcpClientTransport_InitUnix(&transport, host, events);
    }

    return TcpClientTransport_Init(&transport, host, port_text, events);
}

static void onConnected(void *context)
{
    TcpClientTransport *self = context;
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_ADMIN, 0 };
    uint8_t encoded[COMMAND_BUFFER_SIZE];
    size_t encoded_size;

    encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
    self->port.send_control(self->port.context, encoded, encoded_size);

    sendRequest();
}

static void onDisconnected(void *context, uint64_t now_us)
{
    (void)context;
    (void)now_us;

    fprintf(stderr, "connection lost (is the admin role allowed on this transport?)\n");
    exit_code = 1;
}

static void onControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    MessageHeader header;
    AdminKickReplyMessage kick_reply;
    AdminForgetReplyMessage forget_reply;

    (void)context;
    (void)now_us;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }

    if (header.type == kMESSAGE_TYPE_ADMIN_STATUS_REPLY) {
        printStatusReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_PEERS_REPLY) {
        printPeersReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_PINS_REPLY) {
        printPinsReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_KICK_REPLY) {
        AdminKickReplyMessage_Decode(&kick_reply, data + MESSAGE_HEADER_SIZE, header.length);
        printActionReply(kick_reply.status, "kicked");
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_FORGET_REPLY) {
        AdminForgetReplyMessage_Decode(&forget_reply, data + MESSAGE_HEADER_SIZE, header.length);
        printActionReply(forget_reply.status, "forgot");
        return;
    }
}

static void onFrame(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    (void)data;
    (void)size;
}

static void sendRequest(void)
{
    AdminPeersMessage peers = { page_offset };
    AdminPinsMessage pins = { page_offset };
    AdminKickMessage kick;
    AdminForgetMessage forget;
    uint8_t encoded[COMMAND_BUFFER_SIZE];
    size_t encoded_size = 0;

    if (command == kCLI_COMMAND_STATUS) {
        encoded_size = AdminStatusMessage_Encode(encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_PEERS) {
        encoded_size = AdminPeersMessage_Encode(&peers, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_PINS) {
        encoded_size = AdminPinsMessage_Encode(&pins, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_KICK) {
        memset(&kick, 0, sizeof(kick));
        snprintf(kick.agent_name, sizeof(kick.agent_name), "%s", target_agent);
        encoded_size = AdminKickMessage_Encode(&kick, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_FORGET) {
        memset(&forget, 0, sizeof(forget));
        snprintf(forget.agent_name, sizeof(forget.agent_name), "%s", target_agent);
        encoded_size = AdminForgetMessage_Encode(&forget, encoded, sizeof(encoded));
    }

    if (encoded_size == 0) {
        exit_code = 1;
        return;
    }

    transport.port.send_control(transport.port.context, encoded, encoded_size);
}

static void printStatusReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminStatusReplyMessage reply;

    if (!AdminStatusReplyMessage_Decode(&reply, payload, payload_length)) {
        fprintf(stderr, "malformed status reply\n");
        exit_code = 1;
        return;
    }

    printf("peers: %u (agents %u, clients %u)\n", reply.peer_count, reply.agent_count, reply.client_count);
    printf("interfaces: %u\n", reply.interface_count);
    exit_code = 0;
}

static void printPeersReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminPeersReplyMessage reply;
    uint8_t i;

    if (!AdminPeersReplyMessage_Decode(&reply, payload, payload_length)) {
        fprintf(stderr, "malformed peers reply\n");
        exit_code = 1;
        return;
    }

    if (!page_header_printed) {
        printf("%-12s %-8s %-32s %s\n", "peer-id", "role", "agent", "fingerprint");
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        printf(
            "0x%08X   %-8s %-32s %s\n",
            reply.entries[i].peer_id,
            roleName(reply.entries[i].role),
            reply.entries[i].agent_name[0] != '\0' ? reply.entries[i].agent_name : "-",
            reply.entries[i].fingerprint_hex[0] != '\0' ? reply.entries[i].fingerprint_hex : "-"
        );
    }

    if (reply.flags & ADMIN_REPLY_FLAG_MORE) {
        page_offset += reply.count;
        sendRequest();
        return;
    }
    exit_code = 0;
}

static void printPinsReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminPinsReplyMessage reply;
    uint8_t i;

    if (!AdminPinsReplyMessage_Decode(&reply, payload, payload_length)) {
        fprintf(stderr, "malformed pins reply\n");
        exit_code = 1;
        return;
    }

    if (!page_header_printed) {
        printf("%-32s %s\n", "agent", "fingerprint");
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        printf("%-32s %s\n", reply.entries[i].agent_name, reply.entries[i].fingerprint_hex);
    }

    if (reply.flags & ADMIN_REPLY_FLAG_MORE) {
        page_offset += reply.count;
        sendRequest();
        return;
    }
    exit_code = 0;
}

static void printActionReply(uint8_t status, const char *action)
{
    if (status != ADMIN_STATUS_OK) {
        fprintf(stderr, "unknown agent %s\n", target_agent);
        exit_code = 1;
        return;
    }

    printf("%s %s\n", action, target_agent);
    exit_code = 0;
}

static const char *roleName(uint8_t role)
{
    if (role == kPEER_ROLE_AGENT) {
        return "agent";
    }
    if (role == kPEER_ROLE_CLIENT) {
        return "client";
    }
    if (role == kPEER_ROLE_ADMIN) {
        return "admin";
    }

    return "unknown";
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
        current_fd = TcpClientTransport_Fd(&transport);
        wanted_mask = EPOLLIN;
        if (TcpClientTransport_WantsWritable(&transport)) {
            wanted_mask |= EPOLLOUT;
        }
        EpollRegistry_SyncSlot(&poll_registry, TCP_SLOT, current_fd, wanted_mask, (uint32_t)current_fd);

        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);
        for(i=0; i<event_count; i++) {
            if (events[i].events & EPOLLOUT) {
                TcpClientTransport_OnWritable(&transport);
            }
            if (events[i].events & EPOLLIN) {
                TcpClientTransport_OnReadable(&transport);
            }
        }
    }

    return exit_code;
}
