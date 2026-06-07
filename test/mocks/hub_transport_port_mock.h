#pragma once

#include "hub/ports/hub_transport_port.h"

#define HUB_MOCK_CONTROL_LOG_MAX 8
#define HUB_MOCK_CONTROL_SIZE 4096
#define HUB_MOCK_FRAME_LOG_MAX 16
#define HUB_MOCK_FRAME_SIZE 128

typedef struct {
    HubTransportPort port;
    int control_count;
    uint32_t control_peers[HUB_MOCK_CONTROL_LOG_MAX];
    uint8_t control_log[HUB_MOCK_CONTROL_LOG_MAX][HUB_MOCK_CONTROL_SIZE];
    size_t control_sizes[HUB_MOCK_CONTROL_LOG_MAX];
    int frame_count;
    uint32_t frame_peers[HUB_MOCK_FRAME_LOG_MAX];
    uint8_t frame_log[HUB_MOCK_FRAME_LOG_MAX][HUB_MOCK_FRAME_SIZE];
    size_t frame_sizes[HUB_MOCK_FRAME_LOG_MAX];
    int close_count;
    uint32_t last_closed_peer;
    bool control_result;
    bool frame_result;
} HubTransportPortMock;

void HubTransportPortMock_Reset(HubTransportPortMock *self);
