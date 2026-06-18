#pragma once

#include "agent/ports/transport_port.h"

#define MOCK_CONTROL_LOG_SIZE 1024
#define MOCK_CONTROL_LOG_MESSAGES_MAX 8
#define MOCK_FRAME_BUFFER_SIZE 256

typedef struct {
    TransportPort port;
    bool connect_result;
    int connect_calls;
    int disconnect_calls;
    int control_count;
    uint8_t control_log[MOCK_CONTROL_LOG_MESSAGES_MAX][MOCK_CONTROL_LOG_SIZE];
    size_t control_sizes[MOCK_CONTROL_LOG_MESSAGES_MAX];
    int frame_count;
    uint8_t last_frame[MOCK_FRAME_BUFFER_SIZE];
    size_t last_frame_size;
    uint8_t last_frame_channel;
    uint32_t channel_mode_count;
    uint8_t last_channel_mode_channel;
    bool last_channel_mode_reliable;
} TransportPortMock;

void TransportPortMock_Reset(TransportPortMock *self);
