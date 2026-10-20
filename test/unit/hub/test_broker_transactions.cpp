#include <cest>
#include <cstring>

extern "C" {
#include "hub/broker.h"
#include "authorization_mock.h"
#include "broker_driver.h"
#include "hub_transport_port_mock.h"
#include "protocol/admin_message.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/interface_status_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"
}

#define AGENT_PEER 100
#define CLIENT_PEER 200
#define OTHER_CLIENT_PEER 201
#define LATE_CLIENT_PEER 202
#define ADMIN_PEER 300
#define AGENT_CHANNEL 0
#define TXN_ID 7
#define AGENT_WINDOW (256 * 1024)
#define REQUEST_ID 0x7E0
#define CLIENT_FINGERPRINT "c1"
#define RELIABLE_WRITE (OPEN_FLAG_RELIABLE | OPEN_FLAG_WANT_WRITE)
#define MESSAGE_BUFFER_SIZE 512
#define AGENT_CAPABILITIES (HELLO_CAP_RELIABLE_CHANNELS | HELLO_CAP_TRANSACTIONS)

static Broker broker;
static HubTransportPortMock transport;
static AuthorizationMock authorization;
static HubTransportEvents events;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };
static uint32_t can0_interface_id;
static uint8_t client_channel;
static uint8_t other_client_channel;

static void startHub(AuthorizationPort *authorization_port, uint32_t agent_capabilities);
static void connectClientWithFingerprint(uint32_t peer_id, const char *fingerprint);
static uint8_t openCan0(uint32_t peer_id, uint8_t open_flags);
static void openCan0KeepingReplies(uint32_t peer_id, uint8_t open_flags);
static void beginFrom(uint32_t peer_id, uint8_t channel, uint8_t flags);
static void agentAnswersBegin(uint8_t status, uint16_t txn_id, uint32_t window_bytes);
static void agentReportsProgress(uint16_t txn_id, uint32_t completed_steps, uint32_t consumed_bytes);
static void agentReportsStatus(uint16_t txn_id, uint8_t state, uint8_t reason, uint32_t completed_steps);
static size_t encodeOneStep(uint8_t channel, uint8_t *buffer, size_t buffer_size);
static bool stepsFromClientStream(uint32_t peer_id, uint8_t channel);
static void stepsFromClientControl(uint32_t peer_id, uint8_t channel);
static void writeFrameFrom(uint32_t peer_id, uint8_t channel);
static int32_t lastControlIndex(uint32_t peer_id, uint8_t type);
static TxnBeginAckMessage lastBeginAckTo(uint32_t peer_id);
static TxnStatusMessage lastStatusTo(uint32_t peer_id);
static uint8_t lastLeasedFlagTo(uint32_t peer_id);
static TransactionCounters adminCounters(void);
static uint8_t framesTo(uint32_t peer_id);

