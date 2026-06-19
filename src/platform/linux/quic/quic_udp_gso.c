#include "platform/linux/quic/quic_udp_gso.h"

#include <errno.h>
#include <string.h>

#include <netinet/in.h>
#include <netinet/udp.h>

#ifndef SOL_UDP
#define SOL_UDP 17
#endif
#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif

static void sendOnePerSegment(
    int32_t fd,
    const struct sockaddr *address,
    socklen_t address_length,
    const uint8_t *data,
    size_t size,
    size_t segment_size
);

/* ---------- public ---------- */

void QuicUdpGso_Send(
    int32_t fd,
    const struct sockaddr *address,
    socklen_t address_length,
    const uint8_t *data,
    size_t size,
    size_t segment_size,
    bool *gso_unsupported
)
{
    struct msghdr message;
    struct iovec io_vector;
    struct cmsghdr *control_message;
    uint8_t control_buffer[CMSG_SPACE(sizeof(uint16_t))];
    uint16_t segment = (uint16_t)segment_size;
    ssize_t sent;

    if (size <= segment_size || *gso_unsupported) {
        sendOnePerSegment(fd, address, address_length, data, size, segment_size);
        return;
    }

    io_vector.iov_base = (void *)data;
    io_vector.iov_len = size;
    memset(&message, 0, sizeof(message));
    memset(control_buffer, 0, sizeof(control_buffer));
    message.msg_name = (void *)address;
    message.msg_namelen = address != NULL ? address_length : 0;
    message.msg_iov = &io_vector;
    message.msg_iovlen = 1;
    message.msg_control = control_buffer;
    message.msg_controllen = sizeof(control_buffer);

    control_message = CMSG_FIRSTHDR(&message);
    control_message->cmsg_level = SOL_UDP;
    control_message->cmsg_type = UDP_SEGMENT;
    control_message->cmsg_len = CMSG_LEN(sizeof(uint16_t));
    memcpy(CMSG_DATA(control_message), &segment, sizeof(segment));

    sent = sendmsg(fd, &message, 0);
    if (sent < 0 && (errno == EINVAL || errno == ENOPROTOOPT || errno == EIO)) {
        *gso_unsupported = true;
        sendOnePerSegment(fd, address, address_length, data, size, segment_size);
    }
}

/* ---------- private ---------- */

static void sendOnePerSegment(
    int32_t fd,
    const struct sockaddr *address,
    socklen_t address_length,
    const uint8_t *data,
    size_t size,
    size_t segment_size
)
{
    size_t offset;
    size_t chunk;

    for(offset=0; offset<size; offset+=segment_size) {
        chunk = size - offset;
        if (chunk > segment_size) {
            chunk = segment_size;
        }
        sendto(fd, data + offset, chunk, 0, address, address != NULL ? address_length : 0);
    }
}
