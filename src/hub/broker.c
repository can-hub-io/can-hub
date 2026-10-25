#include "hub/broker.h"

#include <assert.h>
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
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"
#include "shared/bounded_string.h"

#define CONTROL_BUFFER_SIZE 4096
#define ADMIN_PAGE_WIRE_SIZE(fixed, entries, entry) (MESSAGE_HEADER_SIZE + (fixed) + (entries) * (entry))

static_assert(
    CONTROL_BUFFER_SIZE >= ADMIN_PAGE_WIRE_SIZE(ADMIN_PEERS_REPLY_FIXED_FIELDS_SIZE, ADMIN_PEERS_REPLY_ENTRIES_MAX, ADMIN_PEERS_REPLY_ENTRY_SIZE),
    "a full ADMIN_PEERS_REPLY page exceeds the control buffer"
);
static_assert(
    CONTROL_BUFFER_SIZE >= ADMIN_PAGE_WIRE_SIZE(ADMIN_TXNS_REPLY_FIXED_FIELDS_SIZE, ADMIN_TXNS_REPLY_ENTRIES_MAX, ADMIN_TXNS_REPLY_ENTRY_SIZE),
    "a full ADMIN_TXNS_REPLY page exceeds the control buffer"
);
static_assert(
    CONTROL_BUFFER_SIZE >= ADMIN_PAGE_WIRE_SIZE(ADMIN_REJECTED_REPLY_FIXED_FIELDS_SIZE, ADMIN_REJECTED_REPLY_ENTRIES_MAX, ADMIN_REJECTED_REPLY_ENTRY_SIZE),
    "a full ADMIN_REJECTED_REPLY page exceeds the control buffer"
);
#define PING_REPLY_FLAG 0x01
#define HELLO_TIMEOUT_US 5000000
#define AWAITING_HELLO_MAX (PEER_DIRECTORY_MAX / 2)
#define AWAITING_HELLO_PER_HOST_MAX 4
#define FIRST_CONTACT_PINS_MAX (2 * PEER_DIRECTORY_MAX)
#define FRAME_PAYLOAD_LENGTH_OFFSET (MESSAGE_HEADER_SIZE + 13)
#define FRAME_BUFFER_SIZE (MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD)
#define FRAME_PACE_OVERHEAD_BITS 64
#define FRAME_PACE_BITS_PER_BYTE 10
#define LIVE_EGRESS_MAX_AGE_US 50000

typedef void (*TControlHandler)(
    Broker *self,
    HubPeer *peer,
    const MessageHeader *header,
    const uint8_t *payload
);

