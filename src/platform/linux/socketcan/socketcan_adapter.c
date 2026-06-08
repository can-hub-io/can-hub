#include "platform/linux/socketcan/socketcan_adapter.h"

#include "platform/linux/clock/clock.h"
#include "platform/linux/socketcan/can_netlink.h"

#include "protocol/ifconfig_message.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#define RECEIVE_BUFFER_BYTES (1024 * 1024)

static bool portWriteFrame(void *context, uint8_t interface_index, const FrameMessage *frame);
static bool portConfigure(void *context, uint8_t interface_index, uint8_t op, uint32_t bitrate);
static bool writeClassicFrame(int32_t can_fd, const FrameMessage *frame);
static bool writeFdFrame(int32_t can_fd, const FrameMessage *frame);
static int32_t openCanSocket(const char *interface_name);
static void classicToMessage(const struct can_frame *classic, FrameMessage *frame);
static void fdToMessage(const struct canfd_frame *fd_frame, FrameMessage *frame);

/* ---------- public ---------- */

bool SocketCanAdapter_Open(SocketCanAdapter *self, const RegisterMessage *registration)
{
    uint8_t i;

    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.write_frame = portWriteFrame;
    self->port.configure = portConfigure;
    self->interface_count = registration->interface_count;

    for(i=0; i<self->interface_count; i++) {
        snprintf(self->interface_names[i], REGISTER_INTERFACE_NAME_SIZE, "%s", registration->interface_names[i]);
        self->can_fds[i] = openCanSocket(registration->interface_names[i]);
        if (self->can_fds[i] < 0) {
            SocketCanAdapter_Close(self);
            return false;
        }
    }

    return true;
}

void SocketCanAdapter_Close(SocketCanAdapter *self)
{
    uint8_t i;

    for(i=0; i<self->interface_count; i++) {
        if (self->can_fds[i] >= 0) {
            close(self->can_fds[i]);
            self->can_fds[i] = -1;
        }
    }
}

CanPort *SocketCanAdapter_Port(SocketCanAdapter *self)
{
    return &self->port;
}

int32_t SocketCanAdapter_Fd(const SocketCanAdapter *self, uint8_t interface_index)
{
    return self->can_fds[interface_index];
}

bool SocketCanAdapter_ReadFrame(SocketCanAdapter *self, uint8_t interface_index, FrameMessage *frame)
{
    struct canfd_frame fd_frame;
    struct iovec frame_vector = { &fd_frame, sizeof(fd_frame) };
    struct msghdr message;
    ssize_t bytes_received;

    memset(&message, 0, sizeof(message));
    message.msg_iov = &frame_vector;
    message.msg_iovlen = 1;

    bytes_received = recvmsg(self->can_fds[interface_index], &message, 0);
    if (bytes_received == (ssize_t)CAN_MTU) {
        classicToMessage((const struct can_frame *)&fd_frame, frame);
    } else if (bytes_received == (ssize_t)CANFD_MTU) {
        fdToMessage(&fd_frame, frame);
    } else {
        return false;
    }

    frame->route_flags = (message.msg_flags & MSG_CONFIRM) ? FRAME_ROUTE_FLAG_ECHO : 0;
    frame->timestamp_us = Clock_RealtimeUs();

    return true;
}

/* ---------- private ---------- */

static bool portWriteFrame(void *context, uint8_t interface_index, const FrameMessage *frame)
{
    SocketCanAdapter *self = context;

    if (interface_index >= self->interface_count) {
        return false;
    }

    if (frame->frame_flags & FRAME_FLAG_FD) {
        return writeFdFrame(self->can_fds[interface_index], frame);
    }

    return writeClassicFrame(self->can_fds[interface_index], frame);
}

static bool portConfigure(void *context, uint8_t interface_index, uint8_t op, uint32_t bitrate)
{
    SocketCanAdapter *self = context;
    const char *interface_name;

    if (interface_index >= self->interface_count) {
        return false;
    }

    interface_name = self->interface_names[interface_index];

    if (op == IFCONFIG_OP_UP) {
        return CanNetlink_SetLink(interface_name, true);
    }
    if (op == IFCONFIG_OP_DOWN) {
        return CanNetlink_SetLink(interface_name, false);
    }
    if (op != IFCONFIG_OP_SET_BITRATE) {
        return false;
    }

    if (!CanNetlink_SetLink(interface_name, false)) {
        return false;
    }
    if (!CanNetlink_SetBitrate(interface_name, bitrate)) {
        return false;
    }

    return CanNetlink_SetLink(interface_name, true);
}

static bool writeClassicFrame(int32_t can_fd, const FrameMessage *frame)
{
    struct can_frame classic;

    memset(&classic, 0, sizeof(classic));
    classic.can_id = frame->can_id;
    classic.can_dlc = frame->payload_length;
    memcpy(classic.data, frame->payload, frame->payload_length);

    return write(can_fd, &classic, CAN_MTU) == (ssize_t)CAN_MTU;
}

static bool writeFdFrame(int32_t can_fd, const FrameMessage *frame)
{
    struct canfd_frame fd_frame;

    memset(&fd_frame, 0, sizeof(fd_frame));
    fd_frame.can_id = frame->can_id;
    fd_frame.len = frame->payload_length;
    if (frame->frame_flags & FRAME_FLAG_BRS) {
        fd_frame.flags |= CANFD_BRS;
    }
    memcpy(fd_frame.data, frame->payload, frame->payload_length);

    return write(can_fd, &fd_frame, CANFD_MTU) == (ssize_t)CANFD_MTU;
}

static int32_t openCanSocket(const char *interface_name)
{
    struct sockaddr_can address;
    struct ifreq interface_request;
    int32_t receive_buffer_bytes = RECEIVE_BUFFER_BYTES;
    int32_t fd_frames_enabled = 1;
    int32_t receive_own_messages = 1;
    int32_t can_fd;

    can_fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    if (can_fd < 0) {
        return -1;
    }

    snprintf(interface_request.ifr_name, IFNAMSIZ, "%s", interface_name);
    if (ioctl(can_fd, SIOCGIFINDEX, &interface_request) < 0) {
        close(can_fd);
        return -1;
    }

    setsockopt(can_fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_bytes, sizeof(receive_buffer_bytes));
    setsockopt(can_fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &fd_frames_enabled, sizeof(fd_frames_enabled));
    setsockopt(can_fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &receive_own_messages, sizeof(receive_own_messages));

    memset(&address, 0, sizeof(address));
    address.can_family = AF_CAN;
    address.can_ifindex = interface_request.ifr_ifindex;
    if (bind(can_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(can_fd);
        return -1;
    }

    return can_fd;
}

static void classicToMessage(const struct can_frame *classic, FrameMessage *frame)
{
    frame->can_id = classic->can_id;
    frame->payload_length = classic->can_dlc;
    frame->frame_flags = 0;
    frame->channel = 0;
    memcpy(frame->payload, classic->data, classic->can_dlc);
}

static void fdToMessage(const struct canfd_frame *fd_frame, FrameMessage *frame)
{
    frame->can_id = fd_frame->can_id;
    frame->payload_length = fd_frame->len;
    frame->frame_flags = FRAME_FLAG_FD;
    if (fd_frame->flags & CANFD_BRS) {
        frame->frame_flags |= FRAME_FLAG_BRS;
    }
    frame->channel = 0;
    memcpy(frame->payload, fd_frame->data, fd_frame->len);
}