describe("broker transactions", []() {
    describe("begin", []() {
        beforeEach([]() {
            startHub(NULL, AGENT_CAPABILITIES);
            client_channel = openCan0(CLIENT_PEER, RELIABLE_WRITE);
        });

        it("forwards a begin to the owning agent on the agent's channel", []() {
            int32_t index;
            TxnBeginMessage forwarded;

            beginFrom(CLIENT_PEER, client_channel, 0);
            index = lastControlIndex(AGENT_PEER, kMESSAGE_TYPE_TXN_BEGIN);
            TxnBeginMessage_Decode(&forwarded, transport.control_log[index] + MESSAGE_HEADER_SIZE, TXN_BEGIN_BODY_SIZE);

            expect(index >= 0).toBe(true);
            expect(forwarded.channel).toBe(AGENT_CHANNEL);
        });

        it("relays the grant on the client's channel and caps the window to the relay", []() {
            TxnBeginAckMessage ack;

            beginFrom(CLIENT_PEER, client_channel, 0);
            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            ack = lastBeginAckTo(CLIENT_PEER);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_OK);
            expect(ack.channel).toBe(client_channel);
            expect(ack.txn_id).toBe((uint16_t)TXN_ID);
            expect(ack.window_bytes).toBe((uint32_t)TRANSACTION_RELAY_WINDOW_MAX);
        });

        it("relays the agent's refusal and frees the agent", []() {
            TxnBeginAckMessage ack;

            beginFrom(CLIENT_PEER, client_channel, 0);
            agentAnswersBegin(kTXN_BEGIN_STATUS_BUSY, 0, 0);
            ack = lastBeginAckTo(CLIENT_PEER);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_BUSY);
            expect(TransactionTable_ForAgent(&broker.transactions, AGENT_PEER) == NULL).toBe(true);
        });

        it("answers busy while the agent runs another client's transaction", []() {
            TxnBeginAckMessage ack;

            other_client_channel = openCan0(OTHER_CLIENT_PEER, RELIABLE_WRITE);
            beginFrom(CLIENT_PEER, client_channel, 0);
            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            HubTransportPortMock_Reset(&transport);

            beginFrom(OTHER_CLIENT_PEER, other_client_channel, 0);
            ack = lastBeginAckTo(OTHER_CLIENT_PEER);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_BUSY);
            expect(lastControlIndex(AGENT_PEER, kMESSAGE_TYPE_TXN_BEGIN)).toBe(-1);
        });

        it("requires a reliable channel", []() {
            TxnBeginAckMessage ack;

            other_client_channel = openCan0(OTHER_CLIENT_PEER, OPEN_FLAG_WANT_WRITE);

            beginFrom(OTHER_CLIENT_PEER, other_client_channel, 0);
            ack = lastBeginAckTo(OTHER_CLIENT_PEER);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_RELIABLE_CHANNEL_REQUIRED);
        });

        it("rejects a channel the client never opened", []() {
            TxnBeginAckMessage ack;

            beginFrom(CLIENT_PEER, (uint8_t)(client_channel + 1), 0);
            ack = lastBeginAckTo(CLIENT_PEER);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_INVALID);
        });
    });

    describe("begin gating", []() {
        it("answers unsupported for an agent that does not run transactions", []() {
            TxnBeginAckMessage ack;

            startHub(NULL, HELLO_CAP_RELIABLE_CHANNELS);
            client_channel = openCan0(CLIENT_PEER, RELIABLE_WRITE);

            beginFrom(CLIENT_PEER, client_channel, 0);
            ack = lastBeginAckTo(CLIENT_PEER);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_UNSUPPORTED);
            expect(lastControlIndex(AGENT_PEER, kMESSAGE_TYPE_TXN_BEGIN)).toBe(-1);
        });

        it("answers write denied to a client without a write grant", []() {
            TxnBeginAckMessage ack;

            AuthorizationMock_Reset(&authorization);
            startHub(&authorization.port, AGENT_CAPABILITIES);
            connectClientWithFingerprint(OTHER_CLIENT_PEER, CLIENT_FINGERPRINT);
            other_client_channel = BrokerDriver_OpenInterface(&events, &transport, OTHER_CLIENT_PEER, can0_interface_id, OPEN_FLAG_RELIABLE);

            beginFrom(OTHER_CLIENT_PEER, other_client_channel, 0);
            ack = lastBeginAckTo(OTHER_CLIENT_PEER);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_WRITE_DENIED);
        });
    });

    describe("relay", []() {
        beforeEach([]() {
            startHub(NULL, AGENT_CAPABILITIES);
            client_channel = openCan0(CLIENT_PEER, RELIABLE_WRITE);
            beginFrom(CLIENT_PEER, client_channel, 0);
            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            HubTransportPortMock_Reset(&transport);
        });

        it("relays steps from the client's reliable stream to the agent's channel", []() {
            bool accepted;
            uint8_t relayed_channel;

            accepted = stepsFromClientStream(CLIENT_PEER, client_channel);
            relayed_channel = TxnMessage_Channel(transport.frame_log[0] + MESSAGE_HEADER_SIZE);

            expect(accepted).toBe(true);
            expect(transport.frame_count).toBe(1);
            expect(transport.frame_peers[0]).toBe((uint32_t)AGENT_PEER);
            expect(transport.frame_channels[0]).toBe(AGENT_CHANNEL);
            expect(transport.frame_log[0][0]).toBe((uint8_t)kMESSAGE_TYPE_TXN_STEPS);
            expect(relayed_channel).toBe(AGENT_CHANNEL);
        });

        it("holds steps back while the agent's stream is full", []() {
            bool accepted;

            transport.full_peer = AGENT_PEER;

            accepted = stepsFromClientStream(CLIENT_PEER, client_channel);

            expect(accepted).toBe(false);
            expect(transport.frame_count).toBe(0);
        });

        it("relays steps that arrive on the control stream", []() {
            stepsFromClientControl(CLIENT_PEER, client_channel);

            expect(transport.frame_count).toBe(1);
            expect(transport.frame_peers[0]).toBe((uint32_t)AGENT_PEER);
        });

        it("relays progress to the client on its channel", []() {
            int32_t index;
            TxnProgressMessage progress;

            agentReportsProgress(TXN_ID, 100, 1600);
            index = lastControlIndex(CLIENT_PEER, kMESSAGE_TYPE_TXN_PROGRESS);
            TxnProgressMessage_Decode(&progress, transport.control_log[index] + MESSAGE_HEADER_SIZE, TXN_PROGRESS_BODY_SIZE);

            expect(progress.channel).toBe(client_channel);
            expect(progress.consumed_bytes).toBe((uint32_t)1600);
        });

        it("ignores reports about another transaction", []() {
            agentReportsProgress(TXN_ID + 1, 100, 1600);

            expect(lastControlIndex(CLIENT_PEER, kMESSAGE_TYPE_TXN_PROGRESS)).toBe(-1);
        });

        it("relays the outcome, frees the agent and counts it", []() {
            TxnStatusMessage status;
            TransactionCounters counters;

            agentReportsStatus(TXN_ID, kTXN_STATE_DONE, kTXN_REASON_NONE, 17);
            status = lastStatusTo(CLIENT_PEER);
            counters = adminCounters();

            expect(status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(status.channel).toBe(client_channel);
            expect(TransactionTable_ForAgent(&broker.transactions, AGENT_PEER) == NULL).toBe(true);
            expect(counters.started).toBe((uint64_t)1);
            expect(counters.completed).toBe((uint64_t)1);
            expect(counters.steps_completed).toBe((uint64_t)17);
        });

        it("lists the relayed transaction to the admin", []() {
            AdminTxnsMessage request = { 0 };
            AdminTxnsReplyMessage reply;
            uint8_t encoded[MESSAGE_BUFFER_SIZE];
            size_t encoded_size;
            int32_t index;

            BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
            encoded_size = AdminTxnsMessage_Encode(&request, encoded, sizeof(encoded));
            events.on_peer_control(events.context, ADMIN_PEER, encoded, encoded_size, 0);
            index = lastControlIndex(ADMIN_PEER, kMESSAGE_TYPE_ADMIN_TXNS_REPLY);
            AdminTxnsReplyMessage_Decode(&reply, transport.control_log[index] + MESSAGE_HEADER_SIZE, transport.control_sizes[index] - MESSAGE_HEADER_SIZE);

            expect(reply.count).toBe(1);
            expect(reply.entries[0].client_peer_id).toBe((uint32_t)CLIENT_PEER);
            expect(reply.entries[0].agent_peer_id).toBe((uint32_t)AGENT_PEER);
            expect(reply.entries[0].state).toBe((uint8_t)ADMIN_TXN_STATE_ACTIVE);
            expect(reply.entries[0].txn_id).toBe((uint16_t)TXN_ID);
        });

        it("forwards the client's abort to the agent", []() {
            TxnAbortMessage abort_message = { client_channel, TXN_ID };
            TxnAbortMessage forwarded;
            uint8_t encoded[MESSAGE_BUFFER_SIZE];
            size_t encoded_size;
            int32_t index;

            encoded_size = TxnAbortMessage_Encode(&abort_message, encoded, sizeof(encoded));
            events.on_peer_control(events.context, CLIENT_PEER, encoded, encoded_size, 0);
            index = lastControlIndex(AGENT_PEER, kMESSAGE_TYPE_TXN_ABORT);
            TxnAbortMessage_Decode(&forwarded, transport.control_log[index] + MESSAGE_HEADER_SIZE, TXN_ABORT_BODY_SIZE);

            expect(forwarded.channel).toBe(AGENT_CHANNEL);
            expect(forwarded.txn_id).toBe((uint16_t)TXN_ID);
        });
    });

    describe("lease", []() {
        beforeEach([]() {
            startHub(NULL, AGENT_CAPABILITIES);
            client_channel = openCan0(CLIENT_PEER, RELIABLE_WRITE);
            other_client_channel = openCan0(OTHER_CLIENT_PEER, OPEN_FLAG_WANT_WRITE);
            beginFrom(CLIENT_PEER, client_channel, TXN_BEGIN_FLAG_EXCLUSIVE);
        });

        it("tells the other clients the interface is leased", []() {
            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);

            expect(lastLeasedFlagTo(OTHER_CLIENT_PEER)).toBe((uint8_t)INTERFACE_STATUS_FLAG_LEASED);
            expect(lastLeasedFlagTo(CLIENT_PEER)).toBe(0);
        });

        it("tells a client that opens the interface during the lease", []() {
            uint8_t late_flag;

            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            HubTransportPortMock_Reset(&transport);

            openCan0KeepingReplies(LATE_CLIENT_PEER, OPEN_FLAG_WANT_WRITE);
            late_flag = lastLeasedFlagTo(LATE_CLIENT_PEER);

            expect(late_flag).toBe((uint8_t)INTERFACE_STATUS_FLAG_LEASED);
        });

        it("drops and counts the other clients' frames", []() {
            TransactionCounters counters;

            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            HubTransportPortMock_Reset(&transport);

            writeFrameFrom(OTHER_CLIENT_PEER, other_client_channel);
            counters = adminCounters();

            expect(framesTo(AGENT_PEER)).toBe(0);
            expect(counters.lease_frame_drops).toBe((uint64_t)1);
        });

        it("lets the lease holder write", []() {
            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            HubTransportPortMock_Reset(&transport);

            writeFrameFrom(CLIENT_PEER, client_channel);

            expect(framesTo(AGENT_PEER)).toBe(1);
        });

        it("does not lease while the agent has not granted the transaction", []() {
            writeFrameFrom(OTHER_CLIENT_PEER, other_client_channel);

            expect(framesTo(AGENT_PEER)).toBe(1);
        });

        it("releases the lease with the outcome and says so", []() {
            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            HubTransportPortMock_Reset(&transport);

            agentReportsStatus(TXN_ID, kTXN_STATE_ABORTED, kTXN_REASON_TIMEOUT, 3);
            writeFrameFrom(OTHER_CLIENT_PEER, other_client_channel);

            expect(lastLeasedFlagTo(OTHER_CLIENT_PEER)).toBe(0);
            expect(framesTo(AGENT_PEER)).toBe(1);
        });
    });

    describe("disconnects", []() {
        beforeEach([]() {
            startHub(NULL, AGENT_CAPABILITIES);
            client_channel = openCan0(CLIENT_PEER, RELIABLE_WRITE);
            other_client_channel = openCan0(OTHER_CLIENT_PEER, OPEN_FLAG_WANT_WRITE);
            beginFrom(CLIENT_PEER, client_channel, TXN_BEGIN_FLAG_EXCLUSIVE);
        });

        it("cancels the agent's transaction and releases the lease when the client leaves", []() {
            TxnAbortMessage forwarded;
            int32_t index;

            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            HubTransportPortMock_Reset(&transport);

            events.on_peer_disconnected(events.context, CLIENT_PEER, 0);
            index = lastControlIndex(AGENT_PEER, kMESSAGE_TYPE_TXN_ABORT);
            TxnAbortMessage_Decode(&forwarded, transport.control_log[index] + MESSAGE_HEADER_SIZE, TXN_ABORT_BODY_SIZE);
            writeFrameFrom(OTHER_CLIENT_PEER, other_client_channel);

            expect(forwarded.txn_id).toBe((uint16_t)TXN_ID);
            expect(framesTo(AGENT_PEER)).toBe(1);
        });

        it("cancels a transaction the agent grants after its client left", []() {
            int32_t index;

            events.on_peer_disconnected(events.context, CLIENT_PEER, 0);
            HubTransportPortMock_Reset(&transport);

            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            index = lastControlIndex(AGENT_PEER, kMESSAGE_TYPE_TXN_ABORT);

            expect(index >= 0).toBe(true);
            expect(TransactionTable_AnyLease(&broker.transactions)).toBe(false);
        });

        it("tells the client when the agent leaves", []() {
            TxnStatusMessage status;

            agentAnswersBegin(kTXN_BEGIN_STATUS_OK, TXN_ID, AGENT_WINDOW);
            HubTransportPortMock_Reset(&transport);

            events.on_peer_disconnected(events.context, AGENT_PEER, 0);
            status = lastStatusTo(CLIENT_PEER);

            expect(status.state).toBe((uint8_t)kTXN_STATE_ABORTED);
            expect(status.reason).toBe((uint8_t)kTXN_REASON_AGENT_DISCONNECTED);
            expect(status.txn_id).toBe((uint16_t)TXN_ID);
            expect(TransactionTable_AnyLease(&broker.transactions)).toBe(false);
        });
    });
});