static void onPeerConnected(void *context, uint32_t peer_id, const HubPeerConnectInfo *info, uint64_t now_us);
static bool admitsPeerAwaitingHello(Broker *self, const HubPeerConnectInfo *info);
static bool makeRoomForKnownPeer(Broker *self, const HubPeerConnectInfo *info);
static bool connectingPeerIsKnown(Broker *self, const HubPeerConnectInfo *info);
static void onPeerDisconnected(void *context, uint32_t peer_id, uint64_t now_us);
static void onPeerControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size, uint64_t now_us);
static bool onPeerFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
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
static void handleAdminTxns(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminPinAdd(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminAclSet(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminAclRevoke(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminAclList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleConfigure(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminRejected(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminBridgeSet(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminBridgeDelete(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminBridgeList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static uint8_t applyBridgeRoute(Broker *self, const BridgeRoute *route);
static uint8_t removeBridgeRoute(Broker *self, const BridgeEndpoint *source, const BridgeEndpoint *destination);
static void fillBridgeEntry(Broker *self, const BridgeRouteSlot *slot, AdminBridgeListReplyEntry *entry);
static bool endpointOnline(Broker *self, const BridgeEndpoint *endpoint);
static void handleAdminGroupSet(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminGroupDelete(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminGroupJoin(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminGroupLeave(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleAdminGroupList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static uint8_t applyGroup(Broker *self, const AdminGroupSetMessage *request);
static uint8_t removeGroup(Broker *self, const char *name);
static uint8_t joinGroup(Broker *self, const char *name, const BridgeGroupMember *member);
static uint8_t leaveGroup(Broker *self, const char *name, const BridgeEndpoint *member);
static void fillGroupEntry(Broker *self, const BridgeGroupSlot *slot, AdminGroupListReplyEntry *entry);
static void sendGroupReply(Broker *self, HubPeer *peer, uint8_t type, uint8_t status);
static void loadBridgeRoutes(Broker *self);
static void loadBridgeGroups(Broker *self);
static bool bridgesConfigured(Broker *self);
static bool isGroupEndpoint(const BridgeEndpoint *endpoint);
static bool channelRefusesFrame(Broker *self, const HubPeer *client, const FrameMessage *frame);
static void openGroupInterface(Broker *self, const char *name);
static void closeGroupInterface(Broker *self, const char *name);
static BridgeGroupSlot *groupOfChannel(Broker *self, const HubPeer *client, uint8_t channel, const InterfaceEntry **entry);
static void writeIntoGroup(Broker *self, const HubPeer *writer, BridgeGroupSlot *slot, const InterfaceEntry *entry, const FrameMessage *frame);
static uint16_t showToGroups(Broker *self, const BridgeEndpoint *origin, const FrameMessage *frame, uint32_t echo_originator_peer_id, uint8_t route_flags);
static uint16_t showToGroup(Broker *self, const InterfaceEntry *entry, const FrameMessage *frame, uint32_t echo_originator_peer_id, uint8_t route_flags);
static uint16_t bridgeFrame(Broker *self, const HubPeer *source_agent, const FrameMessage *frame, uint32_t echo_originator_peer_id, uint8_t route_flags);
static void bridgeInto(Broker *self, const BridgeTarget *target, const FrameMessage *frame);
static void handleIfconfigReply(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleInterfaceStatus(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void relayPaceToClients(Broker *self, uint32_t interface_id, uint32_t pace_rate);
static bool rememberPendingIfconfig(Broker *self, uint32_t requester_peer_id, const InterfaceEntry *entry, uint32_t *request_id);
static bool takePendingIfconfig(Broker *self, uint32_t agent_peer_id, uint32_t request_id, PendingIfconfig *taken);
static void learnFdFromSettings(Broker *self, uint32_t interface_id, const InterfaceSettings *settings);
static void releasePendingIfconfig(Broker *self, uint32_t peer_id);
static uint8_t configureStatus(uint8_t agent_status);
static void replyConfigure(Broker *self, HubPeer *peer, uint8_t status);
static bool peerCanConfigure(Broker *self, const HubPeer *peer, const InterfaceEntry *entry);
static void countInterfaceFrame(Broker *self, const HubPeer *peer, uint8_t channel);
static void disconnectPeer(Broker *self, uint32_t peer_id);
static void releasePeer(Broker *self, HubPeer *peer);
static void closeClientChannel(Broker *self, HubPeer *client, uint8_t channel);
static void removeClientInterface(Broker *self, HubPeer *client, uint32_t interface_id);
static void releaseReliableBindings(Broker *self, HubPeer *client);
static void revertAgentChannelIfUnused(Broker *self, uint32_t interface_id);
static bool displaceGhostPeer(Broker *self, const HubPeer *peer, const RegisterMessage *registration);
static uint8_t agentIdentityStatus(Broker *self, const HubPeer *peer, const RegisterMessage *registration);
static bool agentIsEnrolled(Broker *self, const HubPeer *peer);
static bool firstContactPinsFull(Broker *self);
static bool makeRoomForInterfaces(Broker *self, uint8_t interface_count);
static bool registryHasRoomFor(const Broker *self, uint8_t interface_count);
static bool clientIsNamedByAcl(Broker *self, const HubPeer *peer);
static bool clientIsAdmitted(Broker *self, const HubPeer *peer);
static void rejectClient(Broker *self, HubPeer *peer);
static void rejectAgent(Broker *self, HubPeer *peer, const RegisterMessage *registration, uint8_t identity_status);
static void recordRejection(Broker *self, const HubPeer *peer, const char *name, uint8_t reason);
static const char *unknownAgentDetail(const Broker *self);
static void detachAgent(Broker *self, uint32_t agent_peer_id);
static void reattachClientsToAgent(Broker *self, uint32_t agent_peer_id);
static void detachInterface(Broker *self, const InterfaceEntry *entry);
static void reattachInterface(Broker *self, const InterfaceEntry *entry);
static void sendControl(Broker *self, HubPeer *peer, const uint8_t *encoded, size_t encoded_size);
static void sendError(Broker *self, uint32_t peer_id, uint16_t code, const char *detail);
static bool routeFrame(Broker *self, HubPeer *peer, const FrameMessage *frame);
static bool reliableRoutesFit(
    Broker *self,
    const FrameRoute *routes,
    uint16_t route_count,
    const FrameMessage *frame,
    uint32_t echo_originator_peer_id
);
static bool routeTakesFrame(
    Broker *self,
    const FrameRoute *route,
    const FrameMessage *frame,
    uint32_t echo_originator_peer_id
);
static size_t frameWireSize(const FrameMessage *frame);
static bool forwardFrame(Broker *self, const FrameRoute *route, const FrameMessage *frame, uint8_t route_flags);
static bool disconnectSlowConsumer(Broker *self, const HubPeer *destination);
static uint64_t frameWireBits(const uint8_t *wire, uint16_t size);
static void enqueueFrame(Broker *self, HubPeer *peer, uint8_t channel, const uint8_t *data, size_t size);
static void drainPeer(Broker *self, HubPeer *peer);
static bool liveFrameTooOld(const Broker *self, const HubPeer *peer, uint8_t channel);
static void countForwarded(Broker *self, HubPeer *peer, uint8_t channel);
static void countDropped(Broker *self, HubPeer *peer, uint8_t channel);
static uint8_t injectionToken(Broker *self, const HubPeer *sender, const FrameRoute *routes, uint16_t route_count);
static void releaseEchoTokens(Broker *self, uint32_t client_peer_id);
static bool clientCanRead(Broker *self, const HubPeer *peer, const InterfaceEntry *entry);
static bool clientCanWrite(Broker *self, const HubPeer *peer, const InterfaceEntry *entry);
static bool peerCanConfigure(Broker *self, const HubPeer *peer, const InterfaceEntry *entry)
{
    if (peer->role == kHUB_PEER_ROLE_ADMIN) {
        return true;
    }
    if (peer->fingerprint_hex[0] == '\0') {
        return true;
    }
    if (self->authorization == NULL) {
        return true;
    }

    return self->authorization->admin_allowed(
        self->authorization->context,
        peer->fingerprint_hex,
        entry->agent_name,
        entry->interface_name
    );
}

static bool reliableEndpointsSupport(Broker *self, const HubPeer *client, const InterfaceEntry *entry);
static void handleTxnBegin(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleTxnBeginAck(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleTxnContent(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleTxnProgress(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleTxnStatus(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleTxnAbort(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static void handleTxnRosterReport(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload);
static uint8_t admitTransaction(Broker *self, const HubPeer *client, const TxnBeginMessage *begin, RelayedTransaction *request);
static void replyTxnBegin(Broker *self, HubPeer *client, uint8_t channel, uint8_t status);
static bool isTransactionContent(uint8_t type);
static bool relayTransactionContent(Broker *self, HubPeer *client, const MessageHeader *header, const uint8_t *payload);
static bool agentRefusesTemplate(const HubPeer *agent, const MessageHeader *header, const uint8_t *payload);
static void refuseTransaction(Broker *self, RelayedTransaction *transaction, HubPeer *agent, uint8_t reason);
static void sendTxnStatus(Broker *self, HubPeer *client, const RelayedTransaction *transaction, uint8_t reason);
static RelayedTransaction *activeTransactionOfAgent(Broker *self, const HubPeer *agent, uint16_t txn_id);
static void relayTransactionMessage(
    Broker *self,
    HubPeer *destination,
    const MessageHeader *header,
    const uint8_t *payload,
    uint8_t channel
);
static void sendTxnAbort(Broker *self, HubPeer *agent, uint8_t channel, uint16_t txn_id);
static void finishTransaction(Broker *self, RelayedTransaction *transaction);
static void abandonTransaction(Broker *self, RelayedTransaction *transaction);
static void abandonClientTransactions(Broker *self, uint32_t client_peer_id);
static void failAgentTransaction(Broker *self, uint32_t agent_peer_id);
static void announceLease(Broker *self, uint32_t interface_id);
static bool leasedToAnother(const Broker *self, uint32_t interface_id, uint32_t client_peer_id);
static bool channelLeasedToAnother(const Broker *self, const HubPeer *client, uint8_t channel);
static bool endpointLeasedToAnother(const Broker *self, const BridgeEndpoint *endpoint, uint32_t client_peer_id);
static void tickHelloDeadline(Broker *self, HubPeer *peer, uint64_t now_us);

static const TControlHandler control_handlers[kMESSAGE_TYPE_MAX] = {
    [kMESSAGE_TYPE_HELLO] = handleHello,
    [kMESSAGE_TYPE_REGISTER] = handleRegister,
    [kMESSAGE_TYPE_LIST] = handleList,
    [kMESSAGE_TYPE_OPEN] = handleOpen,
    [kMESSAGE_TYPE_CLOSE] = handleClose,
    [kMESSAGE_TYPE_SUBSCRIBE] = handleSubscribe,
    [kMESSAGE_TYPE_PING] = handlePing,
    [kMESSAGE_TYPE_CONFIGURE] = handleConfigure,
    [kMESSAGE_TYPE_ADMIN_STATUS] = handleAdminStatus,
    [kMESSAGE_TYPE_ADMIN_PEERS] = handleAdminPeers,
    [kMESSAGE_TYPE_ADMIN_KICK] = handleAdminKick,
    [kMESSAGE_TYPE_ADMIN_PINS] = handleAdminPins,
    [kMESSAGE_TYPE_ADMIN_FORGET] = handleAdminForget,
    [kMESSAGE_TYPE_ADMIN_KICK_PEER] = handleAdminKickPeer,
    [kMESSAGE_TYPE_ADMIN_AGENTS] = handleAdminAgents,
    [kMESSAGE_TYPE_ADMIN_CLIENTS] = handleAdminClients,
    [kMESSAGE_TYPE_ADMIN_INTERFACES] = handleAdminInterfaces,
    [kMESSAGE_TYPE_ADMIN_TXNS] = handleAdminTxns,
    [kMESSAGE_TYPE_ADMIN_PIN_ADD] = handleAdminPinAdd,
    [kMESSAGE_TYPE_ADMIN_ACL_SET] = handleAdminAclSet,
    [kMESSAGE_TYPE_ADMIN_ACL_REVOKE] = handleAdminAclRevoke,
    [kMESSAGE_TYPE_ADMIN_ACL_LIST] = handleAdminAclList,
    [kMESSAGE_TYPE_ADMIN_REJECTED] = handleAdminRejected,
    [kMESSAGE_TYPE_ADMIN_BRIDGE_SET] = handleAdminBridgeSet,
    [kMESSAGE_TYPE_ADMIN_BRIDGE_DELETE] = handleAdminBridgeDelete,
    [kMESSAGE_TYPE_ADMIN_BRIDGE_LIST] = handleAdminBridgeList,
    [kMESSAGE_TYPE_ADMIN_GROUP_SET] = handleAdminGroupSet,
    [kMESSAGE_TYPE_ADMIN_GROUP_DELETE] = handleAdminGroupDelete,
    [kMESSAGE_TYPE_ADMIN_GROUP_JOIN] = handleAdminGroupJoin,
    [kMESSAGE_TYPE_ADMIN_GROUP_LEAVE] = handleAdminGroupLeave,
    [kMESSAGE_TYPE_ADMIN_GROUP_LIST] = handleAdminGroupList,
    [kMESSAGE_TYPE_IFCONFIG_REPLY] = handleIfconfigReply,
    [kMESSAGE_TYPE_INTERFACE_STATUS] = handleInterfaceStatus,
    [kMESSAGE_TYPE_TXN_BEGIN] = handleTxnBegin,
    [kMESSAGE_TYPE_TXN_BEGIN_ACK] = handleTxnBeginAck,
    [kMESSAGE_TYPE_TXN_TEMPLATE] = handleTxnContent,
    [kMESSAGE_TYPE_TXN_STEPS] = handleTxnContent,
    [kMESSAGE_TYPE_TXN_PROGRESS] = handleTxnProgress,
    [kMESSAGE_TYPE_TXN_STATUS] = handleTxnStatus,
    [kMESSAGE_TYPE_TXN_ABORT] = handleTxnAbort,
    [kMESSAGE_TYPE_TXN_ROSTER] = handleTxnContent,
    [kMESSAGE_TYPE_TXN_ROSTER_REPORT] = handleTxnRosterReport,
};

/* ---------- public ---------- */

void Broker_Init(
    Broker *self,
    HubTransportPort *transport,
    IdentityStorePort *identity_store,
    AuthorizationPort *authorization,
    BridgeStorePort *bridge_store,
    bool require_known_peers
)
{
    memset(self, 0, sizeof(*self));
    self->transport = transport;
    self->identity_store = identity_store;
    self->authorization = authorization;
    self->bridge_store = bridge_store;
    self->require_known_peers = require_known_peers;
    InterfaceRegistry_Reset(&self->registry);
    PeerDirectory_Reset(&self->directory);
    RejectedPeers_Reset(&self->rejected_peers);
    BridgeRoutes_Reset(&self->bridges);
    BridgeGroups_Reset(&self->groups);
    TransactionTable_Reset(&self->transactions);
    loadBridgeRoutes(self);
    loadBridgeGroups(self);
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
    uint16_t i;

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

int32_t Broker_NextTimeoutMs(Broker *self, int32_t cap_ms)
{
    uint64_t best_us = (uint64_t)cap_ms * 1000;
    uint64_t delay;
    const uint8_t *front;
    uint16_t size;
    uint16_t channel;
    HubPeer *peer;
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = PeerDirectory_At(&self->directory, i);
        if (peer == NULL || !EgressQueue_HasPending(&peer->egress)) {
            continue;
        }
        for(channel=0; channel<EGRESS_QUEUE_CHANNELS_MAX; channel++) {
            if (!EgressQueue_ChannelPending(&peer->egress, (uint8_t)channel)) {
                continue;
            }
            front = EgressQueue_FrontOfChannel(&peer->egress, (uint8_t)channel, &size);
            if (front == NULL) {
                continue;
            }
            delay = InterfaceRegistry_EgressDelayUs(&self->registry, peer->peer_id, (uint8_t)channel, frameWireBits(front, size), self->now_us);
            if (delay > 0 && delay < best_us) {
                best_us = delay;
            }
        }
    }

    return (int32_t)((best_us + 999) / 1000);
}

/* ---------- private: events ---------- */

static void onPeerConnected(void *context, uint32_t peer_id, const HubPeerConnectInfo *info, uint64_t now_us)
{
    Broker *self = context;
    HubPeer *peer;

    self->now_us = now_us;

    if (!admitsPeerAwaitingHello(self, info)) {
        sendError(self, peer_id, kERROR_CODE_HUB_FULL, "too many peers awaiting hello");
        self->transport->close_peer(self->transport->context, peer_id);
        return;
    }

    if (PeerDirectory_IsFull(&self->directory) && !makeRoomForKnownPeer(self, info)) {
        sendError(self, peer_id, kERROR_CODE_HUB_FULL, "no peer slot available");
        self->transport->close_peer(self->transport->context, peer_id);
        return;
    }

    peer = PeerDirectory_Allocate(&self->directory, peer_id);
    peer->local = info->local;
    peer->transport_kind = info->transport_kind;
    peer->connected_at_us = now_us;
    if (info->fingerprint_hex != NULL) {
        BoundedString_Copy(peer->fingerprint_hex, sizeof(peer->fingerprint_hex), info->fingerprint_hex);
    }
    if (info->origin != NULL) {
        BoundedString_Copy(peer->origin, sizeof(peer->origin), info->origin);
    }
    if (info->origin_host != NULL) {
        BoundedString_Copy(peer->origin_host, sizeof(peer->origin_host), info->origin_host);
    }
}

static bool admitsPeerAwaitingHello(Broker *self, const HubPeerConnectInfo *info)
{
    if (info->local) {
        return true;
    }
    if (PeerDirectory_CountRole(&self->directory, kHUB_PEER_ROLE_UNKNOWN) >= AWAITING_HELLO_MAX) {
        return false;
    }
    if (info->origin_host == NULL) {
        return true;
    }

    return PeerDirectory_CountAwaitingHelloFrom(&self->directory, info->origin_host) < AWAITING_HELLO_PER_HOST_MAX;
}

static bool makeRoomForKnownPeer(Broker *self, const HubPeerConnectInfo *info)
{
    HubPeer *displaced;

    if (!connectingPeerIsKnown(self, info)) {
        return false;
    }
    displaced = PeerDirectory_FindDisplaceable(&self->directory);
    if (displaced == NULL) {
        return false;
    }

    sendError(self, displaced->peer_id, kERROR_CODE_HUB_FULL, "displaced by a known peer");
    disconnectPeer(self, displaced->peer_id);

    return true;
}

static bool connectingPeerIsKnown(Broker *self, const HubPeerConnectInfo *info)
{
    if (info->local || info->fingerprint_hex == NULL) {
        return true;
    }
    if (self->authorization != NULL && self->authorization->knows(self->authorization->context, info->fingerprint_hex)) {
        return true;
    }

    return self->identity_store != NULL && self->identity_store->knows(self->identity_store->context, info->fingerprint_hex);
}

static void onPeerDisconnected(void *context, uint32_t peer_id, uint64_t now_us)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);

    (void)now_us;

    if (peer == NULL) {
        return;
    }

    releasePeer(self, peer);
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

static bool onPeerFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    Broker *self = context;
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);
    FrameStream stream;
    FrameMessage frame;
    MessageHeader header;
    bool accepted = true;

    if (peer == NULL) {
        return true;
    }
    if (peer->role != kHUB_PEER_ROLE_AGENT && peer->role != kHUB_PEER_ROLE_CLIENT) {
        return true;
    }
    if (MessageHeader_Decode(&header, data, size) && isTransactionContent(header.type)) {
        if (size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
            return true;
        }
        return relayTransactionContent(self, peer, &header, data + MESSAGE_HEADER_SIZE);
    }

    FrameStream_Init(&stream, data, size);
    while (FrameStream_Next(&stream, &frame)) {
        if (!routeFrame(self, peer, &frame)) {
            accepted = false;
        }
    }

    return accepted;
}

static bool routeFrame(Broker *self, HubPeer *peer, const FrameMessage *frame)
{
    FrameRoute *routes = self->frame_routes;
    uint32_t echo_originator_peer_id = 0;
    uint16_t route_count = 0;
    uint16_t routes_dropped = 0;
    uint8_t forwarded_route_flags = 0;
    uint16_t bridged_count = 0;
    BridgeGroupSlot *group;
    const InterfaceEntry *group_entry = NULL;
    uint8_t token;
    bool accepted = true;
    bool bridged = false;
    uint16_t i;

    self->metrics.frames_received++;
    countInterfaceFrame(self, peer, frame->channel);

    if (peer->role == kHUB_PEER_ROLE_AGENT) {
        route_count = FrameRoutes_FromAgent(
            &self->registry,
            &self->directory,
            peer->peer_id,
            frame->channel,
            routes,
            FRAME_ROUTES_MAX,
            &routes_dropped
        );
        forwarded_route_flags = frame->route_flags & (FRAME_ROUTE_FLAG_BRIDGED | FRAME_ROUTE_FLAG_ECHO);
        echo_originator_peer_id = EchoTokens_Originator(&peer->echo_tokens, frame->route_flags);
        bridged = echo_originator_peer_id == ECHO_TOKENS_BRIDGE_OWNER || (frame->route_flags & FRAME_ROUTE_FLAG_BRIDGED) != 0;
        if (bridged) {
            forwarded_route_flags |= FRAME_ROUTE_FLAG_BRIDGED;
        }
    } else {
        if (!ClientSession_CanWrite(&peer->session, frame->channel) || (frame->can_id & FRAME_CAN_ID_FLAG_ERR)) {
            self->metrics.frames_dropped++;
            return true;
        }
        if (channelLeasedToAnother(self, peer, frame->channel)) {
            self->metrics.transactions.lease_frame_drops++;
            return true;
        }
        group = groupOfChannel(self, peer, frame->channel, &group_entry);
        if (group != NULL) {
            writeIntoGroup(self, peer, group, group_entry, frame);
            return true;
        }
        if (channelRefusesFrame(self, peer, frame)) {
            self->metrics.frames_dropped++;
            return true;
        }
        route_count = FrameRoutes_FromClient(
            &self->registry, peer, frame->channel, routes, FRAME_ROUTES_MAX, &routes_dropped);
        token = injectionToken(self, peer, routes, route_count);
        forwarded_route_flags = (uint8_t)(token << FRAME_ROUTE_TOKEN_SHIFT);
    }

    self->metrics.frames_dropped += routes_dropped;

    if (peer->role != kHUB_PEER_ROLE_AGENT && !reliableRoutesFit(self, routes, route_count, frame, echo_originator_peer_id)) {
        return false;
    }

    for(i=0; i<route_count; i++) {
        if (!routeTakesFrame(self, &routes[i], frame, echo_originator_peer_id)) {
            continue;
        }
        if (!forwardFrame(self, &routes[i], frame, forwarded_route_flags)) {
            accepted = false;
        }
    }

    if (peer->role == kHUB_PEER_ROLE_AGENT && !bridged && bridgesConfigured(self)) {
        bridged_count = bridgeFrame(self, peer, frame, echo_originator_peer_id, forwarded_route_flags);
    }
    if (route_count == 0 && bridged_count == 0) {
        self->metrics.frames_unroutable++;
    }

    return accepted;
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

    peer->capabilities = hello.capabilities;

    if (peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }
    if (hello.name[0] != '\0') {
        HubPeer_SetAgentName(peer, hello.name);
    }
    peer->vouched = clientIsNamedByAcl(self, peer);
    if (!clientIsAdmitted(self, peer)) {
        rejectClient(self, peer);
        return;
    }

    RejectedPeers_Forget(&self->rejected_peers, peer);
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

    if (strcmp(registration.agent_name, BRIDGE_GROUP_AGENT_NAME) == 0) {
        sendError(self, peer->peer_id, kERROR_CODE_ROLE_REJECTED, "the agent name group is reserved for bridge groups");
        disconnectPeer(self, peer->peer_id);
        return;
    }

    identity_status = agentIdentityStatus(self, peer, &registration);
    if (identity_status != REGISTER_STATUS_OK) {
        rejectAgent(self, peer, &registration, identity_status);
        return;
    }

    peer->vouched = agentIsEnrolled(self, peer);
    registered = InterfaceRegistry_RegisterAgent(&self->registry, peer->peer_id, &registration, &ack);
    if (!registered && displaceGhostPeer(self, peer, &registration)) {
        registered = InterfaceRegistry_RegisterAgent(&self->registry, peer->peer_id, &registration, &ack);
    }
    if (!registered && peer->vouched && makeRoomForInterfaces(self, registration.interface_count)) {
        registered = InterfaceRegistry_RegisterAgent(&self->registry, peer->peer_id, &registration, &ack);
    }
    sendControl(self, peer, encoded, RegisterAckMessage_Encode(&ack, encoded, sizeof(encoded)));

    if (!registered) {
        disconnectPeer(self, peer->peer_id);
        return;
    }

    HubPeer_SetAgentName(peer, registration.agent_name);
    RejectedPeers_Forget(&self->rejected_peers, peer);
    reattachClientsToAgent(self, peer->peer_id);
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
    ChannelOpenRequest request;
    bool can_write;
    bool reliable;

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

    reliable = (open.flags & OPEN_FLAG_RELIABLE) != 0;
    if (reliable && !reliableEndpointsSupport(self, peer, entry)) {
        ack.status = OPEN_STATUS_RELIABLE_UNSUPPORTED;
        sendControl(self, peer, encoded, OpenAckMessage_Encode(&ack, encoded, sizeof(encoded)));
        return;
    }

    request.interface_id = open.interface_id;
    request.suppress_echo = (open.flags & OPEN_FLAG_SUPPRESS_OWN_ECHO) != 0;
    request.can_write = can_write;
    request.reliable = reliable;
    request.error_frames = (open.flags & OPEN_FLAG_ERROR_FRAMES) != 0;
    request.agent_name = entry->agent_name;
    request.interface_name = entry->interface_name;
    if (ClientSession_OpenInterface(&peer->session, &request, &ack.channel)) {
        ack.status = OPEN_STATUS_OK;
        if (reliable) {
            self->transport->set_channel_mode(self->transport->context, entry->agent_peer_id, entry->agent_channel, true);
        }
    }

    sendControl(self, peer, encoded, OpenAckMessage_Encode(&ack, encoded, sizeof(encoded)));
    if (ack.status == OPEN_STATUS_OK && leasedToAnother(self, entry->interface_id, peer->peer_id)) {
        announceLease(self, entry->interface_id);
    }
}

static void handleClose(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    CloseMessage close;

    if (peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }
    if (!CloseMessage_Decode(&close, payload, header->length)) {
        return;
    }

    closeClientChannel(self, peer, close.channel);
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
    reply.receive_overflows = self->transport->receive_overflows(self->transport->context);
    reply.frames_received = self->metrics.frames_received;
    reply.frames_forwarded = self->metrics.frames_forwarded;
    reply.frames_dropped = self->metrics.frames_dropped;
    reply.frames_unroutable = self->metrics.frames_unroutable;
    reply.transactions = self->metrics.transactions;
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

static void handleAdminTxns(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminTxnsMessage request;
    AdminTxnsReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminTxnsMessage_Decode(&request, payload, header->length)) {
        return;
    }

    AdminViews_Transactions(&self->registry, &self->transactions, self->now_us, request.offset, &reply);
    sendControl(self, peer, encoded, AdminTxnsReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
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
        added = self->identity_store->enroll(self->identity_store->context, request.agent_name, request.fingerprint_hex);
    }
    reply.status = added ? ADMIN_STATUS_OK : ADMIN_STATUS_PIN_FAILED;
    sendControl(self, peer, encoded, AdminPinAddReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminAclSet(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminAclSetMessage request;
    AdminAclSetReplyMessage reply;
    AclEntry entry;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    bool granted = false;

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminAclSetMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memcpy(entry.fingerprint_hex, request.fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
    memcpy(entry.agent_name, request.agent_name, REGISTER_AGENT_NAME_SIZE);
    memcpy(entry.interface_name, request.interface_name, REGISTER_INTERFACE_NAME_SIZE);
    entry.can_admin = request.can_admin != 0;
    entry.can_write = request.can_write != 0 || entry.can_admin;
    entry.can_read = request.can_read != 0 || entry.can_write;
    if (self->authorization != NULL) {
        granted = self->authorization->grant(self->authorization->context, &entry);
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
        if (reply.count > ADMIN_ACL_LIST_REPLY_ENTRIES_MAX) {
            reply.count = ADMIN_ACL_LIST_REPLY_ENTRIES_MAX;
        }
        for(i=0; i<reply.count; i++) {
            memcpy(reply.entries[i].agent_name, entries[i].agent_name, REGISTER_AGENT_NAME_SIZE);
            memcpy(reply.entries[i].interface_name, entries[i].interface_name, REGISTER_INTERFACE_NAME_SIZE);
            memcpy(reply.entries[i].fingerprint_hex, entries[i].fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
            reply.entries[i].can_read = entries[i].can_read ? 1 : 0;
            reply.entries[i].can_write = entries[i].can_write ? 1 : 0;
            reply.entries[i].can_admin = entries[i].can_admin ? 1 : 0;
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
        if (reply.count > ADMIN_PINS_REPLY_ENTRIES_MAX) {
            reply.count = ADMIN_PINS_REPLY_ENTRIES_MAX;
        }
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

static void handleConfigure(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    ConfigureMessage request;
    IfconfigMessage forward;
    const InterfaceEntry *entry;
    HubPeer *agent;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN && peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }
    if (!ConfigureMessage_Decode(&request, payload, header->length)) {
        return;
    }

    entry = InterfaceRegistry_FindByName(&self->registry, request.agent_name, request.interface_name);
    if (entry == NULL || InterfaceEntry_IsVirtual(entry)) {
        replyConfigure(self, peer, CONFIGURE_STATUS_UNKNOWN_INTERFACE);
        return;
    }
    if (!peerCanConfigure(self, peer, entry)) {
        replyConfigure(self, peer, CONFIGURE_STATUS_DENIED);
        return;
    }

    agent = PeerDirectory_Find(&self->directory, entry->agent_peer_id);
    if (agent == NULL || agent->role != kHUB_PEER_ROLE_AGENT) {
        replyConfigure(self, peer, CONFIGURE_STATUS_AGENT_UNREACHABLE);
        return;
    }

    memset(&forward, 0, sizeof(forward));
    if (!rememberPendingIfconfig(self, peer->peer_id, entry, &forward.request_id)) {
        replyConfigure(self, peer, CONFIGURE_STATUS_AGENT_UNREACHABLE);
        return;
    }

    memcpy(forward.interface_name, entry->interface_name, REGISTER_INTERFACE_NAME_SIZE);
    forward.op = request.op;
    forward.settings = request.settings;
    sendControl(self, agent, encoded, IfconfigMessage_Encode(&forward, encoded, sizeof(encoded)));
}

static void handleAdminRejected(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminRejectedMessage request;
    AdminRejectedReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminRejectedMessage_Decode(&request, payload, header->length)) {
        return;
    }

    RejectedPeers_List(&self->rejected_peers, request.offset, &reply, self->now_us);
    sendControl(self, peer, encoded, AdminRejectedReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminBridgeSet(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminBridgeSetMessage request;
    AdminBridgeSetReplyMessage reply;
    BridgeRoute route;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminBridgeSetMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memset(&route, 0, sizeof(route));
    memcpy(&route.source, &request.source, sizeof(route.source));
    memcpy(&route.destination, &request.destination, sizeof(route.destination));
    route.filter_count = request.filter_count;
    memcpy(route.filters, request.filters, sizeof(route.filters));

    reply.status = applyBridgeRoute(self, &route);
    sendControl(self, peer, encoded, AdminBridgeSetReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminBridgeDelete(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminBridgeDeleteMessage request;
    AdminBridgeDeleteReplyMessage reply;
    BridgeEndpoint source;
    BridgeEndpoint destination;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminBridgeDeleteMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memcpy(&source, &request.source, sizeof(source));
    memcpy(&destination, &request.destination, sizeof(destination));

    reply.status = removeBridgeRoute(self, &source, &destination);
    sendControl(self, peer, encoded, AdminBridgeDeleteReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void handleAdminBridgeList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminBridgeListMessage request;
    AdminBridgeListReplyMessage reply;
    const BridgeRouteSlot *slot;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN) {
        return;
    }
    if (!AdminBridgeListMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memset(&reply, 0, sizeof(reply));
    while (reply.count < ADMIN_BRIDGE_LIST_REPLY_ENTRIES_MAX) {
        slot = BridgeRoutes_Nth(&self->bridges, (uint16_t)(request.offset + reply.count));
        if (slot == NULL) {
            break;
        }
        fillBridgeEntry(self, slot, &reply.entries[reply.count]);
        reply.count++;
    }
    if (BridgeRoutes_Nth(&self->bridges, (uint16_t)(request.offset + reply.count)) != NULL) {
        reply.flags |= ADMIN_REPLY_FLAG_MORE;
    }

    sendControl(self, peer, encoded, AdminBridgeListReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static uint8_t applyBridgeRoute(Broker *self, const BridgeRoute *route)
{
    if (!BridgeRoute_IsValid(route) || isGroupEndpoint(&route->source) || isGroupEndpoint(&route->destination)) {
        return ADMIN_BRIDGE_STATUS_INVALID;
    }
    if (!BridgeRoutes_HasRoom(&self->bridges, route)) {
        return ADMIN_BRIDGE_STATUS_FULL;
    }
    if (self->bridge_store != NULL && !self->bridge_store->save(self->bridge_store->context, route)) {
        return ADMIN_BRIDGE_STATUS_STORE_FAILED;
    }

    BridgeRoutes_Set(&self->bridges, route);

    return ADMIN_BRIDGE_STATUS_OK;
}

static uint8_t removeBridgeRoute(Broker *self, const BridgeEndpoint *source, const BridgeEndpoint *destination)
{
    if (BridgeRoutes_Find(&self->bridges, source, destination) == NULL) {
        return ADMIN_BRIDGE_STATUS_UNKNOWN;
    }
    if (self->bridge_store != NULL && !self->bridge_store->remove(self->bridge_store->context, source, destination)) {
        return ADMIN_BRIDGE_STATUS_STORE_FAILED;
    }

    BridgeRoutes_Remove(&self->bridges, source, destination);

    return ADMIN_BRIDGE_STATUS_OK;
}

static void fillBridgeEntry(Broker *self, const BridgeRouteSlot *slot, AdminBridgeListReplyEntry *entry)
{
    memset(entry, 0, sizeof(*entry));
    memcpy(&entry->source, &slot->route.source, sizeof(entry->source));
    memcpy(&entry->destination, &slot->route.destination, sizeof(entry->destination));
    entry->filter_count = slot->route.filter_count;
    memcpy(entry->filters, slot->route.filters, sizeof(entry->filters));
    entry->forwarded = slot->forwarded;
    entry->dropped = slot->dropped;
    if (endpointOnline(self, &slot->route.source)) {
        entry->state |= ADMIN_BRIDGE_STATE_SOURCE_ONLINE;
    }
    if (endpointOnline(self, &slot->route.destination)) {
        entry->state |= ADMIN_BRIDGE_STATE_DESTINATION_ONLINE;
    }
}

static bool endpointOnline(Broker *self, const BridgeEndpoint *endpoint)
{
    return InterfaceRegistry_FindByName(&self->registry, endpoint->agent_name, endpoint->interface_name) != NULL;
}

static void loadBridgeRoutes(Broker *self)
{
    BridgeRoute route;
    uint16_t position = 0;

    if (self->bridge_store == NULL) {
        return;
    }

    while (position < BRIDGE_ROUTES_MAX && self->bridge_store->load(self->bridge_store->context, position, &route)) {
        BridgeRoutes_Set(&self->bridges, &route);
        position++;
    }
}

static void handleAdminGroupSet(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminGroupSetMessage request;

    if (peer->role != kHUB_PEER_ROLE_ADMIN || !AdminGroupSetMessage_Decode(&request, payload, header->length)) {
        return;
    }

    sendGroupReply(self, peer, kMESSAGE_TYPE_ADMIN_GROUP_SET_REPLY, applyGroup(self, &request));
}

static void handleAdminGroupDelete(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminGroupDeleteMessage request;

    if (peer->role != kHUB_PEER_ROLE_ADMIN || !AdminGroupDeleteMessage_Decode(&request, payload, header->length)) {
        return;
    }

    sendGroupReply(self, peer, kMESSAGE_TYPE_ADMIN_GROUP_DELETE_REPLY, removeGroup(self, request.name));
}

static void handleAdminGroupJoin(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminGroupMemberMessage request;
    BridgeGroupMember member;

    if (peer->role != kHUB_PEER_ROLE_ADMIN || !AdminGroupMemberMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memset(&member, 0, sizeof(member));
    memcpy(&member.endpoint, &request.member, sizeof(member.endpoint));
    member.direction = request.direction;
    member.filter_count = request.filter_count;
    memcpy(member.filters, request.filters, sizeof(member.filters));
    sendGroupReply(self, peer, kMESSAGE_TYPE_ADMIN_GROUP_JOIN_REPLY, joinGroup(self, request.name, &member));
}

static void handleAdminGroupLeave(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminGroupMemberMessage request;
    BridgeEndpoint member;

    if (peer->role != kHUB_PEER_ROLE_ADMIN || !AdminGroupMemberMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memcpy(&member, &request.member, sizeof(member));
    sendGroupReply(self, peer, kMESSAGE_TYPE_ADMIN_GROUP_LEAVE_REPLY, leaveGroup(self, request.name, &member));
}

static void handleAdminGroupList(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    AdminGroupListMessage request;
    AdminGroupListReplyMessage reply;
    const BridgeGroupSlot *slot;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_ADMIN || !AdminGroupListMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memset(&reply, 0, sizeof(reply));
    slot = BridgeGroups_Nth(&self->groups, request.offset);
    if (slot != NULL) {
        fillGroupEntry(self, slot, &reply.entries[0]);
        reply.count = ADMIN_GROUP_LIST_REPLY_ENTRIES_MAX;
    }
    if (BridgeGroups_Nth(&self->groups, (uint16_t)(request.offset + reply.count)) != NULL) {
        reply.flags |= ADMIN_REPLY_FLAG_MORE;
    }

    sendControl(self, peer, encoded, AdminGroupListReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static uint8_t applyGroup(Broker *self, const AdminGroupSetMessage *request)
{
    BridgeGroup group;

    if (!BridgeGroup_IsNameValid(request->name)) {
        return ADMIN_BRIDGE_STATUS_INVALID;
    }
    if (!BridgeGroups_HasRoom(&self->groups, request->name)) {
        return ADMIN_BRIDGE_STATUS_FULL;
    }
    if (BridgeGroups_Find(&self->groups, request->name) == NULL
        && InterfaceRegistry_Count(&self->registry) >= INTERFACE_REGISTRY_MAX) {
        return ADMIN_BRIDGE_STATUS_FULL;
    }

    memset(&group, 0, sizeof(group));
    memcpy(group.name, request->name, sizeof(group.name));
    group.filter_count = request->filter_count;
    memcpy(group.filters, request->filters, sizeof(group.filters));
    if (self->bridge_store != NULL && !self->bridge_store->save_group(self->bridge_store->context, &group)) {
        return ADMIN_BRIDGE_STATUS_STORE_FAILED;
    }

    BridgeGroups_Set(&self->groups, group.name, group.filter_count, group.filters);
    openGroupInterface(self, group.name);

    return ADMIN_BRIDGE_STATUS_OK;
}

static uint8_t removeGroup(Broker *self, const char *name)
{
    if (BridgeGroups_Find(&self->groups, name) == NULL) {
        return ADMIN_BRIDGE_STATUS_UNKNOWN;
    }
    if (self->bridge_store != NULL && !self->bridge_store->remove_group(self->bridge_store->context, name)) {
        return ADMIN_BRIDGE_STATUS_STORE_FAILED;
    }

    closeGroupInterface(self, name);
    BridgeGroups_Remove(&self->groups, name);

    return ADMIN_BRIDGE_STATUS_OK;
}

static uint8_t joinGroup(Broker *self, const char *name, const BridgeGroupMember *member)
{
    BridgeGroupSlot *slot = BridgeGroups_Find(&self->groups, name);

    if (slot == NULL) {
        return ADMIN_BRIDGE_STATUS_UNKNOWN;
    }
    if (isGroupEndpoint(&member->endpoint) || !BridgeGroupMember_IsValid(member)) {
        return ADMIN_BRIDGE_STATUS_INVALID;
    }
    if (!BridgeGroup_HasMember(&slot->group, &member->endpoint) && slot->group.member_count == BRIDGE_GROUP_MEMBERS_MAX) {
        return ADMIN_BRIDGE_STATUS_FULL;
    }
    if (self->bridge_store != NULL && !self->bridge_store->add_member(self->bridge_store->context, name, member)) {
        return ADMIN_BRIDGE_STATUS_STORE_FAILED;
    }

    BridgeGroups_Join(&self->groups, name, member);

    return ADMIN_BRIDGE_STATUS_OK;
}

static uint8_t leaveGroup(Broker *self, const char *name, const BridgeEndpoint *member)
{
    BridgeGroupSlot *slot = BridgeGroups_Find(&self->groups, name);

    if (slot == NULL || !BridgeGroup_HasMember(&slot->group, member)) {
        return ADMIN_BRIDGE_STATUS_UNKNOWN;
    }
    if (self->bridge_store != NULL && !self->bridge_store->remove_member(self->bridge_store->context, name, member)) {
        return ADMIN_BRIDGE_STATUS_STORE_FAILED;
    }

    BridgeGroups_Leave(&self->groups, name, member);

    return ADMIN_BRIDGE_STATUS_OK;
}

static void fillGroupEntry(Broker *self, const BridgeGroupSlot *slot, AdminGroupListReplyEntry *entry)
{
    uint8_t i;

    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, slot->group.name, sizeof(entry->name));
    entry->filter_count = slot->group.filter_count;
    memcpy(entry->filters, slot->group.filters, sizeof(entry->filters));
    entry->member_count = slot->group.member_count;
    entry->forwarded = slot->forwarded;
    entry->dropped = slot->dropped;
    for(i=0; i<slot->group.member_count; i++) {
        memcpy(&entry->members[i].endpoint, &slot->group.members[i].endpoint, sizeof(entry->members[i].endpoint));
        entry->members[i].direction = slot->group.members[i].direction;
        entry->members[i].filter_count = slot->group.members[i].filter_count;
        memcpy(entry->members[i].filters, slot->group.members[i].filters, sizeof(entry->members[i].filters));
        if (endpointOnline(self, &slot->group.members[i].endpoint)) {
            entry->members[i].state = ADMIN_GROUP_MEMBER_ONLINE;
        }
    }
}

static void sendGroupReply(Broker *self, HubPeer *peer, uint8_t type, uint8_t status)
{
    AdminGroupReplyMessage reply = { status };
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    sendControl(self, peer, encoded, AdminGroupReplyMessage_Encode(type, &reply, encoded, sizeof(encoded)));
}

static void loadBridgeGroups(Broker *self)
{
    BridgeGroup group;
    uint16_t position = 0;

    if (self->bridge_store == NULL) {
        return;
    }

    while (position < BRIDGE_GROUPS_MAX && self->bridge_store->load_group(self->bridge_store->context, position, &group)) {
        if (BridgeGroups_Restore(&self->groups, &group)) {
            openGroupInterface(self, group.name);
        }
        position++;
    }
}

static bool bridgesConfigured(Broker *self)
{
    return !BridgeRoutes_IsEmpty(&self->bridges) || !BridgeGroups_IsEmpty(&self->groups);
}

static bool channelRefusesFrame(Broker *self, const HubPeer *client, const FrameMessage *frame)
{
    const InterfaceEntry *entry;
    uint32_t interface_id;

    if (!ClientSession_InterfaceForChannel(&client->session, frame->channel, &interface_id)) {
        return false;
    }

    entry = InterfaceRegistry_FindById(&self->registry, interface_id);

    return entry != NULL && InterfaceEntry_RefusesFrame(entry, frame->frame_flags);
}

static bool isGroupEndpoint(const BridgeEndpoint *endpoint)
{
    return strcmp(endpoint->agent_name, BRIDGE_GROUP_AGENT_NAME) == 0;
}

static void openGroupInterface(Broker *self, const char *name)
{
    const InterfaceEntry *entry;

    if (InterfaceRegistry_FindByName(&self->registry, BRIDGE_GROUP_AGENT_NAME, name) != NULL) {
        return;
    }

    entry = InterfaceRegistry_AddVirtual(&self->registry, BRIDGE_GROUP_AGENT_NAME, name);
    if (entry != NULL) {
        reattachInterface(self, entry);
    }
}

static void closeGroupInterface(Broker *self, const char *name)
{
    const InterfaceEntry *entry = InterfaceRegistry_FindByName(&self->registry, BRIDGE_GROUP_AGENT_NAME, name);

    if (entry == NULL) {
        return;
    }

    detachInterface(self, entry);
    InterfaceRegistry_RemoveVirtual(&self->registry, entry->interface_id);
}

static BridgeGroupSlot *groupOfChannel(Broker *self, const HubPeer *client, uint8_t channel, const InterfaceEntry **entry)
{
    uint32_t interface_id;

    if (!ClientSession_InterfaceForChannel(&client->session, channel, &interface_id)) {
        return NULL;
    }

    *entry = InterfaceRegistry_FindById(&self->registry, interface_id);
    if (*entry == NULL || !InterfaceEntry_IsVirtual(*entry)) {
        return NULL;
    }

    return BridgeGroups_Find(&self->groups, (*entry)->interface_name);
}

/*
 * A frame a client writes into a group reaches every member bus that
 * receives and every other client of the group at once; the bus echoes come back bridged and
 * are not shown again.
 */
static void writeIntoGroup(Broker *self, const HubPeer *writer, BridgeGroupSlot *slot, const InterfaceEntry *entry, const FrameMessage *frame)
{
    BridgeTarget target;
    uint8_t i;

    target.forwarded = &slot->forwarded;
    target.dropped = &slot->dropped;
    for(i=0; i<slot->group.member_count; i++) {
        if (!BridgeGroupMember_Receives(&slot->group.members[i])) {
            continue;
        }
        if (endpointLeasedToAnother(self, &slot->group.members[i].endpoint, writer->peer_id)) {
            self->metrics.transactions.lease_frame_drops++;
            continue;
        }
        target.destination = &slot->group.members[i].endpoint;
        bridgeInto(self, &target, frame);
    }

    showToGroup(self, entry, frame, writer->peer_id, 0);
}

static uint16_t showToGroups(Broker *self, const BridgeEndpoint *origin, const FrameMessage *frame, uint32_t echo_originator_peer_id, uint8_t route_flags)
{
    const BridgeGroupSlot *slot;
    const InterfaceEntry *entry;
    uint16_t shown = 0;
    uint8_t i;

    for(i=0; i<BRIDGE_GROUPS_MAX; i++) {
        slot = BridgeGroups_Slot(&self->groups, i);
        if (slot == NULL || !BridgeGroup_Carries(&slot->group, origin, frame->can_id)) {
            continue;
        }
        entry = InterfaceRegistry_FindByName(&self->registry, BRIDGE_GROUP_AGENT_NAME, slot->group.name);
        if (entry != NULL) {
            shown = (uint16_t)(shown + showToGroup(self, entry, frame, echo_originator_peer_id, route_flags));
        }
    }

    return shown;
}

static uint16_t showToGroup(Broker *self, const InterfaceEntry *entry, const FrameMessage *frame, uint32_t echo_originator_peer_id, uint8_t route_flags)
{
    FrameRoute *routes = self->frame_routes;
    uint16_t routes_dropped;
    uint16_t route_count;
    uint16_t i;

    route_count = FrameRoutes_ToInterface(&self->directory, entry->interface_id, routes, FRAME_ROUTES_MAX, &routes_dropped);
    self->metrics.frames_dropped += routes_dropped;
    InterfaceRegistry_CountFrame(&self->registry, entry->interface_id);
    for(i=0; i<route_count; i++) {
        if (routeTakesFrame(self, &routes[i], frame, echo_originator_peer_id)) {
            forwardFrame(self, &routes[i], frame, route_flags);
        }
    }

    return route_count;
}

static uint16_t bridgeFrame(Broker *self, const HubPeer *source_agent, const FrameMessage *frame, uint32_t echo_originator_peer_id, uint8_t route_flags)
{
    const InterfaceEntry *source;
    BridgeEndpoint origin;
    uint16_t count;
    uint16_t i;

    source = InterfaceRegistry_FindByAgentChannel(&self->registry, source_agent->peer_id, frame->channel);
    if (source == NULL) {
        return 0;
    }

    memset(&origin, 0, sizeof(origin));
    memcpy(origin.agent_name, source->agent_name, sizeof(origin.agent_name));
    memcpy(origin.interface_name, source->interface_name, sizeof(origin.interface_name));

    count = BridgeTargets_Collect(&self->bridges, &self->groups, &origin, frame->can_id, self->bridge_targets);
    for(i=0; i<count; i++) {
        bridgeInto(self, &self->bridge_targets[i], frame);
    }

    return (uint16_t)(count + showToGroups(self, &origin, frame, echo_originator_peer_id, route_flags));
}

/*
 * Without an echo token the bus echo of the injection could not be told
 * from plain traffic and would be bridged back: no token, no injection.
 */
static void bridgeInto(Broker *self, const BridgeTarget *target, const FrameMessage *frame)
{
    const InterfaceEntry *destination;
    HubPeer *agent;
    FrameRoute route;
    uint8_t token;

    destination = InterfaceRegistry_FindByName(
        &self->registry,
        target->destination->agent_name,
        target->destination->interface_name
    );
    if (destination != NULL && InterfaceEntry_RefusesFrame(destination, frame->frame_flags)) {
        (*target->dropped)++;
        return;
    }
    agent = (destination != NULL) ? PeerDirectory_Find(&self->directory, destination->agent_peer_id) : NULL;
    token = (agent != NULL) ? EchoTokens_Issue(&agent->echo_tokens, ECHO_TOKENS_BRIDGE_OWNER) : FRAME_ROUTE_NO_TOKEN;
    if (token == FRAME_ROUTE_NO_TOKEN) {
        (*target->dropped)++;
        return;
    }

    route.peer_id = destination->agent_peer_id;
    route.channel = destination->agent_channel;
    route.suppress_echo = false;
    route.reliable = false;
    forwardFrame(self, &route, frame, (uint8_t)(token << FRAME_ROUTE_TOKEN_SHIFT));
    (*target->forwarded)++;
}

static void handleIfconfigReply(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    IfconfigReplyMessage reply;
    ConfigureReplyMessage relayed;
    HubPeer *requester;
    PendingIfconfig pending;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (peer->role != kHUB_PEER_ROLE_AGENT) {
        return;
    }
    if (!IfconfigReplyMessage_Decode(&reply, payload, header->length)) {
        return;
    }
    if (!takePendingIfconfig(self, peer->peer_id, reply.request_id, &pending)) {
        return;
    }

    learnFdFromSettings(self, pending.interface_id, &reply.settings);
    requester = PeerDirectory_Find(&self->directory, pending.requester_peer_id);
    if (requester == NULL) {
        return;
    }

    relayed.status = configureStatus(reply.status);
    relayed.settings = reply.settings;
    memcpy(relayed.detail, reply.detail, IFCONFIG_DETAIL_SIZE);
    sendControl(self, requester, encoded, ConfigureReplyMessage_Encode(&relayed, encoded, sizeof(encoded)));
}

static void handleInterfaceStatus(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    InterfaceStatusMessage status;
    const InterfaceEntry *entry;
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
        InterfaceRegistry_ApplyAdvertisedRate(
            &self->registry,
            peer->peer_id,
            status.entries[i].channel,
            status.entries[i].advertised_rate,
            status.entries[i].credit,
            self->now_us
        );
        entry = InterfaceRegistry_FindByAgentChannel(&self->registry, peer->peer_id, status.entries[i].channel);
        if (entry != NULL) {
            relayPaceToClients(self, entry->interface_id, entry->shaper.rate_bits_per_s);
        }
    }
}

static void relayPaceToClients(Broker *self, uint32_t interface_id, uint32_t pace_rate)
{
    InterfaceStatusMessage status;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    HubPeer *peer;
    uint8_t channel;
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = PeerDirectory_At(&self->directory, i);
        if (peer == NULL || peer->role != kHUB_PEER_ROLE_CLIENT) {
            continue;
        }
        if (!ClientSession_ChannelForInterface(&peer->session, interface_id, &channel)) {
            continue;
        }
        memset(&status, 0, sizeof(status));
        status.interface_count = 1;
        status.entries[0].channel = channel;
        status.entries[0].advertised_rate = pace_rate;
        if (leasedToAnother(self, interface_id, peer->peer_id)) {
            status.entries[0].flags = INTERFACE_STATUS_FLAG_LEASED;
        }
        sendControl(self, peer, encoded, InterfaceStatusMessage_Encode(&status, encoded, sizeof(encoded)));
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
        if (self->require_known_peers || firstContactPinsFull(self)) {
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

static bool agentIsEnrolled(Broker *self, const HubPeer *peer)
{
    if (self->identity_store == NULL || peer->fingerprint_hex[0] == '\0') {
        return false;
    }

    return self->identity_store->knows(self->identity_store->context, peer->fingerprint_hex);
}

static bool firstContactPinsFull(Broker *self)
{
    return self->identity_store->count_first_contact(self->identity_store->context) >= FIRST_CONTACT_PINS_MAX;
}

static bool makeRoomForInterfaces(Broker *self, uint8_t interface_count)
{
    HubPeer *displaced;

    if (registryHasRoomFor(self, interface_count)) {
        return false;
    }

    while (!registryHasRoomFor(self, interface_count)) {
        displaced = PeerDirectory_FindDisplaceableAgent(&self->directory);
        if (displaced == NULL) {
            return false;
        }
        sendError(self, displaced->peer_id, kERROR_CODE_HUB_FULL, "displaced by an enrolled agent");
        disconnectPeer(self, displaced->peer_id);
    }

    return true;
}

static bool registryHasRoomFor(const Broker *self, uint8_t interface_count)
{
    return INTERFACE_REGISTRY_MAX - InterfaceRegistry_Count(&self->registry) >= interface_count;
}

static bool clientIsNamedByAcl(Broker *self, const HubPeer *peer)
{
    if (self->authorization == NULL || peer->fingerprint_hex[0] == '\0') {
        return false;
    }

    return self->authorization->knows(self->authorization->context, peer->fingerprint_hex);
}

static bool clientIsAdmitted(Broker *self, const HubPeer *peer)
{
    if (!self->require_known_peers || peer->fingerprint_hex[0] == '\0') {
        return true;
    }

    return peer->vouched;
}

static void rejectClient(Broker *self, HubPeer *peer)
{
    recordRejection(self, peer, peer->agent_name, ADMIN_REJECTED_REASON_NOT_ALLOWED);
    sendError(self, peer->peer_id, kERROR_CODE_ROLE_REJECTED, "client fingerprint not in the hub allowlist");
    disconnectPeer(self, peer->peer_id);
}

static void rejectAgent(Broker *self, HubPeer *peer, const RegisterMessage *registration, uint8_t identity_status)
{
    RegisterAckMessage ack;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    memset(&ack, 0, sizeof(ack));
    ack.status = identity_status;
    sendControl(self, peer, encoded, RegisterAckMessage_Encode(&ack, encoded, sizeof(encoded)));

    if (identity_status == REGISTER_STATUS_UNKNOWN_AGENT) {
        recordRejection(self, peer, registration->agent_name, ADMIN_REJECTED_REASON_NOT_ALLOWED);
        sendError(self, peer->peer_id, kERROR_CODE_ROLE_REJECTED, unknownAgentDetail(self));
    } else if (identity_status == REGISTER_STATUS_IDENTITY_MISMATCH) {
        recordRejection(self, peer, registration->agent_name, ADMIN_REJECTED_REASON_FINGERPRINT_MISMATCH);
        sendError(self, peer->peer_id, kERROR_CODE_ROLE_REJECTED, "agent name pinned to a different fingerprint");
    } else {
        sendError(self, peer->peer_id, kERROR_CODE_ROLE_REJECTED, "agent identity could not be pinned");
    }

    disconnectPeer(self, peer->peer_id);
}

static const char *unknownAgentDetail(const Broker *self)
{
    if (self->require_known_peers) {
        return "agent fingerprint not in the hub allowlist";
    }

    return "first-contact pins are full; enroll the agent with pins add";
}

static void recordRejection(Broker *self, const HubPeer *peer, const char *name, uint8_t reason)
{
    Rejection rejection = { peer, name, reason, self->now_us };

    RejectedPeers_Record(&self->rejected_peers, &rejection);
}

static bool rememberPendingIfconfig(Broker *self, uint32_t requester_peer_id, const InterfaceEntry *entry, uint32_t *request_id)
{
    uint8_t i;

    for(i=0; i<BROKER_PENDING_IFCONFIG_MAX; i++) {
        if (self->pending_ifconfig[i].in_use) {
            continue;
        }
        self->next_ifconfig_request_id++;
        self->pending_ifconfig[i].in_use = true;
        self->pending_ifconfig[i].requester_peer_id = requester_peer_id;
        self->pending_ifconfig[i].agent_peer_id = entry->agent_peer_id;
        self->pending_ifconfig[i].interface_id = entry->interface_id;
        self->pending_ifconfig[i].request_id = self->next_ifconfig_request_id;
        *request_id = self->next_ifconfig_request_id;
        return true;
    }

    return false;
}

static bool takePendingIfconfig(Broker *self, uint32_t agent_peer_id, uint32_t request_id, PendingIfconfig *taken)
{
    uint8_t i;

    for(i=0; i<BROKER_PENDING_IFCONFIG_MAX; i++) {
        if (!self->pending_ifconfig[i].in_use) {
            continue;
        }
        if (self->pending_ifconfig[i].agent_peer_id != agent_peer_id) {
            continue;
        }
        if (self->pending_ifconfig[i].request_id != request_id) {
            continue;
        }
        *taken = self->pending_ifconfig[i];
        self->pending_ifconfig[i].in_use = false;
        return true;
    }

    return false;
}

/*
 * Turning CAN FD on or off changes what the interface accepts; the settings
 * the agent reports back after a CONFIGURE say which it is now.
 */
static void learnFdFromSettings(Broker *self, uint32_t interface_id, const InterfaceSettings *settings)
{
    if ((settings->fields & INTERFACE_FIELD_CTRLMODE) == 0 || (settings->ctrlmode_mask & INTERFACE_CTRLMODE_FD) == 0) {
        return;
    }

    InterfaceRegistry_SetFdCapable(&self->registry, interface_id, (settings->ctrlmode_flags & INTERFACE_CTRLMODE_FD) != 0);
}

static void releasePendingIfconfig(Broker *self, uint32_t peer_id)
{
    HubPeer *requester;
    uint8_t i;

    for(i=0; i<BROKER_PENDING_IFCONFIG_MAX; i++) {
        if (!self->pending_ifconfig[i].in_use) {
            continue;
        }
        if (self->pending_ifconfig[i].agent_peer_id == peer_id) {
            requester = PeerDirectory_Find(&self->directory, self->pending_ifconfig[i].requester_peer_id);
            if (requester != NULL) {
                replyConfigure(self, requester, CONFIGURE_STATUS_AGENT_UNREACHABLE);
            }
            self->pending_ifconfig[i].in_use = false;
        } else if (self->pending_ifconfig[i].requester_peer_id == peer_id) {
            self->pending_ifconfig[i].in_use = false;
        }
    }
}

static uint8_t configureStatus(uint8_t agent_status)
{
    if (agent_status == IFCONFIG_STATUS_OK) {
        return CONFIGURE_STATUS_OK;
    }
    if (agent_status == IFCONFIG_STATUS_UNKNOWN_INTERFACE) {
        return CONFIGURE_STATUS_UNKNOWN_INTERFACE;
    }

    return CONFIGURE_STATUS_APPLY_FAILED;
}

static void replyConfigure(Broker *self, HubPeer *peer, uint8_t status)
{
    ConfigureReplyMessage reply;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    memset(&reply, 0, sizeof(reply));
    reply.status = status;
    sendControl(self, peer, encoded, ConfigureReplyMessage_Encode(&reply, encoded, sizeof(encoded)));
}

static void disconnectPeer(Broker *self, uint32_t peer_id)
{
    HubPeer *peer = PeerDirectory_Find(&self->directory, peer_id);

    if (peer == NULL) {
        return;
    }

    releasePeer(self, peer);
    self->transport->close_peer(self->transport->context, peer_id);
}

static void releasePeer(Broker *self, HubPeer *peer)
{
    uint32_t peer_id = peer->peer_id;

    if (peer->role == kHUB_PEER_ROLE_AGENT) {
        failAgentTransaction(self, peer_id);
        detachAgent(self, peer_id);
    }
    if (peer->role == kHUB_PEER_ROLE_CLIENT) {
        abandonClientTransactions(self, peer_id);
        releaseReliableBindings(self, peer);
        releaseEchoTokens(self, peer_id);
    }

    releasePendingIfconfig(self, peer_id);
    PeerDirectory_Release(&self->directory, peer_id);
}

static void closeClientChannel(Broker *self, HubPeer *client, uint8_t channel)
{
    ChannelBinding *binding = ClientSession_BindingForChannel(&client->session, channel);
    RelayedTransaction *transaction;
    uint32_t interface_id;
    bool reliable;

    if (binding == NULL) {
        return;
    }

    transaction = TransactionTable_ForClientChannel(&self->transactions, client->peer_id, channel);
    if (transaction != NULL) {
        abandonTransaction(self, transaction);
    }

    interface_id = binding->interface_id;
    reliable = binding->reliable;
    ClientSession_CloseChannel(&client->session, channel);
    if (!reliable) {
        return;
    }

    self->transport->set_channel_mode(self->transport->context, client->peer_id, channel, false);
    revertAgentChannelIfUnused(self, interface_id);
}

static void removeClientInterface(Broker *self, HubPeer *client, uint32_t interface_id)
{
    uint8_t channel;

    while (ClientSession_ChannelForInterface(&client->session, interface_id, &channel)) {
        closeClientChannel(self, client, channel);
    }
}

static void releaseReliableBindings(Broker *self, HubPeer *client)
{
    uint32_t interface_id;

    while (ClientSession_TakeReliableBinding(&client->session, &interface_id)) {
        revertAgentChannelIfUnused(self, interface_id);
    }
}

static void revertAgentChannelIfUnused(Broker *self, uint32_t interface_id)
{
    const InterfaceEntry *entry = InterfaceRegistry_FindById(&self->registry, interface_id);
    HubPeer *peer;
    uint16_t i;

    if (entry == NULL || InterfaceEntry_IsVirtual(entry)) {
        return;
    }

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = PeerDirectory_At(&self->directory, i);
        if (peer == NULL || peer->role != kHUB_PEER_ROLE_CLIENT) {
            continue;
        }
        if (ClientSession_HoldsReliable(&peer->session, interface_id)) {
            return;
        }
    }

    self->transport->set_channel_mode(self->transport->context, entry->agent_peer_id, entry->agent_channel, false);
}

static bool displaceGhostPeer(Broker *self, const HubPeer *peer, const RegisterMessage *registration)
{
    HubPeer *ghost;
    uint32_t ghost_peer_id;

    if (!InterfaceRegistry_CollidingPeer(&self->registry, registration, &ghost_peer_id)) {
        return false;
    }
    if (ghost_peer_id == peer->peer_id) {
        return false;
    }

    ghost = PeerDirectory_Find(&self->directory, ghost_peer_id);
    if (ghost == NULL || !HubPeer_IsSameAgent(peer, ghost)) {
        return false;
    }

    disconnectPeer(self, ghost_peer_id);

    return true;
}

static void detachAgent(Broker *self, uint32_t agent_peer_id)
{
    const InterfaceEntry *entry;
    uint32_t entry_index;

    for(entry_index=0; entry_index<INTERFACE_REGISTRY_MAX; entry_index++) {
        entry = &self->registry.entries[entry_index];
        if (entry->in_use && entry->agent_peer_id == agent_peer_id) {
            detachInterface(self, entry);
        }
    }

    InterfaceRegistry_RemovePeer(&self->registry, agent_peer_id);
}

static void reattachClientsToAgent(Broker *self, uint32_t agent_peer_id)
{
    const InterfaceEntry *entry;
    uint32_t entry_index;

    for(entry_index=0; entry_index<INTERFACE_REGISTRY_MAX; entry_index++) {
        entry = &self->registry.entries[entry_index];
        if (entry->in_use && entry->agent_peer_id == agent_peer_id) {
            reattachInterface(self, entry);
        }
    }
}

static void detachInterface(Broker *self, const InterfaceEntry *entry)
{
    HubPeer *peer;
    uint16_t peer_index;

    for(peer_index=0; peer_index<PEER_DIRECTORY_MAX; peer_index++) {
        peer = PeerDirectory_At(&self->directory, peer_index);
        if (peer != NULL && peer->role == kHUB_PEER_ROLE_CLIENT) {
            ClientSession_DetachInterface(&peer->session, entry->interface_id);
        }
    }
}

static void reattachInterface(Broker *self, const InterfaceEntry *entry)
{
    HubPeer *peer;
    uint16_t peer_index;
    uint8_t channel;

    for(peer_index=0; peer_index<PEER_DIRECTORY_MAX; peer_index++) {
        peer = PeerDirectory_At(&self->directory, peer_index);
        if (peer == NULL || peer->role != kHUB_PEER_ROLE_CLIENT) {
            continue;
        }
        if (!ClientSession_ReattachInterface(&peer->session, entry->agent_name, entry->interface_name, entry->interface_id, clientCanWrite(self, peer, entry))) {
            continue;
        }
        if (!clientCanRead(self, peer, entry)) {
            removeClientInterface(self, peer, entry->interface_id);
            continue;
        }
        if (ClientSession_ChannelForInterface(&peer->session, entry->interface_id, &channel)
            && ClientSession_ChannelReliable(&peer->session, channel)) {
            self->transport->set_channel_mode(self->transport->context, entry->agent_peer_id, entry->agent_channel, true);
        }
    }
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
    BoundedString_Copy(error.detail, sizeof(error.detail), detail);
    encoded_size = ErrorMessage_Encode(&error, encoded, sizeof(encoded));

    self->transport->send_control(self->transport->context, peer_id, encoded, encoded_size);
}

static bool reliableRoutesFit(
    Broker *self,
    const FrameRoute *routes,
    uint16_t route_count,
    const FrameMessage *frame,
    uint32_t echo_originator_peer_id
)
{
    size_t size = frameWireSize(frame);
    uint16_t i;

    for(i=0; i<route_count; i++) {
        if (!routes[i].reliable || !routeTakesFrame(self, &routes[i], frame, echo_originator_peer_id)) {
            continue;
        }
        if (!self->transport->frame_fits(self->transport->context, routes[i].peer_id, routes[i].channel, size)) {
            return false;
        }
    }

    return true;
}

static bool routeTakesFrame(
    Broker *self,
    const FrameRoute *route,
    const FrameMessage *frame,
    uint32_t echo_originator_peer_id
)
{
    HubPeer *destination;

    if (route->suppress_echo && route->peer_id == echo_originator_peer_id) {
        return false;
    }

    destination = PeerDirectory_Find(&self->directory, route->peer_id);
    if (destination == NULL || destination->role != kHUB_PEER_ROLE_CLIENT) {
        return true;
    }

    return ClientSession_ChannelAccepts(&destination->session, route->channel, frame->can_id);
}

static size_t frameWireSize(const FrameMessage *frame)
{
    return (size_t)MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + frame->payload_length;
}

static bool forwardFrame(Broker *self, const FrameRoute *route, const FrameMessage *frame, uint8_t route_flags)
{
    uint8_t forwarded[FRAME_BUFFER_SIZE];
    FrameMessage outgoing = *frame;
    HubPeer *destination;
    size_t size;

    destination = PeerDirectory_Find(&self->directory, route->peer_id);
    outgoing.channel = route->channel;
    outgoing.route_flags = route_flags;
    size = FrameMessage_Encode(&outgoing, forwarded, sizeof(forwarded));

    if (destination == NULL) {
        if (self->transport->send_frame(self->transport->context, route->peer_id, route->channel, forwarded, size)) {
            self->metrics.frames_forwarded++;
        } else {
            self->metrics.frames_dropped++;
        }
        return true;
    }

    if (route->reliable) {
        if (!self->transport->send_frame(self->transport->context, route->peer_id, route->channel, forwarded, size)) {
            return disconnectSlowConsumer(self, destination);
        }
        countForwarded(self, destination, route->channel);
        return true;
    }

    if (EgressQueue_ChannelPending(&destination->egress, route->channel)) {
        enqueueFrame(self, destination, route->channel, forwarded, size);
        return true;
    }

    if (!InterfaceRegistry_TryEgress(
            &self->registry,
            route->peer_id,
            route->channel,
            frameWireBits(forwarded, (uint16_t)size),
            self->now_us)) {
        enqueueFrame(self, destination, route->channel, forwarded, size);
        return true;
    }

    if (self->transport->send_frame(self->transport->context, route->peer_id, route->channel, forwarded, size)) {
        countForwarded(self, destination, route->channel);
        return true;
    }

    enqueueFrame(self, destination, route->channel, forwarded, size);

    return true;
}

static bool disconnectSlowConsumer(Broker *self, const HubPeer *destination)
{
    uint32_t peer_id = destination->peer_id;

    if (destination->role != kHUB_PEER_ROLE_CLIENT) {
        return false;
    }

    sendError(self, peer_id, kERROR_CODE_SLOW_CONSUMER, "reliable channel stalled");
    disconnectPeer(self, peer_id);

    return true;
}

static void enqueueFrame(Broker *self, HubPeer *peer, uint8_t channel, const uint8_t *data, size_t size)
{
    uint8_t evicted_channel = 0;
    TEGRESS_PUSH_RESULT result;

    result = EgressQueue_Push(&peer->egress, channel, data, (uint16_t)size, self->now_us, &evicted_channel);
    if (result != kEGRESS_PUSH_QUEUED) {
        countDropped(self, peer, evicted_channel);
    }
}

static void drainPeer(Broker *self, HubPeer *peer)
{
    const uint8_t *front;
    uint16_t size;
    uint16_t visited;
    uint8_t channel;

    for(visited=0; visited<EGRESS_QUEUE_CHANNELS_MAX; visited++) {
        if (!EgressQueue_NextPendingChannel(&peer->egress, &channel)) {
            return;
        }
        for(;;) {
            front = EgressQueue_FrontOfChannel(&peer->egress, channel, &size);
            if (front == NULL) {
                break;
            }
            if (liveFrameTooOld(self, peer, channel)) {
                EgressQueue_PopChannel(&peer->egress, channel);
                countDropped(self, peer, channel);
                continue;
            }
            if (!InterfaceRegistry_TryEgress(&self->registry, peer->peer_id, channel, frameWireBits(front, size), self->now_us)) {
                break;
            }
            if (!self->transport->send_frame(self->transport->context, peer->peer_id, channel, front, size)) {
                return;
            }
            EgressQueue_PopChannel(&peer->egress, channel);
            countForwarded(self, peer, channel);
        }
    }
}

static bool liveFrameTooOld(const Broker *self, const HubPeer *peer, uint8_t channel)
{
    uint64_t enqueued_us;

    if (peer->role != kHUB_PEER_ROLE_CLIENT) {
        return false;
    }
    enqueued_us = EgressQueue_FrontEnqueuedUs(&peer->egress, channel);

    return self->now_us > enqueued_us && self->now_us - enqueued_us > LIVE_EGRESS_MAX_AGE_US;
}

static uint64_t frameWireBits(const uint8_t *wire, uint16_t size)
{
    if (size <= FRAME_PAYLOAD_LENGTH_OFFSET) {
        return FRAME_PACE_OVERHEAD_BITS;
    }

    return FRAME_PACE_OVERHEAD_BITS + (uint64_t)FRAME_PACE_BITS_PER_BYTE * wire[FRAME_PAYLOAD_LENGTH_OFFSET];
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

static bool reliableEndpointsSupport(Broker *self, const HubPeer *client, const InterfaceEntry *entry)
{
    const HubPeer *agent;

    if ((client->capabilities & HELLO_CAP_RELIABLE_CHANNELS) == 0) {
        return false;
    }

    agent = PeerDirectory_Find(&self->directory, entry->agent_peer_id);

    return agent != NULL && (agent->capabilities & HELLO_CAP_RELIABLE_CHANNELS) != 0;
}

static uint8_t injectionToken(Broker *self, const HubPeer *sender, const FrameRoute *routes, uint16_t route_count)
{
    HubPeer *agent;

    if (route_count == 0) {
        return FRAME_ROUTE_NO_TOKEN;
    }

    agent = PeerDirectory_Find(&self->directory, routes[0].peer_id);
    if (agent == NULL) {
        return FRAME_ROUTE_NO_TOKEN;
    }

    return EchoTokens_Issue(&agent->echo_tokens, sender->peer_id);
}

static void releaseEchoTokens(Broker *self, uint32_t client_peer_id)
{
    HubPeer *peer;
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = PeerDirectory_At(&self->directory, i);
        if (peer != NULL && peer->role == kHUB_PEER_ROLE_AGENT) {
            EchoTokens_ReleaseClient(&peer->echo_tokens, client_peer_id);
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
        sendError(self, peer->peer_id, kERROR_CODE_HELLO_TIMEOUT, "no hello within the deadline");
        disconnectPeer(self, peer->peer_id);
    }
}

/* ---------- private: transactions ---------- */

static void handleTxnBegin(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    TxnBeginMessage begin;
    RelayedTransaction request;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    uint8_t client_channel;
    uint8_t status;

    if (peer->role != kHUB_PEER_ROLE_CLIENT) {
        return;
    }
    if (!TxnBeginMessage_Decode(&begin, payload, header->length)) {
        return;
    }

    client_channel = begin.channel;
    status = admitTransaction(self, peer, &begin, &request);
    if (status == kTXN_BEGIN_STATUS_OK && TransactionTable_Open(&self->transactions, &request) == NULL) {
        status = kTXN_BEGIN_STATUS_BUSY;
    }
    if (status != kTXN_BEGIN_STATUS_OK) {
        self->metrics.transactions.rejected++;
        replyTxnBegin(self, peer, client_channel, status);
        return;
    }

    begin.channel = request.agent_channel;
    sendControl(
        self,
        PeerDirectory_Find(&self->directory, request.agent_peer_id),
        encoded,
        TxnBeginMessage_Encode(&begin, encoded, sizeof(encoded))
    );
}

static void handleTxnBeginAck(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    TxnBeginAckMessage ack;
    RelayedTransaction *transaction;
    HubPeer *client;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    uint8_t client_channel;

    if (peer->role != kHUB_PEER_ROLE_AGENT) {
        return;
    }
    if (!TxnBeginAckMessage_Decode(&ack, payload, header->length)) {
        return;
    }
    transaction = TransactionTable_ForAgent(&self->transactions, peer->peer_id);
    if (transaction == NULL || transaction->state != kTRANSACTION_RELAY_STATE_PENDING) {
        return;
    }

    client = PeerDirectory_Find(&self->directory, transaction->client_peer_id);
    client_channel = transaction->client_channel;
    if (ack.status != kTXN_BEGIN_STATUS_OK) {
        self->metrics.transactions.rejected++;
        TransactionTable_Close(&self->transactions, transaction);
        if (client != NULL) {
            replyTxnBegin(self, client, client_channel, ack.status);
        }
        return;
    }
    if (client == NULL) {
        self->metrics.transactions.aborted++;
        sendTxnAbort(self, peer, transaction->agent_channel, ack.txn_id);
        TransactionTable_Close(&self->transactions, transaction);
        return;
    }

    TransactionTable_Activate(&self->transactions, transaction, ack.txn_id);
    self->metrics.transactions.started++;
    if (transaction->exclusive) {
        announceLease(self, transaction->interface_id);
    }

    ack.channel = client_channel;
    if (ack.window_bytes > TRANSACTION_RELAY_WINDOW_MAX) {
        ack.window_bytes = TRANSACTION_RELAY_WINDOW_MAX;
    }
    sendControl(self, client, encoded, TxnBeginAckMessage_Encode(&ack, encoded, sizeof(encoded)));
}

static void handleTxnContent(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    relayTransactionContent(self, peer, header, payload);
}

static void handleTxnProgress(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    TxnProgressMessage progress;
    RelayedTransaction *transaction;

    if (!TxnProgressMessage_Decode(&progress, payload, header->length)) {
        return;
    }
    transaction = activeTransactionOfAgent(self, peer, progress.txn_id);
    if (transaction == NULL) {
        return;
    }

    transaction->completed_steps = progress.completed_steps;
    transaction->consumed_bytes = progress.consumed_bytes;
    transaction->last_progress_us = self->now_us;
    relayTransactionMessage(
        self,
        PeerDirectory_Find(&self->directory, transaction->client_peer_id),
        header,
        payload,
        transaction->client_channel
    );
}

static void handleTxnStatus(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    TxnStatusMessage status;
    RelayedTransaction *transaction;

    if (!TxnStatusMessage_Decode(&status, payload, header->length)) {
        return;
    }
    transaction = activeTransactionOfAgent(self, peer, status.txn_id);
    if (transaction == NULL) {
        return;
    }

    if (status.state == kTXN_STATE_DONE) {
        self->metrics.transactions.completed++;
    } else {
        self->metrics.transactions.aborted++;
    }
    self->metrics.transactions.steps_completed += status.completed_steps;
    relayTransactionMessage(
        self,
        PeerDirectory_Find(&self->directory, transaction->client_peer_id),
        header,
        payload,
        transaction->client_channel
    );
    finishTransaction(self, transaction);
}

static void handleTxnAbort(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    RelayedTransaction *transaction;

    if (peer->role != kHUB_PEER_ROLE_CLIENT || header->length < TXN_ABORT_BODY_SIZE) {
        return;
    }
    transaction = TransactionTable_ForClientChannel(&self->transactions, peer->peer_id, TxnMessage_Channel(payload));
    if (transaction == NULL) {
        return;
    }
    if (transaction->state == kTRANSACTION_RELAY_STATE_PENDING) {
        abandonTransaction(self, transaction);
        return;
    }

    relayTransactionMessage(
        self,
        PeerDirectory_Find(&self->directory, transaction->agent_peer_id),
        header,
        payload,
        transaction->agent_channel
    );
}

static void handleTxnRosterReport(Broker *self, HubPeer *peer, const MessageHeader *header, const uint8_t *payload)
{
    TxnRosterReportMessage report;
    RelayedTransaction *transaction;

    if (!TxnRosterReportMessage_Decode(&report, payload, header->length)) {
        return;
    }
    transaction = activeTransactionOfAgent(self, peer, report.txn_id);
    if (transaction == NULL) {
        return;
    }

    relayTransactionMessage(
        self,
        PeerDirectory_Find(&self->directory, transaction->client_peer_id),
        header,
        payload,
        transaction->client_channel
    );
}

static uint8_t admitTransaction(Broker *self, const HubPeer *client, const TxnBeginMessage *begin, RelayedTransaction *request)
{
    const InterfaceEntry *entry;
    const HubPeer *agent;
    uint32_t interface_id;

    if (!ClientSession_InterfaceForChannel(&client->session, begin->channel, &interface_id)) {
        return kTXN_BEGIN_STATUS_INVALID;
    }
    entry = InterfaceRegistry_FindById(&self->registry, interface_id);
    if (entry == NULL || InterfaceEntry_IsVirtual(entry)) {
        return kTXN_BEGIN_STATUS_INVALID;
    }
    agent = PeerDirectory_Find(&self->directory, entry->agent_peer_id);
    if (agent == NULL) {
        return kTXN_BEGIN_STATUS_INVALID;
    }
    if ((agent->capabilities & HELLO_CAP_TRANSACTIONS) == 0) {
        return kTXN_BEGIN_STATUS_UNSUPPORTED;
    }
    if (!ClientSession_ChannelReliable(&client->session, begin->channel)) {
        return kTXN_BEGIN_STATUS_RELIABLE_CHANNEL_REQUIRED;
    }
    if (!ClientSession_CanWrite(&client->session, begin->channel)) {
        return kTXN_BEGIN_STATUS_WRITE_DENIED;
    }
    if (TransactionTable_ForAgent(&self->transactions, agent->peer_id) != NULL) {
        return kTXN_BEGIN_STATUS_BUSY;
    }

    memset(request, 0, sizeof(*request));
    request->exclusive = (begin->flags & TXN_BEGIN_FLAG_EXCLUSIVE) != 0;
    request->interface_id = interface_id;
    request->agent_peer_id = agent->peer_id;
    request->agent_channel = entry->agent_channel;
    request->client_peer_id = client->peer_id;
    request->client_channel = begin->channel;
    request->started_at_us = self->now_us;

    return kTXN_BEGIN_STATUS_OK;
}

static void replyTxnBegin(Broker *self, HubPeer *client, uint8_t channel, uint8_t status)
{
    TxnBeginAckMessage ack;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    memset(&ack, 0, sizeof(ack));
    ack.channel = channel;
    ack.status = status;
    sendControl(self, client, encoded, TxnBeginAckMessage_Encode(&ack, encoded, sizeof(encoded)));
}

static bool isTransactionContent(uint8_t type)
{
    return type == kMESSAGE_TYPE_TXN_ROSTER || type == kMESSAGE_TYPE_TXN_TEMPLATE || type == kMESSAGE_TYPE_TXN_STEPS;
}

static bool relayTransactionContent(Broker *self, HubPeer *client, const MessageHeader *header, const uint8_t *payload)
{
    RelayedTransaction *transaction;
    HubPeer *agent;
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    size_t size = (size_t)MESSAGE_HEADER_SIZE + header->length;

    if (client->role != kHUB_PEER_ROLE_CLIENT || header->length == 0 || size > sizeof(encoded)) {
        return true;
    }
    transaction = TransactionTable_ForClientChannel(&self->transactions, client->peer_id, TxnMessage_Channel(payload));
    if (transaction == NULL || transaction->state != kTRANSACTION_RELAY_STATE_ACTIVE) {
        return true;
    }
    agent = PeerDirectory_Find(&self->directory, transaction->agent_peer_id);
    if (agent == NULL) {
        return true;
    }
    if (agentRefusesTemplate(agent, header, payload)) {
        refuseTransaction(self, transaction, agent, kTXN_REASON_UNSUPPORTED);
        return true;
    }
    if (!self->transport->frame_fits(self->transport->context, agent->peer_id, transaction->agent_channel, size)) {
        return false;
    }

    MessageHeader_Encode(header, encoded, sizeof(encoded));
    memcpy(encoded + MESSAGE_HEADER_SIZE, payload, header->length);
    TxnMessage_SetChannel(encoded, transaction->agent_channel);
    if (header->type == kMESSAGE_TYPE_TXN_STEPS && header->length >= TXN_STEPS_FIXED_FIELDS_SIZE) {
        transaction->relayed_bytes += (uint32_t)(header->length - TXN_STEPS_FIXED_FIELDS_SIZE);
    }
    self->transport->send_frame(self->transport->context, agent->peer_id, transaction->agent_channel, encoded, size);

    return true;
}

static bool agentRefusesTemplate(const HubPeer *agent, const MessageHeader *header, const uint8_t *payload)
{
    TxnTemplateMessage template_message;

    if (header->type != kMESSAGE_TYPE_TXN_TEMPLATE || (agent->capabilities & HELLO_CAP_ISOTP) != 0) {
        return false;
    }
    if (!TxnTemplateMessage_Decode(&template_message, payload, header->length)) {
        return false;
    }
    return template_message.type == kTXN_TEMPLATE_TYPE_ISOTP;
}

static void refuseTransaction(Broker *self, RelayedTransaction *transaction, HubPeer *agent, uint8_t reason)
{
    HubPeer *client = PeerDirectory_Find(&self->directory, transaction->client_peer_id);

    sendTxnAbort(self, agent, transaction->agent_channel, transaction->txn_id);
    sendTxnStatus(self, client, transaction, reason);
    self->metrics.transactions.aborted++;
    finishTransaction(self, transaction);
}

static void sendTxnStatus(Broker *self, HubPeer *client, const RelayedTransaction *transaction, uint8_t reason)
{
    TxnStatusMessage status;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    if (client == NULL) {
        return;
    }

    memset(&status, 0, sizeof(status));
    status.channel = transaction->client_channel;
    status.state = kTXN_STATE_ABORTED;
    status.txn_id = transaction->txn_id;
    status.reason = reason;
    status.failing_seq = transaction->completed_steps;
    status.completed_steps = transaction->completed_steps;
    sendControl(self, client, encoded, TxnStatusMessage_Encode(&status, encoded, sizeof(encoded)));
}

static RelayedTransaction *activeTransactionOfAgent(Broker *self, const HubPeer *agent, uint16_t txn_id)
{
    RelayedTransaction *transaction;

    if (agent->role != kHUB_PEER_ROLE_AGENT) {
        return NULL;
    }
    transaction = TransactionTable_ForAgent(&self->transactions, agent->peer_id);
    if (transaction == NULL || transaction->state != kTRANSACTION_RELAY_STATE_ACTIVE || transaction->txn_id != txn_id) {
        return NULL;
    }

    return transaction;
}

static void relayTransactionMessage(
    Broker *self,
    HubPeer *destination,
    const MessageHeader *header,
    const uint8_t *payload,
    uint8_t channel
)
{
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    size_t size = (size_t)MESSAGE_HEADER_SIZE + header->length;

    if (destination == NULL || size > sizeof(encoded)) {
        return;
    }

    MessageHeader_Encode(header, encoded, sizeof(encoded));
    memcpy(encoded + MESSAGE_HEADER_SIZE, payload, header->length);
    TxnMessage_SetChannel(encoded, channel);
    sendControl(self, destination, encoded, size);
}

static void sendTxnAbort(Broker *self, HubPeer *agent, uint8_t channel, uint16_t txn_id)
{
    TxnAbortMessage abort_message;
    uint8_t encoded[CONTROL_BUFFER_SIZE];

    abort_message.channel = channel;
    abort_message.txn_id = txn_id;
    sendControl(self, agent, encoded, TxnAbortMessage_Encode(&abort_message, encoded, sizeof(encoded)));
}

static void finishTransaction(Broker *self, RelayedTransaction *transaction)
{
    uint32_t interface_id = transaction->interface_id;
    bool leased = transaction->exclusive && transaction->state == kTRANSACTION_RELAY_STATE_ACTIVE;

    TransactionTable_Close(&self->transactions, transaction);
    if (leased) {
        announceLease(self, interface_id);
    }
}

static void abandonTransaction(Broker *self, RelayedTransaction *transaction)
{
    HubPeer *agent;

    if (transaction->state == kTRANSACTION_RELAY_STATE_PENDING) {
        transaction->client_peer_id = TRANSACTION_NO_CLIENT;
        return;
    }

    agent = PeerDirectory_Find(&self->directory, transaction->agent_peer_id);
    if (agent != NULL) {
        sendTxnAbort(self, agent, transaction->agent_channel, transaction->txn_id);
    }
    self->metrics.transactions.aborted++;
    finishTransaction(self, transaction);
}

static void abandonClientTransactions(Broker *self, uint32_t client_peer_id)
{
    RelayedTransaction *transaction;
    uint16_t iterator = 0;

    while ((transaction = TransactionTable_NextForClient(&self->transactions, client_peer_id, &iterator)) != NULL) {
        abandonTransaction(self, transaction);
    }
}

static void failAgentTransaction(Broker *self, uint32_t agent_peer_id)
{
    RelayedTransaction *transaction = TransactionTable_ForAgent(&self->transactions, agent_peer_id);
    HubPeer *client;

    if (transaction == NULL) {
        return;
    }

    client = PeerDirectory_Find(&self->directory, transaction->client_peer_id);
    sendTxnStatus(self, client, transaction, kTXN_REASON_AGENT_DISCONNECTED);
    if (transaction->state == kTRANSACTION_RELAY_STATE_ACTIVE) {
        self->metrics.transactions.aborted++;
    } else {
        self->metrics.transactions.rejected++;
    }
    finishTransaction(self, transaction);
}

static void announceLease(Broker *self, uint32_t interface_id)
{
    const InterfaceEntry *entry = InterfaceRegistry_FindById(&self->registry, interface_id);

    if (entry != NULL) {
        relayPaceToClients(self, interface_id, entry->shaper.rate_bits_per_s);
    }
}

static bool leasedToAnother(const Broker *self, uint32_t interface_id, uint32_t client_peer_id)
{
    uint32_t holder_peer_id;

    if (!TransactionTable_LeaseHolder(&self->transactions, interface_id, &holder_peer_id)) {
        return false;
    }
    return holder_peer_id != client_peer_id;
}

static bool channelLeasedToAnother(const Broker *self, const HubPeer *client, uint8_t channel)
{
    uint32_t interface_id;

    if (!TransactionTable_AnyLease(&self->transactions)) {
        return false;
    }
    if (!ClientSession_InterfaceForChannel(&client->session, channel, &interface_id)) {
        return false;
    }
    return leasedToAnother(self, interface_id, client->peer_id);
}

static bool endpointLeasedToAnother(const Broker *self, const BridgeEndpoint *endpoint, uint32_t client_peer_id)
{
    const InterfaceEntry *entry;

    if (!TransactionTable_AnyLease(&self->transactions)) {
        return false;
    }
    entry = InterfaceRegistry_FindByName(&self->registry, endpoint->agent_name, endpoint->interface_name);
    if (entry == NULL) {
        return false;
    }
    return leasedToAnother(self, entry->interface_id, client_peer_id);
}
