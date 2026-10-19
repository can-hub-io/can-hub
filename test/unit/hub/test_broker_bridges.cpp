#include <cest>

#include <cstring>

extern "C" {
#include "hub/broker.h"
#include "broker_driver.h"
#include "hub_transport_port_mock.h"
#include "protocol/admin_message.h"
#include "protocol/frame_message.h"
#include "protocol/interface_name.h"
#include "protocol/message_header.h"
}

#define TRUCK_PEER 100
#define BUS_PEER 101
#define CLIENT_PEER 200
#define ADMIN_PEER 300
#define TRUCK_CAN0_CHANNEL 0
#define BUS_CAN0_CHANNEL 0
#define BUS_INTERFACE_INDEX 2
#define PASSED_ID 0x100
#define FILTERED_ID 0x200
#define EXACT_MASK FRAME_CAN_ID_MASK
#define ADMIN_REQUEST_BUFFER_SIZE 512
#define FRAME_BUFFER_SIZE 128

typedef struct {
    BridgeStorePort port;
    BridgeRoute routes[BRIDGE_ROUTES_MAX];
    uint16_t route_count;
    bool save_result;
} BridgeStoreMock;

static Broker broker;
static HubTransportPortMock transport;
static BridgeStoreMock store;
static HubTransportEvents events;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };
static const RegisterMessage bus_registration = { "bus7", 1, { "can0" } };
static uint8_t reply_status;
static AdminBridgeListReplyMessage listed;
static FrameMessage injected;

static void resetStore(void);
static void startHub(void);
static void connectAgents(void);
static void addBridge(const char *source, const char *destination, uint8_t id_count, const uint32_t *ids);
static void deleteBridge(const char *source, const char *destination);
static void listBridges(void);
static void frameFromAgent(uint32_t agent_peer_id, uint8_t channel, uint32_t can_id, uint8_t route_flags);
static void echoFromBus(uint8_t token);
static uint8_t framesTo(uint32_t peer_id);
static FrameMessage lastFrameTo(uint32_t peer_id);
static uint8_t tokenOf(const FrameMessage *frame);
static void openBusAsClient(void);
static void storeRoute(const char *source, const char *destination);
static void endpointFrom(const char *text, AdminBridgeEndpoint *endpoint);
static void sendAdmin(const uint8_t *encoded, size_t encoded_size);
static bool storeSave(void *context, const BridgeRoute *route);
static bool storeRemove(void *context, const BridgeEndpoint *source, const BridgeEndpoint *destination);
static bool storeLoad(void *context, uint16_t position, BridgeRoute *route);

