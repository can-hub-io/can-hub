#include "hub/broker.h"

#include <string.h>

#include "hub/domain/admin_views.h"
#include "hub/domain/frame_routes.h"
#include "hub/domain/hub_peer.h"
#include "protocol/admin_message.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"

#define CONTROL_BUFFER_SIZE 4096
#define PING_REPLY_FLAG 0x01
#define HELLO_TIMEOUT_US 5000000
#define FRAME_CHANNEL_OFFSET (MESSAGE_HEADER_SIZE + 12)
#define FRAME_BUFFER_SIZE (MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD)

typedef void (*TControlHandler)(
    Broker *self,
    HubPeer *peer,
    const MessageHeader *header,
    const uint8_t *payload
);

static void onPeerConnected(void *context, uint32_t peer_id, const char *fingerprint_hex, bool local);
static void onPeerDisconnected(void *context, uint32_t peer_id, uint64_t now_us);
static void onPeerControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size, uint64_t now_us);
static void onPeerFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void handleHello(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleRegister(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleOpen(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleClose(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handlePing(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminStatus(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminPeers(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminKick(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminPins(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminForget(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminKickPeer(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminAgents(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminClients(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminInterfaces(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void countInterfaceFrame(Broker *self, const HubPeer *peer, uint8_t channel);
static void disconnectPeer(Broker *self, uint32_t peer_id);
static bool agentIdentityAccepted(Broker *self, const HubPeer *peer, const RegisterMessage *registration);
static void detachAgent(Broker *self, uint32_t agent_peer_id);
static void sendControl(Broker *self, HubPeer *peer, const uint8_t *encoded, size_t encoded_size);
static void forwardFrame(Broker *self, const FrameRoute *route, const uint8_t *data, size_t size);
static void tickHelloDeadline(Broker *self, HubPeer *peer, uint64_t now_us);

static const TControlHandler control_handlers[kMESSAGE_TYPE_MAX] = {
    [kMESSAGE_TYPE_HELLO] = handleHello,
    [kMESSAGE_TYPE_REGISTER] = handleRegister,
    [kMESSAGE_TYPE_LIST] = handleList,
    [kMESSAGE_TYPE_OPEN] = handleOpen,
    [kMESSAGE_TYPE_CLOSE] = handleClose,
    [kMESSAGE_TYPE_PING] = handlePing,
    [kMESSAGE_TYPE_ADMIN_STATUS] = handleAdminStatus,
    [kMESSAGE_TYPE_ADMIN_PEERS] = handleAdminPeers,
    [kMESSAGE_TYPE_ADMIN_KICK] = handleAdminKick,
    [kMESSAGE_TYPE_ADMIN_PINS] = handleAdminPins,
    [kMESSAGE_TYPE_ADMIN_FORGET] = handleAdminForget,
    [kMESSAGE_TYPE_ADMIN_KICK_PEER] = handleAdminKickPeer,
    [kMESSAGE_TYPE_ADMIN_AGENTS] = handleAdminAgents,
    [kMESSAGE_TYPE_ADMIN_CLIENTS] = handleAdminClients,
    [kMESSAGE_TYPE_ADMIN_INTERFACES] = handleAdminInterfaces,
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

void Broker_Tick(Broker *self, uint64_t now_us)
{
    HubPeer *peer;
    uint8_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = PeerDirectory_At(&self->directory, i);
        if (peer != NULL && peer->role == kHUB_PEER_ROLE_UNKNOWN) {
            tickHelloDeadline(self, peer, now_us);
        }
    }
}

/* ---------- private: events ---------- */

static void onPeerConnected(void *context, uint32_t peer_id, const char *fingerprint_hex, bool local)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Allocate(&self->directory, peer_id);

    if (peer == NULL) {
        self->transport->close_peer(self->transport->context, peer_id);
        return;
    }

    peer->local = local;
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

    peer = PeerDirectory_Find(&self->directory, peer_id);
    if (peer != NULL && peer->send_failed) {
        disconnectPeer(self, peer_id);
    }
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
    if (peer->role != kHUB_PEER_ROLE_AGENT && peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }

    self->metrics.frames_received++;
    countInterfaceFrame(self, peer, frame.channel);

    if (peer->role == kHUB_PEER_ROLE_AGENT) {
        route_count = FrameRoutes_FromAgent(
            &self->registry,
            &self->directory,
            peer->peer_id,
            frame.channel,
            routes,
            FRAME_ROUTES_MAX
        );
    } else {
        route_count = FrameRoutes_FromClient(&self->registry, peer, frame.channel, routes, FRAME_ROUTES_MAX);
    }

    if (route_count == 0) {
        self->metrics.frames_unroutable++;
        return;
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

    if (!HubPeer_AdoptRole(peer, hello.role)) {
        self->transport->close_peer(self->transport->context, peer->peer_id);
    }
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
        sendControl(self, peer, encoded, RegisterAckMessage_Encode(&ack, encoded, sizeof(encoded)));
        self->transport->close_peer(self->transport->context, peer->peer_id);
        return;
    }

    registered = InterfaceRegistry_RegisterAgent(&self->registry, peer->peer_id, &registration, &ack);
    sendControl(self, peer, encoded, RegisterAckMessage_Encode(&ack, encoded, sizeof(encoded)));

    if (!registered) {
        self->transport->close_peer(self->transport->context, peer->peer_id);
        return;
    }

    HubPeer_SetAgentName(peer, registration.agent_name);
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
    sendControl(self, peer, encoded, ListReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
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

    sendControl(self, peer, encoded, OpenAckMessage_Encode(&ack, encoded, sizeof(encoded)));
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
    sendControl(self, peer, encoded, sizeof(encoded));
}

/* ---------- private: admin handlers ---------- */

static void handleAdminStatus(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminStatusReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    (void)header;
    (void)payload;

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }

    reply.peer_count = PeerDirectory_Count(&self->directory);
    reply.agent_count = PeerDirectory_CountRole(&self->directory, kHUB_PEER_ROLE_AGENT);
    reply.client_count = PeerDirectory_CountRole(&self->directory, kHUB_PEER_ROLE_CLIENT);
    reply.interface_count = InterfaceRegistry_Count(&self->registry);
    reply.frames_received = self->metrics.frames_received;
    reply.frames_forwarded = self->metrics.frames_forwarded;
    reply.frames_dropped = self->metrics.frames_dropped;
    reply.frames_unroutable = self->metrics.frames_unroutable;
    sendControl(self, peer, encoded, AdminStatusReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminPeers(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminPeersMessage request;
    AdminPeersReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminPeersMessage_Decode(&request, payload, header->length)) {
        return;
    }

    PeerDirectory_List(&self->directory, request.offset, &reply);
    sendControl(self, peer, encoded, AdminPeersReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminKick(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminKickMessage kick;
    AdminKickReplyMessage reply;
    HubPeer *agent;
    uint32_t agent_peer_id = 0;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminKickMessage_Decode(&kick, payload, header->length)) {
        return;
    }

    reply.status = ADMIN_STATUS_UNKNOWN_AGENT;
    agent = PeerDirectory_FindAgentByName(&self->directory, kick.agent_name);
    if (agent != NULL) {
        agent_peer_id = agent->peer_id;
        reply.status = ADMIN_STATUS_OK;
    }

    sendControl(self, peer, encoded, AdminKickReplyMessage_Encode(&reply, encoded, sizeof(encoded)));

    if (reply.status == ADMIN_STATUS_OK) {
        disconnectPeer(self, agent_peer_id);
    }
}

static void handleAdminKickPeer(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminKickPeerMessage kick;
    AdminKickPeerReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    bool found;

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminKickPeerMessage_Decode(&kick, payload, header->length)) {
        return;
    }

    found = PeerDirectory_Find(&self->directory, kick.peer_id) != NULL;
    reply.status = found ? ADMIN_STATUS_OK : ADMIN_STATUS_UNKNOWN_PEER;
    sendControl(self, peer, encoded, AdminKickPeerReplyMessage_Encode(&reply, encoded, sizeof(encoded)));

    if (found) {
        disconnectPeer(self, kick.peer_id);
    }
}

static void handleAdminAgents(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminAgentsMessage request;
    AdminAgentsReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminAgentsMessage_Decode(&request, payload, header->length)) {
        return;
    }

    AdminViews_Agents(&self->registry, &self->directory, request.agent_name, request.offset, &reply);
    sendControl(self, peer, encoded, AdminAgentsReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminClients(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminClientsMessage request;
    AdminClientsReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminClientsMessage_Decode(&request, payload, header->length)) {
        return;
    }

    AdminViews_Clients(&self->registry, &self->directory, request.agent_name, request.offset, &reply);
    sendControl(self, peer, encoded, AdminClientsReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminInterfaces(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminInterfacesMessage request;
    AdminInterfacesReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminInterfacesMessage_Decode(&request, payload, header->length)) {
        return;
    }

    AdminViews_Interfaces(&self->registry, &self->directory, request.offset, &reply);
    sendControl(self, peer, encoded, AdminInterfacesReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminPins(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminPinsMessage request;
    AdminPinsReplyMessage reply;
    IdentityPin pins[ADMIN_PINS_REPLY_ENTRIES_MAX];
    bool more = false;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    uint8_t i;

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminPinsMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memset(&reply, 0, sizeof(reply));
    if (self->identity_store != NULL) {
        reply.count = self->identity_store->list(
            self->identity_store->context,
            request.offset,
            pins,
            ADMIN_PINS_REPLY_ENTRIES_MAX,
            &more
        );
        for(i=0; i<reply.count; i++) {
            memcpy(reply.entries[i].agent_name, pins[i].agent_name, REGISTER_AGENT_NAME_SIZE);
            memcpy(reply.entries[i].fingerprint_hex, pins[i].fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
        }
        if (more) {
            reply.flags |= ADMIN_REPLY_FLAG_MORE;
        }
    }

    sendControl(self, peer, encoded, AdminPinsReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminForget(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminForgetMessage forget;
    AdminForgetReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminForgetMessage_Decode(&forget, payload, header->length)) {
        return;
    }

    reply.status = ADMIN_STATUS_UNKNOWN_AGENT;
    if (self->identity_store != NULL && self->identity_store->forget(self->identity_store->context, forget.agent_name)) {
        reply.status = ADMIN_STATUS_OK;
    }

    sendControl(self, peer, encoded, AdminForgetReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
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

static void disconnectPeer(Broker *self, uint32_t peer_id)
{
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);

    if (peer == NULL) {
        return;
    }

    if (peer->role == kHUB_PEER_ROLE_AGENT) {
        detachAgent(self, peer_id);
    }

    PeerDirectory_Release(&self->directory, peer_id);
    self->transport->close_peer(self->transport->context, peer_id);
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

static void sendControl(Broker *self, HubPeer *peer, const uint8_t *encoded, size_t encoded_size)
{
    if (encoded_size == 0) {
        return;
    }

    if (!self->transport->send_control(self->transport->context, peer->peer_id, encoded, encoded_size)) {
        peer->send_failed = true;
    }
}

static void forwardFrame(Broker *self, const FrameRoute *route, const uint8_t *data, size_t size)
{
    uint8_t forwarded[FRAME_BUFFER_SIZE];
    HubPeer *destination;
    bool sent;

    memcpy(forwarded, data, size);
    forwarded[FRAME_CHANNEL_OFFSET] = route->channel;

    sent = self->transport->send_frame(self->transport->context, route->peer_id, forwarded, size);
    if (sent) {
        self->metrics.frames_forwarded++;
    } else {
        self->metrics.frames_dropped++;
    }

    destination = PeerDirectory_Find(&self->directory, route->peer_id);
    if (destination != NULL) {
        if (sent) {
            destination->frames_forwarded++;
        } else {
            destination->frames_dropped++;
        }
    }
}

static void countInterfaceFrame(Broker *self, const HubPeer *peer, uint8_t channel)
{
    const InterfaceEntry *entry;
    uint32_t interface_id;

    if (peer->role == kHUB_PEER_ROLE_AGENT) {
        entry = InterfaceRegistry_FindByAgentChannel(&self->registry, peer->peer_id, channel);
        if (entry != NULL) {
            InterfaceRegistry_CountFrame(&self->registry, entry->interface_id);
        }
        return;
    }

    if (ClientSession_InterfaceForChannel(&peer->session, channel, &interface_id)) {
        InterfaceRegistry_CountFrame(&self->registry, interface_id);
    }
}

static void tickHelloDeadline(Broker *self, HubPeer *peer, uint64_t now_us)
{
    if (peer->hello_deadline_us == 0) {
        peer->hello_deadline_us = now_us + HELLO_TIMEOUT_US;
        return;
    }

    if (now_us >= peer->hello_deadline_us) {
        disconnectPeer(self, peer->peer_id);
    }
}
