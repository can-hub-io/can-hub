#include "socketcand/socketcand_bridge.h"

#include <string.h>

#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "socketcand/domain/beacon_xml.h"
#include "socketcand/domain/socketcand_codec.h"

#define WIRE_BUFFER_SIZE 128
#define ASCII_BUFFER_SIZE 256
#define BEACON_BUFFER_SIZE 8192

#define BEACON_INTERVAL_US 3000000u
#define LIST_REFRESH_INTERVAL_US 5000000u
#define RECONNECT_DELAY_US 1000000u

#define OPEN_FLAGS_READ_ONLY OPEN_FLAG_SUPPRESS_OWN_ECHO
#define OPEN_FLAGS_READ_WRITE (OPEN_FLAG_SUPPRESS_OWN_ECHO | OPEN_FLAG_WANT_WRITE)

static void transportOnConnected(void *context);
static void transportOnDisconnected(void *context, uint64_t now_us);
static void transportOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void transportOnFrame(void *context, const uint8_t *data, size_t size);
static void serverOnClientConnected(void *context, uint32_t connection_id);
static void serverOnClientBytes(void *context, uint32_t connection_id, const uint8_t *data, size_t size);
static void serverOnClientDisconnected(void *context, uint32_t connection_id);

static void tryConnect(SocketcandBridge *self, uint64_t now_us);
static void startListing(SocketcandBridge *self);
static void handleListReply(SocketcandBridge *self, const uint8_t *body, uint16_t length, uint64_t now_us);
static void handleOpenAck(SocketcandBridge *self, const OpenAckMessage *ack);
static void deliverFrame(SocketcandBridge *self, const uint8_t *data, size_t size);
static bool dispatchCommand(SocketcandBridge *self, SocketcandConnection *connection, const SocketcandCommand *command);
static bool openBus(SocketcandBridge *self, SocketcandConnection *connection, const char *bus);
static void handleSend(SocketcandBridge *self, SocketcandConnection *connection, const FrameMessage *frame);
static void sendHello(SocketcandBridge *self);
static void sendListPage(SocketcandBridge *self, uint16_t offset);
static void sendOpen(SocketcandBridge *self, uint32_t interface_id, uint8_t flags);
static void sendClose(SocketcandBridge *self, uint8_t channel);
static void emitBeacon(SocketcandBridge *self);
static void reattachRenewedInterfaces(SocketcandBridge *self);
static void closeAllConnections(SocketcandBridge *self);
static void closeConnection(SocketcandBridge *self, SocketcandConnection *connection, const char *detail);
static void sendAscii(SocketcandBridge *self, uint32_t connection_id, const char *text, size_t length);
static void copyString(char *destination, size_t destination_size, const char *source);

/* ---------- public ---------- */

void SocketcandBridge_Init(SocketcandBridge *self, TransportPort *hub, SocketcandServerPort *server, const char *device_name, const char *beacon_url, bool beacon_enabled)
{
    memset(self, 0, sizeof(*self));
    self->hub = hub;
    self->server = server;
    InterfaceCatalogue_Reset(&self->catalogue);
    ConnectionTable_Reset(&self->connections);
    self->hub_state = kSOCKETCAND_HUB_DISCONNECTED;
    self->beacon_enabled = beacon_enabled;
    copyString(self->device_name, sizeof(self->device_name), device_name);
    copyString(self->beacon_url, sizeof(self->beacon_url), beacon_url);
}

TransportEvents SocketcandBridge_TransportEvents(SocketcandBridge *self)
{
    TransportEvents events = {
        .context = self,
        .on_connected = transportOnConnected,
        .on_disconnected = transportOnDisconnected,
        .on_control = transportOnControl,
        .on_frame = transportOnFrame,
    };

    return events;
}

SocketcandServerEvents SocketcandBridge_ServerEvents(SocketcandBridge *self)
{
    SocketcandServerEvents events = {
        .context = self,
        .on_client_connected = serverOnClientConnected,
        .on_client_bytes = serverOnClientBytes,
        .on_client_disconnected = serverOnClientDisconnected,
    };

    return events;
}

void SocketcandBridge_Tick(SocketcandBridge *self, uint64_t now_us)
{
    if (self->hub_state == kSOCKETCAND_HUB_DISCONNECTED) {
        if (now_us >= self->next_connect_at_us) {
            tryConnect(self, now_us);
        }
        return;
    }
    if (self->hub_state != kSOCKETCAND_HUB_READY) {
        return;
    }

    if (!self->listing_active && now_us >= self->next_list_at_us) {
        startListing(self);
    }
    if (self->beacon_enabled && now_us >= self->next_beacon_at_us) {
        emitBeacon(self);
        self->next_beacon_at_us = now_us + BEACON_INTERVAL_US;
    }
}