describe("broker bus bridges", []() {
    describe("routing", []() {
        beforeEach([]() {
            resetStore();
            startHub();
            connectAgents();
        });

        it("injects a frame of the source bus into the destination bus", []() {
            addBridge("truck42/can0", "bus7/can0", 0, NULL);

            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, PASSED_ID, 0);
            injected = lastFrameTo(BUS_PEER);

            expect(framesTo(BUS_PEER)).toBe(1);
            expect(injected.channel).toBe(BUS_CAN0_CHANNEL);
            expect(injected.can_id).toBe((uint32_t)PASSED_ID);
            expect(tokenOf(&injected) != FRAME_ROUTE_NO_TOKEN).toBe(true);
        });

        it("injects only the ids its filters let through", []() {
            const uint32_t ids[] = { PASSED_ID };

            addBridge("truck42/can0", "bus7/can0", 1, ids);

            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, PASSED_ID, 0);
            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, FILTERED_ID, 0);
            injected = lastFrameTo(BUS_PEER);

            expect(framesTo(BUS_PEER)).toBe(1);
            expect(injected.can_id).toBe((uint32_t)PASSED_ID);
        });

        it("never bridges an error frame", []() {
            addBridge("truck42/can0", "bus7/can0", 0, NULL);

            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, FRAME_CAN_ID_FLAG_ERR | PASSED_ID, 0);

            expect(framesTo(BUS_PEER)).toBe(0);
        });

        it("never bridges the echo of a bridged frame back", []() {
            addBridge("truck42/can0", "bus7/can0", 0, NULL);
            addBridge("bus7/can0", "truck42/can0", 0, NULL);
            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, PASSED_ID, 0);
            injected = lastFrameTo(BUS_PEER);
            HubTransportPortMock_Reset(&transport);

            echoFromBus(tokenOf(&injected));

            expect(framesTo(TRUCK_PEER)).toBe(0);
        });

        it("bridges traffic of the destination bus that did not come over the bridge", []() {
            addBridge("truck42/can0", "bus7/can0", 0, NULL);
            addBridge("bus7/can0", "truck42/can0", 0, NULL);

            frameFromAgent(BUS_PEER, BUS_CAN0_CHANNEL, PASSED_ID, 0);

            expect(framesTo(TRUCK_PEER)).toBe(1);
        });

        it("marks the echo of a bridged frame as bridged for clients", []() {
            addBridge("truck42/can0", "bus7/can0", 0, NULL);
            openBusAsClient();
            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, PASSED_ID, 0);
            injected = lastFrameTo(BUS_PEER);
            HubTransportPortMock_Reset(&transport);

            echoFromBus(tokenOf(&injected));
            injected = lastFrameTo(CLIENT_PEER);

            expect(framesTo(CLIENT_PEER)).toBe(1);
            expect((injected.route_flags & FRAME_ROUTE_FLAG_BRIDGED) != 0).toBe(true);
        });

        it("counts a frame for an offline destination as dropped", []() {
            addBridge("truck42/can0", "ghost/can0", 0, NULL);

            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, PASSED_ID, 0);
            listBridges();

            expect(listed.count).toBe(1);
            expect(listed.entries[0].dropped).toBe((uint64_t)1);
            expect(listed.entries[0].forwarded).toBe((uint64_t)0);
            expect(listed.entries[0].state).toBe((uint8_t)ADMIN_BRIDGE_STATE_SOURCE_ONLINE);
        });

        it("counts what it forwards", []() {
            addBridge("truck42/can0", "bus7/can0", 0, NULL);
            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, PASSED_ID, 0);
            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, PASSED_ID, 0);

            listBridges();

            expect(listed.entries[0].forwarded).toBe((uint64_t)2);
            expect(listed.entries[0].state).toBe((uint8_t)(ADMIN_BRIDGE_STATE_SOURCE_ONLINE | ADMIN_BRIDGE_STATE_DESTINATION_ONLINE));
        });
    });

    describe("administration", []() {
        beforeEach([]() {
            resetStore();
            startHub();
        });

        it("replaces the filters of a route set again", []() {
            const uint32_t ids[] = { PASSED_ID, FILTERED_ID };

            addBridge("truck42/can0", "bus7/can0", 1, ids);

            addBridge("truck42/can0", "bus7/can0", 2, ids);
            listBridges();

            expect(reply_status).toBe((uint8_t)ADMIN_BRIDGE_STATUS_OK);
            expect(listed.count).toBe(1);
            expect(listed.entries[0].filter_count).toBe(2);
            expect(listed.entries[0].filters[1].can_id).toBe((uint32_t)FILTERED_ID);
        });

        it("refuses a route from a bus into itself", []() {
            addBridge("truck42/can0", "truck42/can0", 0, NULL);

            expect(reply_status).toBe((uint8_t)ADMIN_BRIDGE_STATUS_INVALID);
        });

        it("deletes a route", []() {
            addBridge("truck42/can0", "bus7/can0", 0, NULL);

            deleteBridge("truck42/can0", "bus7/can0");
            listBridges();

            expect(reply_status).toBe((uint8_t)ADMIN_BRIDGE_STATUS_OK);
            expect(listed.count).toBe(0);
            expect(store.route_count).toBe((uint16_t)0);
        });

        it("reports a route it does not hold", []() {
            deleteBridge("truck42/can0", "bus7/can0");

            expect(reply_status).toBe((uint8_t)ADMIN_BRIDGE_STATUS_UNKNOWN);
        });

        it("keeps a route the store could not save out of the table", []() {
            store.save_result = false;

            addBridge("truck42/can0", "bus7/can0", 0, NULL);
            listBridges();

            expect(reply_status).toBe((uint8_t)ADMIN_BRIDGE_STATUS_STORE_FAILED);
            expect(listed.count).toBe(0);
        });

        it("writes a new route to the store", []() {
            addBridge("truck42/can0", "bus7/can0", 0, NULL);

            expect(store.route_count).toBe((uint16_t)1);
            expect((const char *)store.routes[0].destination.agent_name).toBe("bus7");
        });

        it("pages a list longer than one reply", []() {
            char destination[32];
            uint8_t i;

            for(i=0; i<ADMIN_BRIDGE_LIST_REPLY_ENTRIES_MAX + 1; i++) {
                snprintf(destination, sizeof(destination), "bus%u/can0", i);
                addBridge("truck42/can0", destination, 0, NULL);
            }

            listBridges();

            expect(listed.count).toBe((uint8_t)ADMIN_BRIDGE_LIST_REPLY_ENTRIES_MAX);
            expect((listed.flags & ADMIN_REPLY_FLAG_MORE) != 0).toBe(true);
        });
    });

    describe("start", []() {
        it("applies the routes the store holds", []() {
            resetStore();
            storeRoute("truck42/can0", "bus7/can0");
            startHub();
            connectAgents();

            frameFromAgent(TRUCK_PEER, TRUCK_CAN0_CHANNEL, PASSED_ID, 0);

            expect(framesTo(BUS_PEER)).toBe(1);
        });
    });
});

