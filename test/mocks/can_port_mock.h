#pragma once

#include "agent/ports/can_port.h"

typedef struct {
    CanPort port;
    bool write_result;
    int write_count;
    uint8_t last_interface_index;
    FrameMessage last_frame;
    bool configure_result;
    int configure_count;
    uint8_t last_configure_interface_index;
    uint8_t last_configure_op;
    uint32_t last_configure_bitrate;
    uint32_t bitrate_value;
} CanPortMock;

void CanPortMock_Reset(CanPortMock *self);
