#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "agent/agent.h"
#include "can_port_mock.h"
#include "protocol/hello_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/interface_status_message.h"
#include "protocol/message_header.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"
#include "transport_port_mock.h"
}

static Agent agent;
static TransportPortMock transport;
static CanPortMock can;
static const RegisterMessage registration = { "truck42", 2, { "can0", "can1" } };
static const RegisterAckMessage ack_ok = { REGISTER_STATUS_OK, 2, { 7, 9 } };
static IfconfigReplyMessage ifconfig_reply;
static uint8_t ifconfig_reply_type;

#define IFCONFIG_REQUEST_ID 77
#define TXN_CHANNEL 7
#define UNASSIGNED_CHANNEL 99
#define REQUEST_ID 0x7E0
#define RESPONSE_ID 0x7E8
#define ID_MASK 0x7FF
#define TXN_TIMEOUT_MS 50
#define TXN_TEMPLATE_COUNT_K_BODY_OFFSET 6

static InterfaceSettings bitrateSettings(uint32_t bitrate);
static void sendIfconfig(const char *interface_name, uint8_t op, const InterfaceSettings *settings);
static void enterRunning(void);
static TxnBeginAckMessage sendTxnBegin(uint8_t channel);
static size_t encodeResponseTemplate(uint16_t txn_id, uint8_t *buffer, size_t buffer_size);
static size_t encodeLastRequestStep(uint16_t txn_id, uint8_t *buffer, size_t buffer_size);
static uint8_t lastControlType(void);
static TxnStatusMessage lastTxnStatus(void);

