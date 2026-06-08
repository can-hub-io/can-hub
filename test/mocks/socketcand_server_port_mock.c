#include "socketcand_server_port_mock.h"

#include <string.h>

static bool mockWriteClient(void *context, uint32_t connection_id, const uint8_t *data, size_t size);
static void mockCloseClient(void *context, uint32_t connection_id);
static void mockSendBeacon(void *context, const uint8_t *data, size_t size);
static SocketcandServerPortMockConnection *resolveConnection(SocketcandServerPortMock *self, uint32_t connection_id);

/* ---------- public ---------- */

void SocketcandServerPortMock_Reset(SocketcandServerPortMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.write_client = mockWriteClient;
    self->port.close_client = mockCloseClient;
    self->port.send_beacon = mockSendBeacon;
    self->write_result = true;
}

const char *SocketcandServerPortMock_Written(SocketcandServerPortMock *self, uint32_t connection_id)
{
    SocketcandServerPortMockConnection *connection = resolveConnection(self, connection_id);

    if (connection == NULL) {
        return "";
    }

    return connection->written;
}

bool SocketcandServerPortMock_Closed(SocketcandServerPortMock *self, uint32_t connection_id)
{
    SocketcandServerPortMockConnection *connection = resolveConnection(self, connection_id);

    return connection != NULL && connection->closed;
}

/* ---------- private ---------- */

static bool mockWriteClient(void *context, uint32_t connection_id, const uint8_t *data, size_t size)
{
    SocketcandServerPortMock *self = context;
    SocketcandServerPortMockConnection *connection = resolveConnection(self, connection_id);

    if (connection == NULL || !self->write_result) {
        return false;
    }
    if (connection->written_length + size >= SOCKETCAND_MOCK_BUFFER_SIZE) {
        return false;
    }
    memcpy(connection->written + connection->written_length, data, size);
    connection->written_length += size;
    connection->written[connection->written_length] = '\0';

    return true;
}

static void mockCloseClient(void *context, uint32_t connection_id)
{
    SocketcandServerPortMock *self = context;
    SocketcandServerPortMockConnection *connection = resolveConnection(self, connection_id);

    if (connection != NULL) {
        connection->closed = true;
    }
}

static void mockSendBeacon(void *context, const uint8_t *data, size_t size)
{
    SocketcandServerPortMock *self = context;

    self->beacon_count++;
    if (size >= SOCKETCAND_MOCK_BUFFER_SIZE) {
        return;
    }
    memcpy(self->last_beacon, data, size);
    self->last_beacon[size] = '\0';
    self->last_beacon_length = size;
}

static SocketcandServerPortMockConnection *resolveConnection(SocketcandServerPortMock *self, uint32_t connection_id)
{
    uint8_t i;

    for(i=0; i<SOCKETCAND_MOCK_CONNECTIONS_MAX; i++) {
        if (self->connections[i].used && self->connections[i].connection_id == connection_id) {
            return &self->connections[i];
        }
    }
    for(i=0; i<SOCKETCAND_MOCK_CONNECTIONS_MAX; i++) {
        if (!self->connections[i].used) {
            self->connections[i].used = true;
            self->connections[i].connection_id = connection_id;
            return &self->connections[i];
        }
    }

    return NULL;
}