static void startHub(AuthorizationPort *authorization_port, uint32_t agent_capabilities)
{
    HubTransportPortMock_Reset(&transport);
    Broker_Init(&broker, &transport.port, NULL, authorization_port, NULL, false);
    events = Broker_Events(&broker);
    BrokerDriver_ConnectAgentWithCapabilities(&events, &transport, AGENT_PEER, &truck_registration, agent_capabilities);
    can0_interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
}

static void connectClientWithFingerprint(uint32_t peer_id, const char *fingerprint)
{
    HubPeerConnectInfo info = { fingerprint, NULL, NULL, kPEER_TRANSPORT_QUIC, false };
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_CLIENT, HELLO_CAP_RELIABLE_CHANNELS, "" };
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    events.on_peer_connected(events.context, peer_id, &info, 0);
    encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
}

static uint8_t openCan0(uint32_t peer_id, uint8_t open_flags)
{
    BrokerDriver_ConnectClientWithCapabilities(&events, peer_id, HELLO_CAP_RELIABLE_CHANNELS);
    return BrokerDriver_OpenInterface(&events, &transport, peer_id, can0_interface_id, open_flags);
}

static void openCan0KeepingReplies(uint32_t peer_id, uint8_t open_flags)
{
    OpenMessage open = { can0_interface_id, open_flags };
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    BrokerDriver_ConnectClientWithCapabilities(&events, peer_id, HELLO_CAP_RELIABLE_CHANNELS);
    encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));
    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
}