describe("agent", []() {
    describe("disconnected", []() {
        beforeEach([]() {
            TransportPortMock_Reset(&transport);
            CanPortMock_Reset(&can);
            Agent_Init(&agent, &transport.port, &can.port, &registration);
        });

        it("connects on the first tick", []() {
            Agent_Tick(&agent, 0);

            expect(transport.connect_calls).toBe(1);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_CONNECTING);
        });

        it("doubles the backoff on repeated connect failures", []() {
            transport.connect_result = false;

            Agent_Tick(&agent, 0);
            Agent_Tick(&agent, 1000000);
            Agent_Tick(&agent, 1000000 + 1999999);
            Agent_Tick(&agent, 1000000 + 2000000);

            expect(transport.connect_calls).toBe(3);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_DISCONNECTED);
        });
    });

    describe("connecting", []() {
        beforeEach([]() {
            TransportPortMock_Reset(&transport);
            CanPortMock_Reset(&can);
            Agent_Init(&agent, &transport.port, &can.port, &registration);
            Agent_Tick(&agent, 0);
        });

        it("sends hello and register once connected", []() {
            MessageHeader first_header;
            MessageHeader second_header;
            RegisterMessage sent_registration;

            Agent_OnConnected(&agent);
            MessageHeader_Decode(&first_header, transport.control_log[0], transport.control_sizes[0]);
            MessageHeader_Decode(&second_header, transport.control_log[1], transport.control_sizes[1]);
            RegisterMessage_Decode(&sent_registration, transport.control_log[1] + MESSAGE_HEADER_SIZE, second_header.length);

            expect(transport.control_count).toBe(2);
            expect(first_header.type).toBe(kMESSAGE_TYPE_HELLO);
            expect(second_header.type).toBe(kMESSAGE_TYPE_REGISTER);
            expect((const char *)sent_registration.agent_name).toBe("truck42");
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_REGISTERING);
        });
    });

    describe("registering", []() {
        beforeEach([]() {
            TransportPortMock_Reset(&transport);
            CanPortMock_Reset(&can);
            Agent_Init(&agent, &transport.port, &can.port, &registration);
            Agent_Tick(&agent, 0);
            Agent_OnConnected(&agent);
        });

        it("runs after a successful register ack", []() {
            uint8_t encoded[64];
            size_t encoded_size = RegisterAckMessage_Encode(&ack_ok, encoded, sizeof(encoded));

            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);

            expect(Agent_State(&agent)).toBe(kAGENT_STATE_RUNNING);
        });

        it("disconnects when the register ack reports an error", []() {
            RegisterAckMessage ack_error = { 1, 0, { 0 } };
            uint8_t encoded[64];
            size_t encoded_size = RegisterAckMessage_Encode(&ack_error, encoded, sizeof(encoded));

            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);

            expect(transport.disconnect_calls).toBe(1);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_DISCONNECTED);
        });

        it("drops can frames while not running", []() {
            FrameMessage frame = { 0x123, 1000, 0, 2, 0, 0, { 0xAA, 0xBB } };

            Agent_OnCanFrame(&agent, 0, &frame);

            expect(transport.frame_count).toBe(0);
        });

        it("stays registering while the ack timeout has not expired", []() {
            Agent_Tick(&agent, 1000000);

            Agent_Tick(&agent, 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000 - 1);

            expect(transport.disconnect_calls).toBe(0);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_REGISTERING);
        });

        it("disconnects when the register ack never arrives", []() {
            Agent_Tick(&agent, 1000000);

            Agent_Tick(&agent, 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000);

            expect(transport.disconnect_calls).toBe(1);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_DISCONNECTED);
        });

        it("does not time out after a successful ack", []() {
            uint8_t encoded[64];
            size_t encoded_size = RegisterAckMessage_Encode(&ack_ok, encoded, sizeof(encoded));

            Agent_Tick(&agent, 1000000);
            Agent_OnControlMessage(&agent, encoded, encoded_size, 2000000);

            Agent_Tick(&agent, 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000);

            expect(transport.disconnect_calls).toBe(0);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_RUNNING);
        });

        it("arms a fresh timeout on the next registration attempt", []() {
            uint8_t encoded[64];
            size_t encoded_size = RegisterAckMessage_Encode(&ack_ok, encoded, sizeof(encoded));
            uint64_t second_attempt_us = 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000 + 2000000;

            Agent_Tick(&agent, 1000000);
            Agent_Tick(&agent, 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000);
            Agent_Tick(&agent, second_attempt_us);
            Agent_OnConnected(&agent);
            Agent_Tick(&agent, second_attempt_us);

            Agent_OnControlMessage(&agent, encoded, encoded_size, second_attempt_us + 1000);

            expect(Agent_State(&agent)).toBe(kAGENT_STATE_RUNNING);
            expect(transport.disconnect_calls).toBe(1);
        });

        it("replies pong to a ping request", []() {
            MessageHeader ping = { kMESSAGE_TYPE_PING, 0, 0 };
            MessageHeader reply;
            uint8_t encoded[MESSAGE_HEADER_SIZE];
            int control_count_before = transport.control_count;

            MessageHeader_Encode(&ping, encoded, sizeof(encoded));

            Agent_OnControlMessage(&agent, encoded, sizeof(encoded), 0);
            MessageHeader_Decode(
                &reply,
                transport.control_log[control_count_before],
                transport.control_sizes[control_count_before]
            );

            expect(transport.control_count).toBe(control_count_before + 1);
            expect(reply.type).toBe(kMESSAGE_TYPE_PING);
            expect(reply.flags).toBe(0x01);
        });
    });

    describe("running", []() {
        beforeEach([]() {
            enterRunning();
        });

        it("forwards can frames with the assigned channel", []() {
            FrameMessage frame = { 0x123, 1000, 0, 2, 0, 0, { 0xAA, 0xBB } };
            FrameMessage sent_frame = { 0 };
            MessageHeader sent_header;

            Agent_OnCanFrame(&agent, 1, &frame);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(transport.frame_count).toBe(1);
            expect(sent_frame.channel).toBe(9);
            expect(sent_frame.can_id).toBe((uint32_t)0x123);
            expect(sent_frame.payload).toEqual(frame.payload);
        });

        it("injects transport frames into the owning interface", []() {
            FrameMessage frame = { 0x456, 2000, 7, 3, 0, 0, { 1, 2, 3 } };
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&frame, encoded_frame, sizeof(encoded_frame));

            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);

            expect(can.write_count).toBe(1);
            expect(can.last_interface_index).toBe(0);
            expect(can.last_frame.can_id).toBe((uint32_t)0x456);
        });

        it("ignores transport frames for unknown channels", []() {
            FrameMessage frame = { 0x456, 2000, 42, 3, 0, 0, { 1, 2, 3 } };
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&frame, encoded_frame, sizeof(encoded_frame));

            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);

            expect(can.write_count).toBe(0);
        });

        it("reconnects only after the backoff delay", []() {
            Agent_OnDisconnected(&agent, 5000000);

            Agent_Tick(&agent, 5000000 + 999999);
            Agent_Tick(&agent, 5000000 + 1000000);

            expect(transport.connect_calls).toBe(2);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_CONNECTING);
        });

        it("returns the origin token on the bus echo of an injection", []() {
            FrameMessage injected = { 0x456, 2000, 7, 1, 0, 8 << FRAME_ROUTE_TOKEN_SHIFT, { 0x55 } };
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, FRAME_ROUTE_FLAG_ECHO, { 0x55 } };
            FrameMessage sent_frame;
            MessageHeader sent_header;
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&injected, encoded_frame, sizeof(encoded_frame));

            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);

            Agent_OnCanFrame(&agent, 0, &echo);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(transport.frame_count).toBe(1);
            expect(sent_frame.route_flags).toBe(FRAME_ROUTE_FLAG_ECHO | (8 << FRAME_ROUTE_TOKEN_SHIFT));
        });

        it("forwards an unmatched echo without a token", []() {
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, FRAME_ROUTE_FLAG_ECHO, { 0x55 } };
            FrameMessage sent_frame;
            MessageHeader sent_header;

            Agent_OnCanFrame(&agent, 0, &echo);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(sent_frame.route_flags).toBe(FRAME_ROUTE_FLAG_ECHO);
        });

        it("drops the pending token when the bus write fails", []() {
            FrameMessage injected = { 0x456, 2000, 7, 1, 0, 8 << FRAME_ROUTE_TOKEN_SHIFT, { 0x55 } };
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, FRAME_ROUTE_FLAG_ECHO, { 0x55 } };
            FrameMessage sent_frame;
            MessageHeader sent_header;
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&injected, encoded_frame, sizeof(encoded_frame));

            can.write_result = false;
            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);

            Agent_OnCanFrame(&agent, 0, &echo);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(sent_frame.route_flags).toBe(FRAME_ROUTE_FLAG_ECHO);
        });

        it("strips stale route flags from genuine bus frames", []() {
            FrameMessage frame = { 0x123, 1000, 0, 1, 0, FRAME_ROUTE_TOKEN_MASK, { 0x55 } };
            FrameMessage sent_frame;
            MessageHeader sent_header;

            Agent_OnCanFrame(&agent, 0, &frame);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(sent_frame.route_flags).toBe(0);
        });

        it("applies the requested settings and replies with the effective ones", []() {
            InterfaceSettings requested = bitrateSettings(250000);

            can.query_settings = bitrateSettings(250000);
            can.query_settings.sample_point = 875;

            sendIfconfig("can1", IFCONFIG_OP_APPLY, &requested);

            expect(can.configure_count).toBe(1);
            expect(can.last_configure_interface_index).toBe(1);
            expect(can.last_configure_settings.bitrate).toBe((uint32_t)250000);
            expect(ifconfig_reply_type).toBe(kMESSAGE_TYPE_IFCONFIG_REPLY);
            expect(ifconfig_reply.request_id).toBe((uint32_t)IFCONFIG_REQUEST_ID);
            expect(ifconfig_reply.status).toBe(IFCONFIG_STATUS_OK);
            expect(ifconfig_reply.settings.sample_point).toBe((uint16_t)875);
        });

        it("answers a query with the settings without changing them", []() {
            InterfaceSettings requested = bitrateSettings(0);

            can.query_settings = bitrateSettings(500000);

            sendIfconfig("can0", IFCONFIG_OP_QUERY, &requested);

            expect(can.configure_count).toBe(0);
            expect(ifconfig_reply.status).toBe(IFCONFIG_STATUS_OK);
            expect(ifconfig_reply.settings.bitrate).toBe((uint32_t)500000);
        });

        it("replies unknown interface for a name it does not export", []() {
            InterfaceSettings requested = bitrateSettings(250000);

            sendIfconfig("can9", IFCONFIG_OP_APPLY, &requested);

            expect(can.configure_count).toBe(0);
            expect(ifconfig_reply.status).toBe(IFCONFIG_STATUS_UNKNOWN_INTERFACE);
        });

        it("refuses a bitrate and a time quantum in one request without reaching the adapter", []() {
            InterfaceSettings requested = bitrateSettings(250000);

            requested.fields |= INTERFACE_FIELD_BIT_TIMING;
            requested.bit_timing.tq = 125;

            sendIfconfig("can0", IFCONFIG_OP_APPLY, &requested);

            expect(can.configure_count).toBe(0);
            expect(ifconfig_reply.status).toBe(IFCONFIG_STATUS_APPLY_FAILED);
            expect((const char *)ifconfig_reply.detail).toBe("give a bitrate or a time quantum and segments, not both");
        });

        it("replies apply failed with the reason the adapter gives", []() {
            InterfaceSettings requested = bitrateSettings(250000);

            can.configure_result = false;
            snprintf(can.configure_detail, sizeof(can.configure_detail), "Operation not supported");

            sendIfconfig("can0", IFCONFIG_OP_APPLY, &requested);

            expect(can.configure_count).toBe(1);
            expect(ifconfig_reply.status).toBe(IFCONFIG_STATUS_APPLY_FAILED);
            expect((const char *)ifconfig_reply.detail).toBe("Operation not supported");
        });

        it("emits an interface status immediately on entering running", []() {
            MessageHeader header;
            int status_index = transport.control_count - 1;

            MessageHeader_Decode(&header, transport.control_log[status_index], transport.control_sizes[status_index]);

            expect(header.type).toBe(kMESSAGE_TYPE_INTERFACE_STATUS);
        });

        it("reports tx-drop counters in the interface status", []() {
            FrameMessage injected = { 0x456, 2000, 7, 1, 0, 0, { 0x55 } };
            InterfaceStatusMessage status;
            MessageHeader header;
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&injected, encoded_frame, sizeof(encoded_frame));
            int status_index;

            can.write_result = false;
            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);
            Agent_Tick(&agent, AGENT_STATUS_PERIOD_MS * 1000);

            status_index = transport.control_count - 1;
            MessageHeader_Decode(&header, transport.control_log[status_index], transport.control_sizes[status_index]);
            InterfaceStatusMessage_Decode(&status, transport.control_log[status_index] + MESSAGE_HEADER_SIZE, header.length);

            expect(header.type).toBe(kMESSAGE_TYPE_INTERFACE_STATUS);
            expect(status.interface_count).toBe(2);
            expect(status.entries[0].channel).toBe(7);
            expect(status.entries[0].tx_dropped).toBe((uint64_t)1);
            expect(status.entries[1].channel).toBe(9);
            expect(status.entries[1].tx_dropped).toBe((uint64_t)0);
        });

        it("does not re-emit the interface status until the period elapses", []() {
            int after_running = transport.control_count;

            Agent_Tick(&agent, AGENT_STATUS_PERIOD_MS * 1000 - 1);

            expect(transport.control_count).toBe(after_running);
        });

        it("re-emits the interface status after the period elapses", []() {
            int after_running = transport.control_count;

            Agent_Tick(&agent, AGENT_STATUS_PERIOD_MS * 1000);

            expect(transport.control_count).toBe(after_running + 1);
        });

        it("advertises the interface bitrate in the status", []() {
            InterfaceStatusMessage status;
            MessageHeader header;
            int status_index;

            can.bitrate_value = 500000;
            Agent_Tick(&agent, AGENT_STATUS_PERIOD_MS * 1000);

            status_index = transport.control_count - 1;
            MessageHeader_Decode(&header, transport.control_log[status_index], transport.control_sizes[status_index]);
            InterfaceStatusMessage_Decode(&status, transport.control_log[status_index] + MESSAGE_HEADER_SIZE, header.length);

            expect(status.entries[0].advertised_rate).toBe((uint32_t)500000);
            expect(status.entries[1].advertised_rate).toBe((uint32_t)500000);
        });
    });

    describe("transactions", []() {
        beforeEach([]() {
            enterRunning();
        });

        it("advertises transactions in its hello", []() {
            HelloMessage hello;

            HelloMessage_Decode(&hello, transport.control_log[0] + MESSAGE_HEADER_SIZE, transport.control_sizes[0] - MESSAGE_HEADER_SIZE);

            expect((hello.capabilities & HELLO_CAP_TRANSACTIONS) != 0).toBe(true);
        });

        it("answers a begin on an assigned channel with its ring window", []() {
            TxnBeginAckMessage ack;

            ack = sendTxnBegin(TXN_CHANNEL);

            expect(lastControlType()).toBe(kMESSAGE_TYPE_TXN_BEGIN_ACK);
            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_OK);
            expect(ack.channel).toBe(TXN_CHANNEL);
            expect(ack.window_bytes).toBe((uint32_t)(TXN_RING_BYTES - TXN_STEP_RECORD_SIZE_MAX));
        });

        it("rejects a begin on a channel it was never assigned", []() {
            TxnBeginAckMessage ack;

            ack = sendTxnBegin(UNASSIGNED_CHANNEL);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_INVALID);
        });

        it("runs steps that arrive on the channel's reliable stream", []() {
            TxnBeginAckMessage ack;
            uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_STEPS_BODY_SIZE_MAX];
            size_t encoded_size;

            ack = sendTxnBegin(TXN_CHANNEL);
            encoded_size = encodeResponseTemplate(ack.txn_id, encoded, sizeof(encoded));
            Agent_OnTransportFrame(&agent, encoded, encoded_size);
            encoded_size = encodeLastRequestStep(ack.txn_id, encoded, sizeof(encoded));
            Agent_OnTransportFrame(&agent, encoded, encoded_size);

            Agent_Tick(&agent, 10);

            expect(can.write_count).toBe(1);
            expect(can.last_interface_index).toBe(0);
            expect(can.last_frame.can_id).toBe((uint32_t)REQUEST_ID);
            expect(Agent_NextWakeUs(&agent)).toBe((uint64_t)(10 + TXN_TIMEOUT_MS * 1000));
        });

        it("runs steps that arrive on the control stream", []() {
            TxnBeginAckMessage ack;
            uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_STEPS_BODY_SIZE_MAX];
            size_t encoded_size;

            ack = sendTxnBegin(TXN_CHANNEL);
            encoded_size = encodeResponseTemplate(ack.txn_id, encoded, sizeof(encoded));
            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);
            encoded_size = encodeLastRequestStep(ack.txn_id, encoded, sizeof(encoded));
            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);

            Agent_Tick(&agent, 10);

            expect(can.write_count).toBe(1);
        });

        it("reports the outcome on the control stream once the answer arrives", []() {
            FrameMessage response = { RESPONSE_ID, 0, 0, 2, 0, 0, { 0x01, 0x76 } };
            TxnBeginAckMessage ack;
            TxnStatusMessage status;
            uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_STEPS_BODY_SIZE_MAX];
            size_t encoded_size;

            ack = sendTxnBegin(TXN_CHANNEL);
            encoded_size = encodeResponseTemplate(ack.txn_id, encoded, sizeof(encoded));
            Agent_OnTransportFrame(&agent, encoded, encoded_size);
            encoded_size = encodeLastRequestStep(ack.txn_id, encoded, sizeof(encoded));
            Agent_OnTransportFrame(&agent, encoded, encoded_size);
            Agent_Tick(&agent, 10);

            Agent_OnCanFrame(&agent, 0, &response);
            Agent_Tick(&agent, 20);
            status = lastTxnStatus();

            expect(lastControlType()).toBe(kMESSAGE_TYPE_TXN_STATUS);
            expect(status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(status.channel).toBe(TXN_CHANNEL);
            expect(status.txn_id).toBe(ack.txn_id);
        });

        it("matches a bus frame once even when the uplink refuses it first", []() {
            FrameMessage response = { RESPONSE_ID, 0, 0, 2, 0, 0, { 0x01, 0x76 } };
            TxnBeginAckMessage ack;
            uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_STEPS_BODY_SIZE_MAX];
            size_t encoded_size;
            bool refused_first;

            ack = sendTxnBegin(TXN_CHANNEL);
            encoded_size = encodeResponseTemplate(ack.txn_id, encoded, sizeof(encoded));
            encoded[MESSAGE_HEADER_SIZE + TXN_TEMPLATE_COUNT_K_BODY_OFFSET] = 2;
            Agent_OnTransportFrame(&agent, encoded, encoded_size);
            encoded_size = encodeLastRequestStep(ack.txn_id, encoded, sizeof(encoded));
            Agent_OnTransportFrame(&agent, encoded, encoded_size);
            Agent_Tick(&agent, 10);
            transport.frame_result = false;
            refused_first = !Agent_OnCanFrame(&agent, 0, &response);
            transport.frame_result = true;

            Agent_OnCanFrame(&agent, 0, &response);
            Agent_Tick(&agent, 20);

            expect(refused_first).toBe(true);
            expect(agent.transactions.current.match_count).toBe(1);
            expect(TransactionRunner_IsActive(&agent.transactions)).toBe(true);
        });

        it("drops the transaction when the hub connection is lost", []() {
            sendTxnBegin(TXN_CHANNEL);

            Agent_OnDisconnected(&agent, 0);

            expect(TransactionRunner_IsActive(&agent.transactions)).toBe(false);
            expect(Agent_NextWakeUs(&agent)).toBe((uint64_t)0);
        });
    });
});

