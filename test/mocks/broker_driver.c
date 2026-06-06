#include "broker_driver.h"

#include "protocol/hello_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"

#define DRIVER_BUFFER_SIZE 512
#define DRIVER_LIST_PEER 999

static void sendHello(const HubTransportEvents *events, uint32_t peer_id, uint8_t role);

/* ---------- public ---------- */

void BrokerDriver_ConnectAgent(
    const HubTransportEvents *events,
    HubTransportPortMock *transport,
    uint32_t peer_id,
    const RegisterMessage *registration
)
{
    uint8_t encoded[DRIVER_BUFFER_SIZE];
    size_t encoded_size;

    events->on_peer_connected(events->context, peer_id, NULL);
    sendHello(events, peer_id, kPEER_ROLE_AGENT);
    encoded_size = RegisterMessage_Encode(registration, encoded, sizeof(encoded));
    events->on_peer_control(events->context, peer_id, encoded, encoded_size, 0);

    transport->control_count = 0;
}

void BrokerDriver_ConnectClient(const HubTransportEvents *events, uint32_t peer_id)
{
    events->on_peer_connected(events->context, peer_id, NULL);
    sendHello(events, peer_id, kPEER_ROLE_CLIENT);
}

uint32_t BrokerDriver_InterfaceIdAt(const HubTransportEvents *events, HubTransportPortMock *transport, uint8_t index)
{
    ListMessage list = { 0 };
    ListReplyMessage reply;
    MessageHeader header;
    uint8_t encoded[DRIVER_BUFFER_SIZE];
    size_t encoded_size;
    int reply_index;

    BrokerDriver_ConnectClient(events, DRIVER_LIST_PEER);
    encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));
    events->on_peer_control(events->context, DRIVER_LIST_PEER, encoded, encoded_size, 0);

    reply_index = transport->control_count - 1;
    MessageHeader_Decode(&header, transport->control_log[reply_index], transport->control_sizes[reply_index]);
    ListReplyMessage_Decode(&reply, transport->control_log[reply_index] + MESSAGE_HEADER_SIZE, header.length);

    events->on_peer_disconnected(events->context, DRIVER_LIST_PEER, 0);
    transport->control_count = 0;

    return reply.entries[index].interface_id;
}

uint8_t BrokerDriver_OpenInterface(
    const HubTransportEvents *events,
    HubTransportPortMock *transport,
    uint32_t peer_id,
    uint32_t interface_id
)
{
    OpenMessage open = { interface_id };
    OpenAckMessage ack;
    MessageHeader header;
    uint8_t encoded[DRIVER_BUFFER_SIZE];
    size_t encoded_size;
    int reply_index;

    encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));
    events->on_peer_control(events->context, peer_id, encoded, encoded_size, 0);

    reply_index = transport->control_count - 1;
    MessageHeader_Decode(&header, transport->control_log[reply_index], transport->control_sizes[reply_index]);
    OpenAckMessage_Decode(&ack, transport->control_log[reply_index] + MESSAGE_HEADER_SIZE, header.length);

    transport->control_count = 0;

    return ack.channel;
}

/* ---------- private ---------- */

static void sendHello(const HubTransportEvents *events, uint32_t peer_id, uint8_t role)
{
    HelloMessage hello = { PROTOCOL_VERSION, role, 0 };
    uint8_t encoded[DRIVER_BUFFER_SIZE];
    size_t encoded_size;

    encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
    events->on_peer_control(events->context, peer_id, encoded, encoded_size, 0);
}