static void beginFrom(uint32_t peer_id, uint8_t channel, uint8_t flags)
{
    TxnBeginMessage begin;
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    memset(&begin, 0, sizeof(begin));
    begin.channel = channel;
    begin.flags = flags;
    encoded_size = TxnBeginMessage_Encode(&begin, encoded, sizeof(encoded));
    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
}

static void agentAnswersBegin(uint8_t status, uint16_t txn_id, uint32_t window_bytes)
{
    TxnBeginAckMessage ack = { AGENT_CHANNEL, status, txn_id, window_bytes };
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    encoded_size = TxnBeginAckMessage_Encode(&ack, encoded, sizeof(encoded));
    events.on_peer_control(events.context, AGENT_PEER, encoded, encoded_size, 0);
}

static void agentReportsProgress(uint16_t txn_id, uint32_t completed_steps, uint32_t consumed_bytes)
{
    TxnProgressMessage progress = { AGENT_CHANNEL, txn_id, completed_steps, consumed_bytes };
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    encoded_size = TxnProgressMessage_Encode(&progress, encoded, sizeof(encoded));
    events.on_peer_control(events.context, AGENT_PEER, encoded, encoded_size, 0);
}

static void agentReportsStatus(uint16_t txn_id, uint8_t state, uint8_t reason, uint32_t completed_steps)
{
    TxnStatusMessage status;
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    memset(&status, 0, sizeof(status));
    status.channel = AGENT_CHANNEL;
    status.state = state;
    status.txn_id = txn_id;
    status.reason = reason;
    status.completed_steps = completed_steps;
    encoded_size = TxnStatusMessage_Encode(&status, encoded, sizeof(encoded));
    events.on_peer_control(events.context, AGENT_PEER, encoded, encoded_size, 0);
}