static InterfaceSettings bitrateSettings(uint32_t bitrate)
{
    InterfaceSettings settings;

    memset(&settings, 0, sizeof(settings));
    settings.fields = INTERFACE_FIELD_BITRATE;
    settings.bitrate = bitrate;

    return settings;
}

static void sendIfconfig(const char *interface_name, uint8_t op, const InterfaceSettings *settings)
{
    IfconfigMessage request;
    MessageHeader header;
    uint8_t encoded[MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE];
    size_t encoded_size;
    int reply_index;

    memset(&request, 0, sizeof(request));
    request.request_id = IFCONFIG_REQUEST_ID;
    snprintf(request.interface_name, sizeof(request.interface_name), "%s", interface_name);
    request.op = op;
    request.settings = *settings;
    encoded_size = IfconfigMessage_Encode(&request, encoded, sizeof(encoded));

    Agent_OnControlMessage(&agent, encoded, encoded_size, 0);
    reply_index = transport.control_count - 1;
    MessageHeader_Decode(&header, transport.control_log[reply_index], transport.control_sizes[reply_index]);
    IfconfigReplyMessage_Decode(&ifconfig_reply, transport.control_log[reply_index] + MESSAGE_HEADER_SIZE, header.length);
    ifconfig_reply_type = header.type;
}