/* ---------- private ---------- */

static void resetStore(void)
{
    memset(&store, 0, sizeof(store));
    store.port.context = &store;
    store.port.save = storeSave;
    store.port.remove = storeRemove;
    store.port.load = storeLoad;
    store.save_result = true;
}

static void startHub(void)
{
    HubTransportPortMock_Reset(&transport);
    Broker_Init(&broker, &transport.port, NULL, NULL, &store.port, false);
    events = Broker_Events(&broker);
    BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
    HubTransportPortMock_Reset(&transport);
}

static void connectAgents(void)
{
    BrokerDriver_ConnectAgent(&events, &transport, TRUCK_PEER, &truck_registration);
    BrokerDriver_ConnectAgent(&events, &transport, BUS_PEER, &bus_registration);
    HubTransportPortMock_Reset(&transport);
}

static void addBridge(const char *source, const char *destination, uint8_t id_count, const uint32_t *ids)
{
    AdminBridgeSetMessage request;
    uint8_t encoded[ADMIN_REQUEST_BUFFER_SIZE];
    uint8_t i;

    memset(&request, 0, sizeof(request));
    endpointFrom(source, &request.source);
    endpointFrom(destination, &request.destination);
    request.filter_count = id_count;
    for(i=0; i<id_count; i++) {
        request.filters[i].can_id = ids[i];
        request.filters[i].can_mask = EXACT_MASK;
    }

    sendAdmin(encoded, AdminBridgeSetMessage_Encode(&request, encoded, sizeof(encoded)));
}

static void deleteBridge(const char *source, const char *destination)
{
    AdminBridgeDeleteMessage request;
    uint8_t encoded[ADMIN_REQUEST_BUFFER_SIZE];

    memset(&request, 0, sizeof(request));
    endpointFrom(source, &request.source);
    endpointFrom(destination, &request.destination);

    sendAdmin(encoded, AdminBridgeDeleteMessage_Encode(&request, encoded, sizeof(encoded)));
}

static void listBridges(void)
{
    AdminBridgeListMessage request = { 0 };
    uint8_t encoded[ADMIN_REQUEST_BUFFER_SIZE];

    sendAdmin(encoded, AdminBridgeListMessage_Encode(&request, encoded, sizeof(encoded)));
}

static void frameFromAgent(uint32_t agent_peer_id, uint8_t channel, uint32_t can_id, uint8_t route_flags)
{
    FrameMessage frame = { can_id, 1000, channel, 1, 0, route_flags, { 0xAA } };
    uint8_t encoded[FRAME_BUFFER_SIZE];
    size_t encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

    events.on_peer_frame(events.context, agent_peer_id, encoded, encoded_size);
}