uint8_t SocketcandBridge_HubState(const SocketcandBridge *self)
{
    return self->hub_state;
}

/* ---------- private ---------- */

static void transportOnConnected(void *context)
{
    SocketcandBridge *self = context;

    if (self->hub_state != kSOCKETCAND_HUB_CONNECTING) {
        return;
    }

    sendHello(self);
    startListing(self);
    self->hub_state = kSOCKETCAND_HUB_READY;
    self->next_beacon_at_us = 0;
}

static void transportOnDisconnected(void *context, uint64_t now_us)
{
    SocketcandBridge *self = context;

    if (self->hub_state == kSOCKETCAND_HUB_DISCONNECTED) {
        return;
    }

    closeAllConnections(self);
    InterfaceCatalogue_Reset(&self->catalogue);
    self->listing_active = false;
    self->hub_state = kSOCKETCAND_HUB_DISCONNECTED;
    self->next_connect_at_us = now_us + RECONNECT_DELAY_US;
    self->next_beacon_at_us = 0;
    self->next_list_at_us = 0;
}

static void transportOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    SocketcandBridge *self = context;
    MessageHeader header;
    OpenAckMessage ack;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }

    if (header.type == kMESSAGE_TYPE_LIST_REPLY) {
        handleListReply(self, data + MESSAGE_HEADER_SIZE, header.length, now_us);
        return;
    }
    if (header.type == kMESSAGE_TYPE_OPEN_ACK) {
        if (OpenAckMessage_Decode(&ack, data + MESSAGE_HEADER_SIZE, header.length)) {
            handleOpenAck(self, &ack);
        }
    }
}

static void transportOnFrame(void *context, const uint8_t *data, size_t size)
{
    deliverFrame(context, data, size);
}

static void serverOnClientConnected(void *context, uint32_t connection_id)
{
    SocketcandBridge *self = context;
    SocketcandConnection *connection = ConnectionTable_Add(&self->connections, connection_id);
    char ascii[ASCII_BUFFER_SIZE];
    size_t length;

    if (connection == NULL) {
        self->server->close_client(self->server->context, connection_id);
        return;
    }

    length = SocketcandCodec_RenderGreeting(ascii, sizeof(ascii));
    sendAscii(self, connection_id, ascii, length);
}

static void serverOnClientBytes(void *context, uint32_t connection_id, const uint8_t *data, size_t size)
{
    SocketcandBridge *self = context;
    SocketcandConnection *connection = ConnectionTable_Find(&self->connections, connection_id);
    SocketcandCommand command;
    char text[ASCII_BUFFER_SIZE];
    size_t length;

    if (connection == NULL) {
        return;
    }

    AsciiFramer_Push(&connection->framer, data, size);
    while (AsciiFramer_Next(&connection->framer, text, sizeof(text), &length)) {
        if (!SocketcandCodec_Parse(text, length, &command)) {
            continue;
        }
        if (!dispatchCommand(self, connection, &command)) {
            return;
        }
    }
}

static void serverOnClientDisconnected(void *context, uint32_t connection_id)
{
    SocketcandBridge *self = context;
    SocketcandConnection *connection = ConnectionTable_Find(&self->connections, connection_id);

    if (connection == NULL) {
        return;
    }
    if (connection->channel_valid && self->hub_state == kSOCKETCAND_HUB_READY) {
        sendClose(self, connection->channel);
    }
    ConnectionTable_Remove(&self->connections, connection_id);
}

static void tryConnect(SocketcandBridge *self, uint64_t now_us)
{
    if (self->hub->connect(self->hub->context)) {
        self->hub_state = kSOCKETCAND_HUB_CONNECTING;
        return;
    }

    self->next_connect_at_us = now_us + RECONNECT_DELAY_US;
}

static void startListing(SocketcandBridge *self)
{
    InterfaceCatalogue_Reset(&self->catalogue);
    self->listing_offset = 0;
    self->listing_active = true;
    sendListPage(self, 0);
}

static void handleListReply(SocketcandBridge *self, const uint8_t *body, uint16_t length, uint64_t now_us)
{
    ListReplyMessage reply;

    if (!ListReplyMessage_Decode(&reply, body, length)) {
        self->listing_active = false;
        self->next_list_at_us = now_us + LIST_REFRESH_INTERVAL_US;
        return;
    }

    InterfaceCatalogue_AppendPage(&self->catalogue, &reply);
    self->listing_offset = (uint16_t)(self->listing_offset + reply.count);

    if ((reply.flags & LIST_REPLY_FLAG_MORE) != 0 && reply.count > 0) {
        sendListPage(self, self->listing_offset);
        return;
    }

    self->listing_active = false;
    self->next_list_at_us = now_us + LIST_REFRESH_INTERVAL_US;
    reattachRenewedInterfaces(self);
}