static void enterRunning(void)
{
    uint8_t encoded[64];
    size_t encoded_size;

    TransportPortMock_Reset(&transport);
    CanPortMock_Reset(&can);
    Agent_Init(&agent, &transport.port, &can.port, &registration);
    Agent_Tick(&agent, 0);
    Agent_OnConnected(&agent);
    encoded_size = RegisterAckMessage_Encode(&ack_ok, encoded, sizeof(encoded));
    Agent_OnControlMessage(&agent, encoded, encoded_size, 0);
}

static TxnBeginAckMessage sendTxnBegin(uint8_t channel)
{
    TxnBeginMessage begin;
    TxnBeginAckMessage ack;
    uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_BEGIN_BODY_SIZE];
    size_t encoded_size;
    uint32_t reply_index;

    memset(&begin, 0, sizeof(begin));
    begin.channel = channel;
    encoded_size = TxnBeginMessage_Encode(&begin, encoded, sizeof(encoded));
    Agent_OnControlMessage(&agent, encoded, encoded_size, 0);

    reply_index = (uint32_t)transport.control_count - 1;
    TxnBeginAckMessage_Decode(&ack, transport.control_log[reply_index] + MESSAGE_HEADER_SIZE, transport.control_sizes[reply_index] - MESSAGE_HEADER_SIZE);
    return ack;
}

