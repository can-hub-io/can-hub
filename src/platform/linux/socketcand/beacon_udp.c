#include "platform/linux/socketcand/beacon_udp.h"

#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>

/* ---------- public ---------- */

int32_t BeaconUdp_Open(void)
{
    int32_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    int32_t enable = 1;

    if (fd < 0) {
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

    return fd;
}

void BeaconUdp_Send(int32_t fd, uint16_t port, const uint8_t *data, size_t size)
{
    struct sockaddr_in address;

    if (fd < 0) {
        return;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    (void)sendto(fd, data, size, 0, (struct sockaddr *)&address, sizeof(address));
}

void BeaconUdp_Close(int32_t fd)
{
    if (fd >= 0) {
        close(fd);
    }
}