static void handleOpenAck(SocketcandBridge *self, const OpenAckMessage *ack)
{
    SocketcandConnection *connection = ConnectionTable_FindPendingOpen(&self->connections, ack->interface_id);
    char ascii[ASCII_BUFFER_SIZE];
    size_t length;

    if (connection == NULL) {
        return;
    }

    if (ack->status == OPEN_STATUS_OK) {
        connection->channel = ack->channel;
        connection->channel_valid = true;
        connection->can_write = (connection->open_state == kSOCKETCAND_OPEN_PENDING_WRITE);
        connection->open_state = kSOCKETCAND_OPEN_DONE;
        if (connection->reattaching) {
            connection->reattaching = false;
            return;
        }
        connection->mode = kSOCKETCAND_MODE_BCM;
        length = SocketcandCodec_RenderOk(ascii, sizeof(ascii));
        sendAscii(self, connection->connection_id, ascii, length);
        return;
    }

    if (connection->open_state == kSOCKETCAND_OPEN_PENDING_WRITE && ack->status == OPEN_STATUS_WRITE_DENIED) {
        connection->open_state = kSOCKETCAND_OPEN_PENDING_READ;
        sendOpen(self, connection->interface_id, OPEN_FLAGS_READ_ONLY);
        return;
    }

    closeConnection(self, connection, "open rejected");
}

static void deliverFrame(SocketcandBridge *self, const uint8_t *data, size_t size)
{
    MessageHeader header;
    FrameMessage frame;
    SocketcandConnection *connection;
    char ascii[ASCII_BUFFER_SIZE];
    size_t length;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (header.type != kMESSAGE_TYPE_FRAME || size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }
    if (!FrameMessage_Decode(&frame, data + MESSAGE_HEADER_SIZE, header.length)) {
        return;
    }

    connection = ConnectionTable_FindByChannel(&self->connections, frame.channel);
    if (connection == NULL || connection->mode != kSOCKETCAND_MODE_RAW) {
        return;
    }

    length = SocketcandCodec_RenderFrame(ascii, sizeof(ascii), &frame);
    if (length > 0) {
        sendAscii(self, connection->connection_id, ascii, length);
    }
}

static bool dispatchCommand(SocketcandBridge *self, SocketcandConnection *connection, const SocketcandCommand *command)
{
    char ascii[ASCII_BUFFER_SIZE];
    size_t length;

    switch (command->kind) {
        case kSOCKETCAND_COMMAND_OPEN:
            return openBus(self, connection, command->bus);
        case kSOCKETCAND_COMMAND_RAWMODE:
            length = SocketcandCodec_RenderOk(ascii, sizeof(ascii));
            sendAscii(self, connection->connection_id, ascii, length);
            connection->mode = kSOCKETCAND_MODE_RAW;
            return true;
        case kSOCKETCAND_COMMAND_BCMMODE:
            length = SocketcandCodec_RenderOk(ascii, sizeof(ascii));
            sendAscii(self, connection->connection_id, ascii, length);
            connection->mode = kSOCKETCAND_MODE_BCM;
            return true;
        case kSOCKETCAND_COMMAND_SEND:
            handleSend(self, connection, &command->frame);
            return true;
        case kSOCKETCAND_COMMAND_ECHO:
            length = SocketcandCodec_RenderEcho(ascii, sizeof(ascii));
            sendAscii(self, connection->connection_id, ascii, length);
            return true;
        default:
            return true;
    }
}

static bool openBus(SocketcandBridge *self, SocketcandConnection *connection, const char *bus)
{
    uint32_t interface_id;

    if (connection->open_state != kSOCKETCAND_OPEN_NONE) {
        closeConnection(self, connection, "bus already open");
        return false;
    }
    if (self->hub_state != kSOCKETCAND_HUB_READY
        || !InterfaceCatalogue_FindByName(&self->catalogue, bus, &interface_id)) {
        closeConnection(self, connection, "could not open bus");
        return false;
    }

    connection->interface_id = interface_id;
    connection->open_state = kSOCKETCAND_OPEN_PENDING_WRITE;
    copyString(connection->bus, sizeof(connection->bus), bus);
    sendOpen(self, interface_id, OPEN_FLAGS_READ_WRITE);

    return true;
}

