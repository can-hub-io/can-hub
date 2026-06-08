#pragma once

#include "socketcand/ports/socketcand_server_port.h"

#define SOCKETCAND_MOCK_CONNECTIONS_MAX 8
#define SOCKETCAND_MOCK_BUFFER_SIZE 4096

typedef struct {
    bool used;
    bool closed;
    uint32_t connection_id;
    char written[SOCKETCAND_MOCK_BUFFER_SIZE];
    size_t written_length;
} SocketcandServerPortMockConnection;

typedef struct {
    SocketcandServerPort port;
    SocketcandServerPortMockConnection connections[SOCKETCAND_MOCK_CONNECTIONS_MAX];
    bool write_result;
    int beacon_count;
    char last_beacon[SOCKETCAND_MOCK_BUFFER_SIZE];
    size_t last_beacon_length;
} SocketcandServerPortMock;

void SocketcandServerPortMock_Reset(SocketcandServerPortMock *self);
const char *SocketcandServerPortMock_Written(SocketcandServerPortMock *self, uint32_t connection_id);
bool SocketcandServerPortMock_Closed(SocketcandServerPortMock *self, uint32_t connection_id);
