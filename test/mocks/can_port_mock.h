#pragma once

#include "ports/can_port.h"

typedef struct {
    CanPort port;
    bool write_result;
    int write_count;
    uint8_t last_interface_index;
    FrameMessage last_frame;
} CanPortMock;

void CanPortMock_Reset(CanPortMock *self);