static void echoFromBus(uint8_t token)
{
    uint8_t route_flags = (uint8_t)(FRAME_ROUTE_FLAG_ECHO | (token << FRAME_ROUTE_TOKEN_SHIFT));

    frameFromAgent(BUS_PEER, BUS_CAN0_CHANNEL, PASSED_ID, route_flags);
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

static FrameMessage lastFrameTo(uint32_t peer_id)
{
    FrameMessage frame;
    MessageHeader header;
    int32_t i;

    memset(&frame, 0, sizeof(frame));
    for(i=0; i<transport.frame_count; i++) {
        if (transport.frame_peers[i] != peer_id) {
            continue;
        }
        MessageHeader_Decode(&header, transport.frame_log[i], transport.frame_sizes[i]);
        FrameMessage_Decode(&frame, transport.frame_log[i] + MESSAGE_HEADER_SIZE, header.length);
    }

    return frame;
}

static uint8_t tokenOf(const FrameMessage *frame)
{
    return (uint8_t)((frame->route_flags & FRAME_ROUTE_TOKEN_MASK) >> FRAME_ROUTE_TOKEN_SHIFT);
}

static void openBusAsClient(void)
{
    uint32_t interface_id;

    BrokerDriver_ConnectClient(&events, CLIENT_PEER);
    interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, BUS_INTERFACE_INDEX);
    BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id, 0);
    HubTransportPortMock_Reset(&transport);
}

static void storeRoute(const char *source, const char *destination)
{
    BridgeRoute *route = &store.routes[store.route_count++];

    memset(route, 0, sizeof(*route));
    InterfaceName_Split(source, route->source.agent_name, route->source.interface_name);
    InterfaceName_Split(destination, route->destination.agent_name, route->destination.interface_name);
}

static void endpointFrom(const char *text, AdminBridgeEndpoint *endpoint)
{
    InterfaceName_Split(text, endpoint->agent_name, endpoint->interface_name);
}

static void sendAdmin(const uint8_t *encoded, size_t encoded_size)
{
    MessageHeader header;
    const uint8_t *reply;
    AdminBridgeSetReplyMessage set_reply;
    AdminBridgeDeleteReplyMessage delete_reply;

    HubTransportPortMock_Reset(&transport);
    events.on_peer_control(events.context, ADMIN_PEER, encoded, encoded_size, 0);

    reply = transport.control_log[transport.control_count - 1];
    MessageHeader_Decode(&header, reply, transport.control_sizes[transport.control_count - 1]);
    if (header.type == kMESSAGE_TYPE_ADMIN_BRIDGE_SET_REPLY) {
        AdminBridgeSetReplyMessage_Decode(&set_reply, reply + MESSAGE_HEADER_SIZE, header.length);
        reply_status = set_reply.status;
    } else if (header.type == kMESSAGE_TYPE_ADMIN_BRIDGE_DELETE_REPLY) {
        AdminBridgeDeleteReplyMessage_Decode(&delete_reply, reply + MESSAGE_HEADER_SIZE, header.length);
        reply_status = delete_reply.status;
    } else if (header.type == kMESSAGE_TYPE_ADMIN_BRIDGE_LIST_REPLY) {
        AdminBridgeListReplyMessage_Decode(&listed, reply + MESSAGE_HEADER_SIZE, header.length);
    }
}

static bool storeSave(void *context, const BridgeRoute *route)
{
    BridgeStoreMock *self = (BridgeStoreMock *)context;
    uint16_t i;

    if (!self->save_result) {
        return false;
    }
    for(i=0; i<self->route_count; i++) {
        if (BridgeEndpoint_Equals(&self->routes[i].source, &route->source)
            && BridgeEndpoint_Equals(&self->routes[i].destination, &route->destination)) {
            self->routes[i] = *route;
            return true;
        }
    }
    self->routes[self->route_count++] = *route;

    return true;
}

static bool storeRemove(void *context, const BridgeEndpoint *source, const BridgeEndpoint *destination)
{
    BridgeStoreMock *self = (BridgeStoreMock *)context;
    uint16_t i;

    for(i=0; i<self->route_count; i++) {
        if (BridgeEndpoint_Equals(&self->routes[i].source, source)
            && BridgeEndpoint_Equals(&self->routes[i].destination, destination)) {
            self->routes[i] = self->routes[--self->route_count];
            return true;
        }
    }

    return true;
}

static bool storeLoad(void *context, uint16_t position, BridgeRoute *route)
{
    BridgeStoreMock *self = (BridgeStoreMock *)context;

    if (position >= self->route_count) {
        return false;
    }

    *route = self->routes[position];

    return true;
}