static size_t encodeResponseTemplate(uint16_t txn_id, uint8_t *buffer, size_t buffer_size)
{
    TxnTemplateMessage template_message;

    memset(&template_message, 0, sizeof(template_message));
    template_message.channel = TXN_CHANNEL;
    template_message.txn_id = txn_id;
    template_message.advance_mode = kTXN_ADVANCE_MODE_COUNT;
    template_message.count_k = 1;
    template_message.patterns_present = 1u << kTXN_PATTERN_ADVANCE;
    template_message.timeout_ms = TXN_TIMEOUT_MS;
    template_message.patterns[kTXN_PATTERN_ADVANCE].can_id = RESPONSE_ID;
    template_message.patterns[kTXN_PATTERN_ADVANCE].can_mask = ID_MASK;
    return TxnTemplateMessage_Encode(&template_message, buffer, buffer_size);
}

static size_t encodeLastRequestStep(uint16_t txn_id, uint8_t *buffer, size_t buffer_size)
{
    TxnStepsMessage fixed_fields = { TXN_CHANNEL, 0, txn_id, 0, TXN_STEPS_FLAG_END, NULL, 0 };
    TxnStepsWriter writer;
    TxnStep step;

    memset(&step, 0, sizeof(step));
    step.can_id = REQUEST_ID;
    step.payload_length = 2;
    step.payload[0] = 0x01;
    step.payload[1] = 0x36;
    TxnStepsWriter_Init(&writer, &fixed_fields, buffer, buffer_size);
    TxnStepsWriter_Append(&writer, &step);
    return TxnStepsWriter_Finish(&writer);
}

static uint8_t lastControlType(void)
{
    uint32_t last_index = (uint32_t)transport.control_count - 1;

    return transport.control_log[last_index][0];
}

static TxnStatusMessage lastTxnStatus(void)
{
    TxnStatusMessage status;
    uint32_t last_index = (uint32_t)transport.control_count - 1;

    TxnStatusMessage_Decode(&status, transport.control_log[last_index] + MESSAGE_HEADER_SIZE, transport.control_sizes[last_index] - MESSAGE_HEADER_SIZE);
    return status;
}
