#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>

#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/tcp/tcp_client_transport.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"

#define MAX_EPOLL_EVENTS 8
#define POLL_PERIOD_MS 100
#define TCP_SLOT 0

typedef enum tclient_command_e {
    kCLIENT_COMMAND_LIST = 0,
    kCLIENT_COMMAND_DUMP,
    kCLIENT_COMMAND_MAX,
} TCLIENT_COMMAND;

static TcpClientTransport transport;
static uint8_t command;
static uint32_t dump_interface_id;
static int32_t exit_code = -1;

static bool parseArguments(int argc, char **argv, char *host, char *port_text);
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
        fprintf(stderr, "usage: %s --connect tcp://<host>:<port> list\n", argv[0]);
        fprintf(stderr, "       %s --connect tcp://<host>:<port> dump <interface-id>\n", argv[0]);
        return 1;
    }

    if (!TcpClientTransport_Init(&transport, host, port_text, &events)) {
        fprintf(stderr, "could not initialize transport\n");
        return 1;
    }
    if (!transport.port.connect(transport.port.context)) {
        fprintf(stderr, "could not connect to %s:%s\n", host, port_text);
        return 1;
    }

    return runEventLoop();
}

/* ---------- private ---------- */

static bool parseArguments(int argc, char **argv, char *host, char *port_text)
{
    const char *connect_url = NULL;
    const char *command_name = NULL;
    uint8_t scheme;
    int32_t i;

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            connect_url = argv[++i];
        } else if (command_name == NULL) {
            command_name = argv[i];
            if (strcmp(command_name, "list") == 0) {
                command = kCLIENT_COMMAND_LIST;
            } else if (strcmp(command_name, "dump") == 0 && i + 1 < argc) {
                command = kCLIENT_COMMAND_DUMP;
                dump_interface_id = (uint32_t)strtoul(argv[++i], NULL, 10);
            } else {
                return false;
            }
        }
    }

    if (connect_url == NULL || command_name == NULL) {
        return false;
    }

    if (!ConnectUrl_Parse(connect_url, &scheme, host, port_text)) {
        return false;
    }

    return scheme == kCONNECT_SCHEME_TCP;
}

static void onConnected(void *context)
{
    TcpClientTransport *self = context;
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_CLIENT, 0 };
    ListMessage list = { 0 };
    OpenMessage open = { dump_interface_id };
    uint8_t encoded[64];
    size_t encoded_size;

    encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
    self->port.send_control(self->port.context, encoded, encoded_size);

    if (command == kCLIENT_COMMAND_LIST) {
        encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));
    } else {
        encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));
    }
    self->port.send_control(self->port.context, encoded, encoded_size);
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

    (void)context;
    (void)now_us;

    if (!MessageHeader_Decode(&header, data, size)) {
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
