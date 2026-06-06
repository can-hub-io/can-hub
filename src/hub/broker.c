#include "hub/broker.h"

#include <string.h>

#include "hub/domain/frame_routes.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"

#define CONTROL_BUFFER_SIZE 4096
#define PING_REPLY_FLAG 0x01
#define FRAME_CHANNEL_OFFSET (MESSAGE_HEADER_SIZE + 12)
#define FRAME_BUFFER_SIZE (MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD)

typedef void (*TControlHandler)(
    Broker *self,
    HubPeer *peer,
    const MessageHeader *header,
    const uint8_t *payload
);

static void onPeerConnected(void *context, uint32_t peer_id, const char *fingerprint_hex);
static void onPeerDisconnected(void *context, uint32_t peer_id, uint64_t now_us);
static void onPeerControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size, uint64_t now_us);
static void onPeerFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void handleHello(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleRegister(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleOpen(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleClose(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handlePing(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static bool agentIdentityAccepted(Broker *self, const HubPeer *peer, const RegisterMessage *registration);
static void detachAgent(Broker *self, uint32_t agent_peer_id);
static void sendControl(Broker *self, uint32_t peer_id, const uint8_t *encoded, size_t encoded_size);
static void forwardFrame(Broker *self, const FrameRoute *route, const uint8_t *data, size_t size);

static const TControlHandler control_handlers[kMESSAGE_TYPE_MAX] = {
    [kMESSAGE_TYPE_HELLO] = handleHello,
    [kMESSAGE_TYPE_REGISTER] = handleRegister,
    [kMESSAGE_TYPE_LIST] = handleList,
    [kMESSAGE_TYPE_OPEN] = handleOpen,
    [kMESSAGE_TYPE_CLOSE] = handleClose,
    [kMESSAGE_TYPE_PING] = handlePing,
};

/* ---------- public ---------- */

void Broker_Init(Broker *self, HubTransportPort *transport, IdentityStorePort *identity_store)
{
    memset(self, 0, sizeof(*self));
    self->transport = transport;
    self->identity_store = identity_store;
    InterfaceRegistry_Reset(&self->registry);
    PeerDirectory_Reset(&self->directory);
}

HubTransportEvents Broker_Events(Broker *self)
{
    HubTransportEvents events = {
        .context = self,
        .on_peer_connected = onPeerConnected,
        .on_peer_disconnected = onPeerDisconnected,
        .on_peer_control = onPeerControl,
        .on_peer_frame = onPeerFrame,
    };

    return events;
}

/* ---------- private: events ---------- */

static void onPeerConnected(void *context, uint32_t peer_id, const char *fingerprint_hex)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Allocate(&self->directory, peer_id);

    if (peer == NULL) {
        self->transport->close_peer(self->transport->context, peer_id);
        return;
    }

    if (fingerprint_hex != NULL) {
        strncpy(peer->fingerprint_hex, fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE - 1);
    }
}

static void onPeerDisconnected(void *context, uint32_t peer_id, uint64_t now_us)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);

    (void)now_us;

    if (peer == NULL) {
        return;
    }

    if (peer->role == kHUB_PEER_ROLE_AGENT) {
        detachAgent(self, peer_id);
    }

    PeerDirectory_Release(&self->directory, peer_id);
}

static void onPeerControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size, uint64_t now_us)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);
    MessageHeader header;

    (void)now_us;

    if (peer == NULL) {
        return;
    }
    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }
    if (header.type >= kMESSAGE_TYPE_MAX || control_handlers[header.type] == NULL) {
        return;
    }

    control_handlers[header.type](self, peer, &header, data + MESSAGE_HEADER_SIZE);
}

static void onPeerFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);
    MessageHeader header;
    FrameMessage frame;
    FrameRoute routes[FRAME_ROUTES_MAX];
    uint8_t route_count = 0;
    uint8_t i;

    if (peer == NULL || size > FRAME_BUFFER_SIZE) {
        return;
    }
    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (header.type != kMESSAGE_TYPE_FRAME || size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }
    if (!FrameMessage_Decode(&frame, data + MESSAGE_HEADER_SIZE, header.length)) {
        return;
    }

    if (peer->role == kHUB_PEER_ROLE_AGENT) {
        route_count = FrameRoutes_FromAgent(
            &self->registry,
            &self->directory,
            peer->peer_id,
            frame.channel,
            routes,
            FRAME_ROUTES_MAX
        );
    } else if (peer->role == kHUB_PEER_ROLE_CLIENT) {
        route_count = FrameRoutes_FromClient(&self->registry, peer, frame.channel, routes, FRAME_ROUTES_MAX);
    }

    for(i=0; i<route_count; i++) {
        forwardFrame(self, &routes[i], data, size);
    }
}

/* ---------- private: control handlers ---------- */

static void handleHello(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    HelloMessage hello;

    if (peer->role != kHUB_PEER_ROLE_UNKNOWN) {
        return;
    }
    if (!HelloMessage_Decode(&hello, payload, header->length)) {
        self->transport->close_peer(self->transport->context, peer->peer_id);
        return;
    }

    peer->role = hello.role == kPEER_ROLE_AGENT ? kHUB_PEER_ROLE_AGENT : kHUB_PEER_ROLE_CLIENT;
}