static void handleSend(SocketcandBridge *self, SocketcandConnection *connection, const FrameMessage *frame)
{
    FrameMessage outgoing = *frame;
    uint8_t encoded[WIRE_BUFFER_SIZE];
    char ascii[ASCII_BUFFER_SIZE];
    size_t encoded_size;
    size_t length;

    if (connection->reattaching) {
        return;
    }
    if (!connection->channel_valid) {
        length = SocketcandCodec_RenderError(ascii, sizeof(ascii), "no bus open");
        sendAscii(self, connection->connection_id, ascii, length);
        return;
    }
    if (!connection->can_write) {
        length = SocketcandCodec_RenderError(ascii, sizeof(ascii), "write not permitted");
        sendAscii(self, connection->connection_id, ascii, length);
        return;
    }

    outgoing.channel = connection->channel;
    outgoing.route_flags = 0;
    encoded_size = FrameMessage_Encode(&outgoing, encoded, sizeof(encoded));
    if (encoded_size > 0) {
        self->hub->send_frame(self->hub->context, encoded, encoded_size);
    }
}

static void sendHello(SocketcandBridge *self)
{
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_CLIENT, 0 };
    uint8_t encoded[WIRE_BUFFER_SIZE];
    size_t encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendListPage(SocketcandBridge *self, uint16_t offset)
{
    ListMessage list = { offset };
    uint8_t encoded[WIRE_BUFFER_SIZE];
    size_t encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendOpen(SocketcandBridge *self, uint32_t interface_id, uint8_t flags)
{
    OpenMessage open = { interface_id, flags };
    uint8_t encoded[WIRE_BUFFER_SIZE];
    size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendClose(SocketcandBridge *self, uint8_t channel)
{
    CloseMessage close = { channel };
    uint8_t encoded[WIRE_BUFFER_SIZE];
    size_t encoded_size = CloseMessage_Encode(&close, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void emitBeacon(SocketcandBridge *self)
{
    char buffer[BEACON_BUFFER_SIZE];
    size_t length = BeaconXml_Render(buffer, sizeof(buffer), self->device_name, self->beacon_url, &self->catalogue);

    if (length > 0) {
        self->server->send_beacon(self->server->context, (const uint8_t *)buffer, length);
    }
}

static void reattachRenewedInterfaces(SocketcandBridge *self)
{
    SocketcandConnection *connection;
    uint32_t new_interface_id;
    uint8_t i;

    for(i=0; i<SOCKETCAND_CONNECTIONS_MAX; i++) {
        connection = &self->connections.connections[i];
        if (!connection->in_use || connection->open_state != kSOCKETCAND_OPEN_DONE) {
            continue;
        }
        if (connection->bus[0] == '\0') {
            continue;
        }
        if (!InterfaceCatalogue_FindByName(&self->catalogue, connection->bus, &new_interface_id)) {
            continue;
        }
        if (new_interface_id == connection->interface_id) {
            continue;
        }

        connection->interface_id = new_interface_id;
        connection->open_state = kSOCKETCAND_OPEN_PENDING_WRITE;
        connection->reattaching = true;
        connection->channel_valid = false;
        sendOpen(self, new_interface_id, OPEN_FLAGS_READ_WRITE);
    }
}

static void closeAllConnections(SocketcandBridge *self)
{
    uint8_t i;

    for(i=0; i<SOCKETCAND_CONNECTIONS_MAX; i++) {
        SocketcandConnection *connection = &self->connections.connections[i];
        if (connection->in_use) {
            self->server->close_client(self->server->context, connection->connection_id);
        }
    }
    ConnectionTable_Reset(&self->connections);
}

static void closeConnection(SocketcandBridge *self, SocketcandConnection *connection, const char *detail)
{
    char ascii[ASCII_BUFFER_SIZE];
    size_t length;

    if (detail != NULL) {
        length = SocketcandCodec_RenderError(ascii, sizeof(ascii), detail);
        if (length > 0) {
            sendAscii(self, connection->connection_id, ascii, length);
        }
    }
    self->server->close_client(self->server->context, connection->connection_id);
    ConnectionTable_Remove(&self->connections, connection->connection_id);
}

static void sendAscii(SocketcandBridge *self, uint32_t connection_id, const char *text, size_t length)
{
    if (length == 0) {
        return;
    }
    self->server->write_client(self->server->context, connection_id, (const uint8_t *)text, length);
}

static void copyString(char *destination, size_t destination_size, const char *source)
{
    size_t length = strlen(source);

    if (length >= destination_size) {
        length = destination_size - 1;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}
