#include "hub/broker.h"

#include <string.h>

#include "hub/domain/admin_views.h"
#include "hub/domain/frame_routes.h"
#include "hub/domain/hub_peer.h"
#include "protocol/admin_message.h"
#include "protocol/error_message.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/interface_status_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "protocol/subscribe_message.h"

#define CONTROL_BUFFER_SIZE 4096
#define PING_REPLY_FLAG 0x01
#define HELLO_TIMEOUT_US 5000000
#define FRAME_ROUTE_TOKEN_VALUES_MAX 63
#define FRAME_CHANNEL_OFFSET (MESSAGE_HEADER_SIZE + 12)
#define FRAME_ROUTE_FLAGS_OFFSET (MESSAGE_HEADER_SIZE + 15)
#define FRAME_BUFFER_SIZE (MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD)

typedef void (*TControlHandler)(
    Broker *self,
    HubPeer *peer,
    const MessageHeader *header,
    const uint8_t *payload
);

static void onPeerConnected(void *context, uint32_t peer_id, const HubPeerConnectInfo *info, uint64_t now_us);
static void onPeerDisconnected(void *context, uint32_t peer_id, uint64_t now_us);
static void onPeerControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size, uint64_t now_us);
static void onPeerFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void onPeerWritable(void *context, uint32_t peer_id);
static void handleHello(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleRegister(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleOpen(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleClose(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleSubscribe(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
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
static void handleAdminPinAdd(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminAclSet(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminAclRevoke(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminAclList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminIfconfig(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleIfconfigReply(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleInterfaceStatus(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static bool rememberPendingIfconfig(Broker *self, uint32_t admin_peer_id, uint32_t agent_peer_id, const char *interface_name);
static bool takePendingIfconfig(Broker *self, uint32_t agent_peer_id, const char *interface_name, uint32_t *admin_peer_id);
static void releasePendingIfconfig(Broker *self, uint32_t peer_id);
static uint8_t adminIfconfigStatus(uint8_t agent_status);
static void countInterfaceFrame(Broker *self, const HubPeer *peer, uint8_t channel);
static void disconnectPeer(Broker *self, uint32_t peer_id);
static bool displaceGhostPeer(Broker *self, const HubPeer *peer, const RegisterMessage *registration);
static uint8_t agentIdentityStatus(Broker *self, const HubPeer *peer, const RegisterMessage *registration);
static void detachAgent(Broker *self, uint32_t agent_peer_id);
static void sendControl(Broker *self, HubPeer *peer, const uint8_t *encoded, size_t encoded_size);
static void sendError(Broker *self, uint32_t peer_id, uint16_t code, const char *detail);
static void forwardFrame(Broker *self, const FrameRoute *route, uint32_t can_id, const uint8_t *data, size_t size, uint8_t route_flags);
static void enqueueFrame(Broker *self, HubPeer *peer, uint8_t channel, const uint8_t *data, size_t size);
static void drainPeer(Broker *self, HubPeer *peer);
static void countForwarded(Broker *self, HubPeer *peer, uint8_t channel);
static void countDropped(Broker *self, HubPeer *peer, uint8_t channel);
static uint8_t injectionToken(Broker *self, const HubPeer *sender);
static bool clientCanRead(Broker *self, const HubPeer *peer, const InterfaceEntry *entry);
static bool clientCanWrite(Broker *self, const HubPeer *peer, const InterfaceEntry *entry);
static uint32_t echoOriginatorPeerId(Broker *self, uint8_t route_flags);
static void tickHelloDeadline(Broker *self, HubPeer *peer, uint64_t now_us);

static const TControlHandler control_handlers[kMESSAGE_TYPE_MAX] = {
    [kMESSAGE_TYPE_HELLO] = handleHello,
    [kMESSAGE_TYPE_REGISTER] = handleRegister,
    [kMESSAGE_TYPE_LIST] = handleList,
    [kMESSAGE_TYPE_OPEN] = handleOpen,
    [kMESSAGE_TYPE_CLOSE] = handleClose,
    [kMESSAGE_TYPE_SUBSCRIBE] = handleSubscribe,
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
    [kMESSAGE_TYPE_ADMIN_PIN_ADD] = handleAdminPinAdd,
    [kMESSAGE_TYPE_ADMIN_ACL_SET] = handleAdminAclSet,
    [kMESSAGE_TYPE_ADMIN_ACL_REVOKE] = handleAdminAclRevoke,
    [kMESSAGE_TYPE_ADMIN_ACL_LIST] = handleAdminAclList,
    [kMESSAGE_TYPE_ADMIN_IFCONFIG] = handleAdminIfconfig,
    [kMESSAGE_TYPE_IFCONFIG_REPLY] = handleIfconfigReply,
    [kMESSAGE_TYPE_INTERFACE_STATUS] = handleInterfaceStatus,
};

/* ---------- public ---------- */

void Broker_Init(
    Broker *self,
    HubTransportPort *transport,
    IdentityStorePort *identity_store,
    AuthorizationPort *authorization,
    bool require_known_agents
)
{
    memset(self, 0, sizeof(*self));
    self->transport = transport;
    self->identity_store = identity_store;
    self->authorization = authorization;
    self->require_known_agents = require_known_agents;
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
        .on_peer_writable = onPeerWritable,
    };

    return events;
}

void Broker_Tick(Broker *self, uint64_t now_us)
{
    HubPeer *peer;
    uint8_t i;

    self->now_us = now_us;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = PeerDirectory_At(&self->directory, i);
        if (peer == NULL) {
            continue;
        }
        if (peer->role == kHUB_PEER_ROLE_UNKNOWN) {
            tickHelloDeadline(self, peer, now_us);
        }
        if (EgressQueue_HasPending(&peer->egress)) {
            drainPeer(self, peer);
        }
    }
}

/* ---------- private: events ---------- */

static void onPeerConnected(void *context, uint32_t peer_id, const HubPeerConnectInfo *info, uint64_t now_us)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Allocate(&self->directory, peer_id);

    self->now_us = now_us;

    if (peer == NULL) {
        sendError(self, peer_id, kERROR_CODE_HUB_FULL, "no peer slot available");
        self->transport->close_peer(self->transport->context, peer_id);
        return;
    }

    peer->local = info->local;
    peer->transport_kind = info->transport_kind;
    peer->connected_at_us = now_us;
    if (info->fingerprint_hex != NULL) {
        strncpy(peer->fingerprint_hex, info->fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE - 1);
    }
    if (info->origin != NULL) {
        strncpy(peer->origin, info->origin, HUB_PEER_ORIGIN_SIZE - 1);
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

    releasePendingIfconfig(self, peer_id);
    PeerDirectory_Release(&self->directory, peer_id);
}

static void onPeerControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size, uint64_t now_us)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);
    MessageHeader header;

    self->now_us = now_us;

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
    uint32_t echo_originator_peer_id = 0;
    uint8_t forwarded_route_flags = 0;
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
        forwarded_route_flags = frame.route_flags & (FRAME_ROUTE_FLAG_BRIDGED | FRAME_ROUTE_FLAG_ECHO);
        echo_originator_peer_id = echoOriginatorPeerId(self, frame.route_flags);
    } else {
        if (!ClientSession_CanWrite(&peer->session, frame.channel)) {
            self->metrics.frames_dropped++;
            return;
        }
        route_count = FrameRoutes_FromClient(&self->registry, peer, frame.channel, routes, FRAME_ROUTES_MAX);
        forwarded_route_flags = (uint8_t)(injectionToken(self, peer) << FRAME_ROUTE_TOKEN_SHIFT);
    }

    if (route_count == 0) {
        self->metrics.frames_unroutable++;
        return;
    }

    for(i=0; i<route_count; i++) {
        if (routes[i].suppress_echo && routes[i].peer_id == echo_originator_peer_id) {
            continue;
        }
        forwardFrame(self, &routes[i], frame.can_id, data, size, forwarded_route_flags);
    }
}

static void onPeerWritable(void *context, uint32_t peer_id)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);

    if (peer != NULL) {
        drainPeer(self, peer);
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
        sendError(self, peer->peer_id, kERROR_CODE_MALFORMED, "malformed hello");
        disconnectPeer(self, peer->peer_id);
        return;
    }

    if (!HubPeer_AdoptRole(peer, hello.role)) {
        sendError(self, peer->peer_id, kERROR_CODE_ROLE_REJECTED, "admin role requires a local transport");
        disconnectPeer(self, peer->peer_id);
        return;
    }

    if (peer->role == kHUB_PEER_ROLE_CLIENT && hello.name[0] != '\0') {
        HubPeer_SetAgentName(peer, hello.name);
    }
}

static void handleRegister(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    RegisterMessage registration;
    RegisterAckMessage ack;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    uint8_t identity_status;
    bool registered;

    if (peer->role != kHUB_PEER_ROLE_AGENT) {
        return;
    }
    if (!RegisterMessage_Decode(&registration, payload, header->length)) {
        sendError(self, peer->peer_id, kERROR_CODE_MALFORMED, "malformed register");
        disconnectPeer(self, peer->peer_id);
        return;
    }

    identity_status = agentIdentityStatus(self, peer, &registration);
    if (identity_status != REGISTER_STATUS_OK) {
        memset(&ack, 0, sizeof(ack));
        ack.status = identity_status;
        sendControl(self, peer, encoded, RegisterAckMessage_Encode(&ack, encoded, sizeof(encoded)));
        sendError(
            self,
            peer->peer_id,
            kERROR_CODE_ROLE_REJECTED,
            identity_status == REGISTER_STATUS_UNKNOWN_AGENT
                ? "agent fingerprint not in the hub allowlist"
                : "agent name pinned to a different fingerprint"
        );
        disconnectPeer(self, peer->peer_id);
        return;
    }

    registered = InterfaceRegistry_RegisterAgent(&self->registry, peer->peer_id, &registration, &ack);
    if (!registered && displaceGhostPeer(self, peer, &registration)) {
        registered = InterfaceRegistry_RegisterAgent(&self->registry, peer->peer_id, &registration, &ack);
    }
    sendControl(self, peer, encoded, RegisterAckMessage_Encode(&ack, encoded, sizeof(encoded)));

    if (!registered) {
        disconnectPeer(self, peer->peer_id);
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
    const InterfaceEntry *entry;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    bool suppress_echo;
    bool can_write;

    if (peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }
    if (!OpenMessage_Decode(&open, payload, header->length)) {
        return;
    }

    memset(&ack, 0, sizeof(ack));
    ack.interface_id = open.interface_id;
    ack.status = OPEN_STATUS_REJECTED;

    entry = InterfaceRegistry_FindById(&self->registry, open.interface_id);
    if (entry == NULL) {
        sendControl(self, peer, encoded, OpenAckMessage_Encode(&ack, encoded, sizeof(encoded)));
        return;
    }

    if (!clientCanRead(self, peer, entry)) {
        ack.status = OPEN_STATUS_READ_DENIED;
        sendControl(self, peer, encoded, OpenAckMessage_Encode(&ack, encoded, sizeof(encoded)));
        sendError(self, peer->peer_id, kERROR_CODE_ROLE_REJECTED, "not authorized to read this interface");
        return;
    }

    can_write = clientCanWrite(self, peer, entry);
    if ((open.flags & OPEN_FLAG_WANT_WRITE) && !can_write) {
        ack.status = OPEN_STATUS_WRITE_DENIED;
        sendControl(self, peer, encoded, OpenAckMessage_Encode(&ack, encoded, sizeof(encoded)));
        sendError(self, peer->peer_id, kERROR_CODE_ROLE_REJECTED, "not authorized to inject on this interface");
        return;
    }

    suppress_echo = (open.flags & OPEN_FLAG_SUPPRESS_OWN_ECHO) != 0;
    if (ClientSession_OpenInterface(&peer->session, open.interface_id, suppress_echo, can_write, &ack.channel)) {
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

static void handleSubscribe(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    SubscribeMessage subscribe;

    if (peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }
    if (!SubscribeMessage_Decode(&subscribe, payload, header->length)) {
        sendError(self, peer->peer_id, kERROR_CODE_MALFORMED, "malformed subscribe");
        return;
    }
    if (!ClientSession_SetFilters(&peer->session, subscribe.channel, subscribe.filters, subscribe.filter_count)) {
        sendError(self, peer->peer_id, kERROR_CODE_MALFORMED, "subscribe on unopened channel");
    }
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

    PeerDirectory_List(&self->directory, request.offset, &reply, self->now_us);
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
        sendError(self, agent_peer_id, kERROR_CODE_KICKED, "kicked by the hub administrator");
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
        sendError(self, kick.peer_id, kERROR_CODE_KICKED, "kicked by the hub administrator");
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

static void handleAdminPinAdd(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminPinAddMessage request;
    AdminPinAddReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    bool added = false;

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminPinAddMessage_Decode(&request, payload, header->length)) {
        return;
    }

    if (self->identity_store != NULL) {
        added = self->identity_store->pin(self->identity_store->context, request.agent_name, request.fingerprint_hex);
    }
    reply.status = added ? ADMIN_STATUS_OK : ADMIN_STATUS_PIN_FAILED;
    sendControl(self, peer, encoded, AdminPinAddReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminAclSet(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminAclSetMessage request;
    AdminAclSetReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    bool granted = false;

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminAclSetMessage_Decode(&request, payload, header->length)) {
        return;
    }

    if (self->authorization != NULL) {
        granted = self->authorization->grant(
            self->authorization->context,
            request.fingerprint_hex,
            request.agent_name,
            request.interface_name,
            request.can_read != 0,
            request.can_write != 0
        );
    }
    reply.status = granted ? ADMIN_STATUS_OK : ADMIN_STATUS_PIN_FAILED;
    sendControl(self, peer, encoded, AdminAclSetReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminAclRevoke(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminAclRevokeMessage request;
    AdminAclRevokeReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    bool revoked = false;

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminAclRevokeMessage_Decode(&request, payload, header->length)) {
        return;
    }

    if (self->authorization != NULL) {
        revoked = self->authorization->revoke(
            self->authorization->context,
            request.fingerprint_hex,
            request.agent_name,
            request.interface_name
        );
    }
    reply.status = revoked ? ADMIN_STATUS_OK : ADMIN_STATUS_UNKNOWN_AGENT;
    sendControl(self, peer, encoded, AdminAclRevokeReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminAclList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminAclListMessage request;
    AdminAclListReplyMessage reply;
    AclEntry entries[ADMIN_ACL_LIST_REPLY_ENTRIES_MAX];
    bool more = false;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    uint8_t i;

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminAclListMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memset(&reply, 0, sizeof(reply));
    if (self->authorization != NULL) {
        reply.count = self->authorization->list(
            self->authorization->context,
            request.offset,
            entries,
            ADMIN_ACL_LIST_REPLY_ENTRIES_MAX,
            &more
        );
        for(i=0; i<reply.count; i++) {
            memcpy(reply.entries[i].agent_name, entries[i].agent_name, REGISTER_AGENT_NAME_SIZE);
            memcpy(reply.entries[i].interface_name, entries[i].interface_name, REGISTER_INTERFACE_NAME_SIZE);
            memcpy(reply.entries[i].fingerprint_hex, entries[i].fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
            reply.entries[i].can_read = entries[i].can_read ? 1 : 0;
            reply.entries[i].can_write = entries[i].can_write ? 1 : 0;
        }
        if (more) {
            reply.flags |= ADMIN_REPLY_FLAG_MORE;
        }
    }

    sendControl(self, peer, encoded, AdminAclListReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
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

static void handleAdminIfconfig(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminIfconfigMessage request;
    AdminIfconfigReplyMessage reply;
    IfconfigMessage forward;
    const InterfaceEntry *entry;
    HubPeer *agent;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminIfconfigMessage_Decode(&request, payload, header->length)) {
        return;
    }

    entry = InterfaceRegistry_FindByName(&self->registry, request.agent_name, request.interface_name);
    if (entry == NULL) {
        reply.status = ADMIN_IFCONFIG_STATUS_UNKNOWN_INTERFACE;
        sendControl(self, peer, encoded, AdminIfconfigReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
        return;
    }

    reply.status = ADMIN_IFCONFIG_STATUS_AGENT_UNREACHABLE;
    agent = PeerDirectory_Find(&self->directory, entry->agent_peer_id);
    if (agent == NULL || agent->role != kHUB_PEER_ROLE_AGENT) {
        sendControl(self, peer, encoded, AdminIfconfigReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
        return;
    }
    if (!rememberPendingIfconfig(self, peer->peer_id, entry->agent_peer_id, entry->interface_name)) {
        sendControl(self, peer, encoded, AdminIfconfigReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
        return;
    }

    memset(&forward, 0, sizeof(forward));
    memcpy(forward.interface_name, entry->interface_name, REGISTER_INTERFACE_NAME_SIZE);
    forward.op = request.op;
    forward.bitrate = request.bitrate;
    sendControl(self, agent, encoded, IfconfigMessage_Encode(&forward, encoded, sizeof(encoded)));
}

static void handleIfconfigReply(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    IfconfigReplyMessage reply;
    AdminIfconfigReplyMessage admin_reply;
    HubPeer *admin;
    uint32_t admin_peer_id;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_AGENT) {
        return;
    }
    if (!IfconfigReplyMessage_Decode(&reply, payload, header->length)) {
        return;
    }
    if (!takePendingIfconfig(self, peer->peer_id, reply.interface_name, &admin_peer_id)) {
        return;
    }

    admin = PeerDirectory_Find(&self->directory, admin_peer_id);
    if (admin == NULL) {
        return;
    }

    admin_reply.status = adminIfconfigStatus(reply.status);
    sendControl(self, admin, encoded, AdminIfconfigReplyMessage_Encode(&admin_reply, encoded, sizeof(encoded)));
}

static void handleInterfaceStatus(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    InterfaceStatusMessage status;
    uint8_t i;

    if (peer->role != kHUB_PEER_ROLE_AGENT) {
        return;
    }
    if (!InterfaceStatusMessage_Decode(&status, payload, header->length)) {
        return;
    }

    for(i=0; i<status.interface_count; i++) {
        InterfaceRegistry_SetTxDropped(
            &self->registry,
            peer->peer_id,
            status.entries[i].channel,
            status.entries[i].tx_dropped
        );
    }
}

/* ---------- private: helpers ---------- */

static uint8_t agentIdentityStatus(Broker *self, const HubPeer *peer, const RegisterMessage *registration)
{
    char pinned[IDENTITY_FINGERPRINT_HEX_SIZE];

    if (self->identity_store == NULL || peer->fingerprint_hex[0] == '\0') {
        return REGISTER_STATUS_OK;
    }

    if (!self->identity_store->lookup(self->identity_store->context, registration->agent_name, pinned)) {
        if (self->require_known_agents) {
            return REGISTER_STATUS_UNKNOWN_AGENT;
        }
        if (!self->identity_store->pin(
                self->identity_store->context,
                registration->agent_name,
                peer->fingerprint_hex
            )) {
            return REGISTER_STATUS_REJECTED;
        }
        return REGISTER_STATUS_OK;
    }

    return strcmp(pinned, peer->fingerprint_hex) == 0 ? REGISTER_STATUS_OK : REGISTER_STATUS_IDENTITY_MISMATCH;
}

static bool rememberPendingIfconfig(Broker *self, uint32_t admin_peer_id, uint32_t agent_peer_id, const char *interface_name)
{
    uint8_t i;

    for(i=0; i<BROKER_PENDING_IFCONFIG_MAX; i++) {
        if (self->pending_ifconfig[i].in_use) {
            continue;
        }
        self->pending_ifconfig[i].in_use = true;
        self->pending_ifconfig[i].admin_peer_id = admin_peer_id;
        self->pending_ifconfig[i].agent_peer_id = agent_peer_id;
        memcpy(self->pending_ifconfig[i].interface_name, interface_name, REGISTER_INTERFACE_NAME_SIZE);
        return true;
    }

    return false;
}

static bool takePendingIfconfig(Broker *self, uint32_t agent_peer_id, const char *interface_name, uint32_t *admin_peer_id)
{
    uint8_t i;

    for(i=0; i<BROKER_PENDING_IFCONFIG_MAX; i++) {
        if (!self->pending_ifconfig[i].in_use) {
            continue;
        }
        if (self->pending_ifconfig[i].agent_peer_id != agent_peer_id) {
            continue;
        }
        if (strncmp(self->pending_ifconfig[i].interface_name, interface_name, REGISTER_INTERFACE_NAME_SIZE) != 0) {
            continue;
        }
        *admin_peer_id = self->pending_ifconfig[i].admin_peer_id;
        self->pending_ifconfig[i].in_use = false;
        return true;
    }

    return false;
}

static void releasePendingIfconfig(Broker *self, uint32_t peer_id)
{
    AdminIfconfigReplyMessage reply;
    HubPeer *admin;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    uint8_t i;

    for(i=0; i<BROKER_PENDING_IFCONFIG_MAX; i++) {
        if (!self->pending_ifconfig[i].in_use) {
            continue;
        }
        if (self->pending_ifconfig[i].agent_peer_id == peer_id) {
            admin = PeerDirectory_Find(&self->directory, self->pending_ifconfig[i].admin_peer_id);
            if (admin != NULL) {
                reply.status = ADMIN_IFCONFIG_STATUS_AGENT_UNREACHABLE;
                sendControl(self, admin, encoded, AdminIfconfigReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
            }
            self->pending_ifconfig[i].in_use = false;
        } else if (self->pending_ifconfig[i].admin_peer_id == peer_id) {
            self->pending_ifconfig[i].in_use = false;
        }
    }
}

static uint8_t adminIfconfigStatus(uint8_t agent_status)
{
    if (agent_status == IFCONFIG_STATUS_OK) {
        return ADMIN_IFCONFIG_STATUS_OK;
    }
    if (agent_status == IFCONFIG_STATUS_UNKNOWN_INTERFACE) {
        return ADMIN_IFCONFIG_STATUS_UNKNOWN_INTERFACE;
    }

    return ADMIN_IFCONFIG_STATUS_APPLY_FAILED;
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

    releasePendingIfconfig(self, peer_id);
    PeerDirectory_Release(&self->directory, peer_id);
    self->transport->close_peer(self->transport->context, peer_id);
}

static bool displaceGhostPeer(Broker *self, const HubPeer *peer, const RegisterMessage *registration)
{
    HubPeer *ghost;
    uint32_t ghost_peer_id;

    if (peer->fingerprint_hex[0] == '\0') {
        return false;
    }
    if (!InterfaceRegistry_CollidingPeer(&self->registry, registration, &ghost_peer_id)) {
        return false;
    }
    if (ghost_peer_id == peer->peer_id) {
        return false;
    }

    ghost = PeerDirectory_Find(&self->directory, ghost_peer_id);
    if (ghost == NULL || strcmp(ghost->fingerprint_hex, peer->fingerprint_hex) != 0) {
        return false;
    }

    disconnectPeer(self, ghost_peer_id);

    return true;
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

static void sendError(Broker *self, uint32_t peer_id, uint16_t code, const char *detail)
{
    ErrorMessage error;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    size_t encoded_size;

    memset(&error, 0, sizeof(error));
    error.code = code;
    strncpy(error.detail, detail, ERROR_DETAIL_SIZE - 1);
    encoded_size = ErrorMessage_Encode(&error, encoded, sizeof(encoded));

    self->transport->send_control(self->transport->context, peer_id, encoded, encoded_size);
}

static void forwardFrame(Broker *self, const FrameRoute *route, uint32_t can_id, const uint8_t *data, size_t size, uint8_t route_flags)
{
    uint8_t forwarded[FRAME_BUFFER_SIZE];
    HubPeer *destination;

    destination = PeerDirectory_Find(&self->directory, route->peer_id);
    if (destination != NULL
        && destination->role == kHUB_PEER_ROLE_CLIENT
        && !ClientSession_ChannelAccepts(&destination->session, route->channel, can_id)) {
        return;
    }

    memcpy(forwarded, data, size);
    forwarded[FRAME_CHANNEL_OFFSET] = route->channel;
    forwarded[FRAME_ROUTE_FLAGS_OFFSET] = route_flags;

    if (destination == NULL) {
        if (self->transport->send_frame(self->transport->context, route->peer_id, forwarded, size)) {
            self->metrics.frames_forwarded++;
        } else {
            self->metrics.frames_dropped++;
        }
        return;
    }

    if (EgressQueue_ChannelPending(&destination->egress, route->channel)) {
        enqueueFrame(self, destination, route->channel, forwarded, size);
        return;
    }

    if (self->transport->send_frame(self->transport->context, route->peer_id, forwarded, size)) {
        countForwarded(self, destination, route->channel);
        return;
    }

    enqueueFrame(self, destination, route->channel, forwarded, size);
}

static void enqueueFrame(Broker *self, HubPeer *peer, uint8_t channel, const uint8_t *data, size_t size)
{
    uint8_t evicted_channel = 0;
    TEGRESS_PUSH_RESULT result;

    result = EgressQueue_Push(&peer->egress, channel, data, (uint16_t)size, &evicted_channel);
    if (result != kEGRESS_PUSH_QUEUED) {
        countDropped(self, peer, evicted_channel);
    }
}

static void drainPeer(Broker *self, HubPeer *peer)
{
    const uint8_t *front;
    uint16_t size;
    uint8_t channel;

    while (EgressQueue_NextPendingChannel(&peer->egress, &channel)) {
        front = EgressQueue_FrontOfChannel(&peer->egress, channel, &size);
        if (front == NULL) {
            continue;
        }
        if (!self->transport->send_frame(self->transport->context, peer->peer_id, front, size)) {
            return;
        }
        EgressQueue_PopChannel(&peer->egress, channel);
        countForwarded(self, peer, channel);
    }
}

static void countForwarded(Broker *self, HubPeer *peer, uint8_t channel)
{
    ChannelBinding *binding;

    self->metrics.frames_forwarded++;
    peer->frames_forwarded++;
    if (peer->role == kHUB_PEER_ROLE_CLIENT) {
        binding = ClientSession_BindingForChannel(&peer->session, channel);
        if (binding != NULL) {
            binding->frames_forwarded++;
        }
    }
}

static void countDropped(Broker *self, HubPeer *peer, uint8_t channel)
{
    ChannelBinding *binding;

    self->metrics.frames_dropped++;
    peer->frames_dropped++;
    if (peer->role == kHUB_PEER_ROLE_CLIENT) {
        binding = ClientSession_BindingForChannel(&peer->session, channel);
        if (binding != NULL) {
            binding->frames_dropped++;
        }
    }
}

static bool clientCanRead(Broker *self, const HubPeer *peer, const InterfaceEntry *entry)
{
    if (peer->fingerprint_hex[0] == '\0') {
        return true;
    }
    if (self->authorization == NULL) {
        return true;
    }

    return self->authorization->read_allowed(
        self->authorization->context,
        peer->fingerprint_hex,
        entry->agent_name,
        entry->interface_name
    );
}

static bool clientCanWrite(Broker *self, const HubPeer *peer, const InterfaceEntry *entry)
{
    if (peer->fingerprint_hex[0] == '\0') {
        return true;
    }
    if (self->authorization == NULL) {
        return true;
    }

    return self->authorization->write_allowed(
        self->authorization->context,
        peer->fingerprint_hex,
        entry->agent_name,
        entry->interface_name
    );
}

static uint8_t injectionToken(Broker *self, const HubPeer *sender)
{
    uint8_t slot = PeerDirectory_SlotOf(&self->directory, sender);

    if (slot >= FRAME_ROUTE_TOKEN_VALUES_MAX) {
        return FRAME_ROUTE_NO_TOKEN;
    }

    return (uint8_t)(slot + 1);
}

static uint32_t echoOriginatorPeerId(Broker *self, uint8_t route_flags)
{
    HubPeer *originator;
    uint8_t token = (uint8_t)((route_flags & FRAME_ROUTE_TOKEN_MASK) >> FRAME_ROUTE_TOKEN_SHIFT);

    if ((route_flags & FRAME_ROUTE_FLAG_ECHO) == 0 || token == FRAME_ROUTE_NO_TOKEN) {
        return 0;
    }

    originator = PeerDirectory_At(&self->directory, (uint8_t)(token - 1));
    if (originator == NULL) {
        return 0;
    }

    return originator->peer_id;
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
        sendError(self, peer->peer_id, kERROR_CODE_HELLO_TIMEOUT, "no hello within the deadline");
        disconnectPeer(self, peer->peer_id);
    }
}
