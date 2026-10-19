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
    InterfaceSettings last_configure_settings;
    char configure_detail[IFCONFIG_DETAIL_SIZE];
    bool query_result;
    int query_count;
    InterfaceSettings query_settings;
    uint32_t bitrate_value;
} CanPortMock;

void CanPortMock_Reset(CanPortMock *self);