static size_t encodeOneStep(uint8_t channel, uint8_t *buffer, size_t buffer_size)
{
    TxnStepsMessage fixed_fields = { channel, 0, TXN_ID, 0, NULL, 0 };
    TxnStepsWriter writer;
    TxnStep step;

    memset(&step, 0, sizeof(step));
    step.can_id = REQUEST_ID;
    step.payload_length = FRAME_PAYLOAD_MAX_CLASSIC;
    TxnStepsWriter_Init(&writer, &fixed_fields, buffer, buffer_size);
    TxnStepsWriter_Append(&writer, &step);
    return TxnStepsWriter_Finish(&writer);
}

static bool stepsFromClientStream(uint32_t peer_id, uint8_t channel)
{
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    encoded_size = encodeOneStep(channel, encoded, sizeof(encoded));
    return events.on_peer_frame(events.context, peer_id, encoded, encoded_size);
}

static void stepsFromClientControl(uint32_t peer_id, uint8_t channel)
{
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    encoded_size = encodeOneStep(channel, encoded, sizeof(encoded));
    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
}

static void writeFrameFrom(uint32_t peer_id, uint8_t channel)
{
    FrameMessage frame = { REQUEST_ID, 0, channel, 1, 0, 0, { 0x55 } };
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;

    encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));
    events.on_peer_frame(events.context, peer_id, encoded, encoded_size);
}