static void handleRegister(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    RegisterMessage registration;
    RegisterAckMessage ack;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    bool registered;

    if (peer->role != kHUB_PEER_ROLE_AGENT) {
        return;
    }
    if (!RegisterMessage_Decode(&registration, payload, header->length)) {
        self->transport->close_peer(self->transport->context, peer->peer_id);
        return;
    }

    if (!agentIdentityAccepted(self, peer, &registration)) {
        memset(&ack, 0, sizeof(ack));
        ack.status = REGISTER_STATUS_IDENTITY_MISMATCH;
        sendControl(self, peer->peer_id, encoded, RegisterAckMessage_Encode(&ack, encoded, sizeof(encoded)));
        self->transport->close_peer(self->transport->context, peer->peer_id);
        return;
    }

    registered = InterfaceRegistry_RegisterAgent(&self->registry, peer->peer_id, &registration, &ack);
    sendControl(self, peer->peer_id, encoded, RegisterAckMessage_Encode(&ack, encoded, sizeof(encoded)));

    if (!registered) {
        self->transport->close_peer(self->transport->context, peer->peer_id);
    }
}

static void handleList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    ListMessage list;
    ListReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (!ListMessage_Decode(&list, payload, header->length)) {
        return;
    }

    InterfaceRegistry_List(&self->registry, list.offset, &reply);
    sendControl(self, peer->peer_id, encoded, ListReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleOpen(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    OpenMessage open;
    OpenAckMessage ack;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    bool interface_exists;

    if (peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }
    if (!OpenMessage_Decode(&open, payload, header->length)) {
        return;
    }

    memset(&ack, 0, sizeof(ack));
    ack.interface_id = open.interface_id;
    ack.status = 1;

    interface_exists = InterfaceRegistry_FindById(&self->registry, open.interface_id) != NULL;
    if (interface_exists && ClientSession_OpenInterface(&peer->session, open.interface_id, &ack.channel)) {
        ack.status = OPEN_STATUS_OK;
    }

    sendControl(self, peer->peer_id, encoded, OpenAckMessage_Encode(&ack, encoded, sizeof(encoded)));
}

static void handleClose(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    CloseMessage close;

    (void)self;

    if (peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }
    if (!CloseMessage_Decode(&close, payload, header->length)) {
        return;
    }

    ClientSession_CloseChannel(&peer->session, close.channel);
}

static void handlePing(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    MessageHeader pong;
    uint8_t encoded[MESSAGE_HEADER_SIZE];

    (void)payload;

    if (header->flags & PING_REPLY_FLAG) {
        return;
    }

    pong.type = kMESSAGE_TYPE_PING;
    pong.flags = PING_REPLY_FLAG;
    pong.length = 0;
    MessageHeader_Encode(&pong, encoded, sizeof(encoded));
    sendControl(self, peer->peer_id, encoded, sizeof(encoded));
}

/* ---------- private: helpers ---------- */

static bool agentIdentityAccepted(Broker *self, const HubPeer *peer, const RegisterMessage *registration)
{
    char pinned[IDENTITY_FINGERPRINT_HEX_SIZE];

    if (self->identity_store == NULL || peer->fingerprint_hex[0] == '\0') {
        return true;
    }

    if (!self->identity_store->lookup(self->identity_store->context, registration->agent_name, pinned)) {
        return self->identity_store->pin(
            self->identity_store->context,
            registration->agent_name,
            peer->fingerprint_hex
        );
    }

    return strcmp(pinned, peer->fingerprint_hex) == 0;
}

static void detachAgent(Broker *self, uint32_t agent_peer_id)
{
    const InterfaceEntry *entry;
    HubPeer *peer;
    uint32_t entry_index;
    uint8_t peer_index;

    for(entry_index=0; entry_index<INTERFACE_REGISTRY_MAX; entry_index++) {
        entry = &self->registry.entries[entry_index];
        if (!entry->in_use || entry->agent_peer_id != agent_peer_id) {
            continue;
        }
        for(peer_index=0; peer_index<PEER_DIRECTORY_MAX; peer_index++) {
            peer = PeerDirectory_At(&self->directory, peer_index);
            if (peer != NULL && peer->role == kHUB_PEER_ROLE_CLIENT) {
                ClientSession_RemoveInterface(&peer->session, entry->interface_id);
            }
        }
    }

    InterfaceRegistry_RemovePeer(&self->registry, agent_peer_id);
}

static void sendControl(Broker *self, uint32_t peer_id, const uint8_t *encoded, size_t encoded_size)
{
    if (encoded_size == 0) {
        return;
    }

    self->transport->send_control(self->transport->context, peer_id, encoded, encoded_size);
}

static void forwardFrame(Broker *self, const FrameRoute *route, const uint8_t *data, size_t size)
{
    uint8_t forwarded[FRAME_BUFFER_SIZE];

    memcpy(forwarded, data, size);
    forwarded[FRAME_CHANNEL_OFFSET] = route->channel;

    self->transport->send_frame(self->transport->context, route->peer_id, forwarded, size);
}