static int32_t lastControlIndex(uint32_t peer_id, uint8_t type)
{
    int32_t i;

    for(i=transport.control_count - 1; i>=0; i--) {
        if (transport.control_peers[i] == peer_id && transport.control_log[i][0] == type) {
            return i;
        }
    }
    return -1;
}

static TxnBeginAckMessage lastBeginAckTo(uint32_t peer_id)
{
    TxnBeginAckMessage ack;
    int32_t index = lastControlIndex(peer_id, kMESSAGE_TYPE_TXN_BEGIN_ACK);

    memset(&ack, 0xFF, sizeof(ack));
    if (index >= 0) {
        TxnBeginAckMessage_Decode(&ack, transport.control_log[index] + MESSAGE_HEADER_SIZE, TXN_BEGIN_ACK_BODY_SIZE);
    }
    return ack;
}

static TxnStatusMessage lastStatusTo(uint32_t peer_id)
{
    TxnStatusMessage status;
    int32_t index = lastControlIndex(peer_id, kMESSAGE_TYPE_TXN_STATUS);

    memset(&status, 0xFF, sizeof(status));
    if (index >= 0) {
        TxnStatusMessage_Decode(&status, transport.control_log[index] + MESSAGE_HEADER_SIZE, TXN_STATUS_BODY_SIZE);
    }
    return status;
}

static uint8_t lastLeasedFlagTo(uint32_t peer_id)
{
    InterfaceStatusMessage status;
    int32_t index = lastControlIndex(peer_id, kMESSAGE_TYPE_INTERFACE_STATUS);

    if (index < 0) {
        return 0xFF;
    }
    InterfaceStatusMessage_Decode(&status, transport.control_log[index] + MESSAGE_HEADER_SIZE, INTERFACE_STATUS_BODY_SIZE);
    return status.entries[0].flags & INTERFACE_STATUS_FLAG_LEASED;
}

static TransactionCounters adminCounters(void)
{
    AdminStatusReplyMessage reply;
    uint8_t encoded[MESSAGE_BUFFER_SIZE];
    size_t encoded_size;
    int32_t index;

    BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
    encoded_size = AdminStatusMessage_Encode(encoded, sizeof(encoded));
    events.on_peer_control(events.context, ADMIN_PEER, encoded, encoded_size, 0);
    index = lastControlIndex(ADMIN_PEER, kMESSAGE_TYPE_ADMIN_STATUS_REPLY);
    AdminStatusReplyMessage_Decode(&reply, transport.control_log[index] + MESSAGE_HEADER_SIZE, ADMIN_STATUS_REPLY_BODY_SIZE);
    return reply.transactions;
}

static uint8_t framesTo(uint32_t peer_id)
{
    uint8_t count = 0;
    int32_t i;

    for(i=0; i<transport.frame_count; i++) {
        if (transport.frame_peers[i] == peer_id) {
            count++;
        }
    }
    return count;
}
