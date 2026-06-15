#include "platform/linux/socketcan/can_netlink.h"

#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/socket.h>

#include <linux/can/netlink.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define NETLINK_BUFFER_SIZE 512
#define NETLINK_RESPONSE_SIZE 4096
#define ATTRIBUTE_TYPE_MASK 0x3FFF

static bool sendAndAck(const void *request, uint32_t length);
static struct rtattr *appendAttribute(
    struct nlmsghdr *header,
    uint32_t buffer_size,
    uint16_t type,
    const void *data,
    uint16_t data_length
);
static struct rtattr *beginNested(struct nlmsghdr *header, uint32_t buffer_size, uint16_t type);
static void endNested(struct nlmsghdr *header, struct rtattr *nest);
static const struct rtattr *findAttribute(const void *data, uint32_t length, uint16_t type);
static uint32_t parseBitrate(const void *attributes, uint32_t length);

/* ---------- public ---------- */

bool CanNetlink_SetLink(const char *interface_name, bool up)
{
    uint8_t buffer[NETLINK_BUFFER_SIZE];
    struct nlmsghdr *header = (struct nlmsghdr *)buffer;
    struct ifinfomsg *info;
    uint32_t index = if_nametoindex(interface_name);

    if (index == 0) {
        return false;
    }

    memset(buffer, 0, sizeof(buffer));
    header->nlmsg_len = NLMSG_LENGTH(sizeof(*info));
    header->nlmsg_type = RTM_NEWLINK;
    header->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    info = NLMSG_DATA(header);
    info->ifi_family = AF_UNSPEC;
    info->ifi_index = (int32_t)index;
    info->ifi_change = IFF_UP;
    info->ifi_flags = up ? IFF_UP : 0;

    return sendAndAck(buffer, header->nlmsg_len);
}

bool CanNetlink_SetBitrate(const char *interface_name, uint32_t bitrate)
{
    uint8_t buffer[NETLINK_BUFFER_SIZE];
    struct nlmsghdr *header = (struct nlmsghdr *)buffer;
    struct ifinfomsg *info;
    struct rtattr *link_info;
    struct rtattr *info_data;
    struct can_bittiming bittiming;
    uint32_t index = if_nametoindex(interface_name);

    if (index == 0) {
        return false;
    }

    memset(buffer, 0, sizeof(buffer));
    header->nlmsg_len = NLMSG_LENGTH(sizeof(*info));
    header->nlmsg_type = RTM_NEWLINK;
    header->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    info = NLMSG_DATA(header);
    info->ifi_family = AF_UNSPEC;
    info->ifi_index = (int32_t)index;

    memset(&bittiming, 0, sizeof(bittiming));
    bittiming.bitrate = bitrate;

    link_info = beginNested(header, sizeof(buffer), IFLA_LINKINFO);
    appendAttribute(header, sizeof(buffer), IFLA_INFO_KIND, "can", 4);
    info_data = beginNested(header, sizeof(buffer), IFLA_INFO_DATA);
    appendAttribute(header, sizeof(buffer), IFLA_CAN_BITTIMING, &bittiming, sizeof(bittiming));
    endNested(header, info_data);
    endNested(header, link_info);

    return sendAndAck(buffer, header->nlmsg_len);
}

uint32_t CanNetlink_GetBitrate(const char *interface_name)
{
    uint8_t request[NETLINK_BUFFER_SIZE];
    uint8_t response[NETLINK_RESPONSE_SIZE];
    struct nlmsghdr *header = (struct nlmsghdr *)request;
    struct ifinfomsg *info;
    struct sockaddr_nl address;
    ssize_t received;
    int32_t fd;
    uint32_t index = if_nametoindex(interface_name);

    if (index == 0) {
        return 0;
    }

    memset(request, 0, sizeof(request));
    header->nlmsg_len = NLMSG_LENGTH(sizeof(*info));
    header->nlmsg_type = RTM_GETLINK;
    header->nlmsg_flags = NLM_F_REQUEST;
    info = NLMSG_DATA(header);
    info->ifi_family = AF_UNSPEC;
    info->ifi_index = (int32_t)index;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        return 0;
    }

    memset(&address, 0, sizeof(address));
    address.nl_family = AF_NETLINK;
    if (sendto(fd, request, header->nlmsg_len, 0, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        return 0;
    }

    received = recv(fd, response, sizeof(response), 0);
    close(fd);
    if (received < (ssize_t)sizeof(struct nlmsghdr)) {
        return 0;
    }

    header = (struct nlmsghdr *)response;
    if (!NLMSG_OK(header, (uint32_t)received) || header->nlmsg_type != RTM_NEWLINK) {
        return 0;
    }

    info = NLMSG_DATA(header);

    return parseBitrate(IFLA_RTA(info), IFLA_PAYLOAD(header));
}

/* ---------- private ---------- */

static bool sendAndAck(const void *request, uint32_t length)
{
    struct sockaddr_nl address;
    uint8_t response[NETLINK_BUFFER_SIZE];
    struct nlmsghdr *header;
    struct nlmsgerr *error;
    ssize_t received;
    int32_t fd;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.nl_family = AF_NETLINK;

    if (sendto(fd, request, length, 0, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        return false;
    }

    received = recv(fd, response, sizeof(response), 0);
    close(fd);
    if (received < (ssize_t)sizeof(struct nlmsghdr)) {
        return false;
    }

    header = (struct nlmsghdr *)response;
    if (header->nlmsg_type != NLMSG_ERROR) {
        return false;
    }

    error = NLMSG_DATA(header);
    return error->error == 0;
}

static struct rtattr *appendAttribute(
    struct nlmsghdr *header,
    uint32_t buffer_size,
    uint16_t type,
    const void *data,
    uint16_t data_length
)
{
    uint32_t attribute_length = RTA_LENGTH(data_length);
    struct rtattr *attribute;

    if (NLMSG_ALIGN(header->nlmsg_len) + RTA_ALIGN(attribute_length) > buffer_size) {
        return NULL;
    }

    attribute = (struct rtattr *)((uint8_t *)header + NLMSG_ALIGN(header->nlmsg_len));
    attribute->rta_type = type;
    attribute->rta_len = (uint16_t)attribute_length;
    if (data_length > 0) {
        memcpy(RTA_DATA(attribute), data, data_length);
    }
    header->nlmsg_len = NLMSG_ALIGN(header->nlmsg_len) + RTA_ALIGN(attribute_length);

    return attribute;
}

static struct rtattr *beginNested(struct nlmsghdr *header, uint32_t buffer_size, uint16_t type)
{
    return appendAttribute(header, buffer_size, type, NULL, 0);
}

static void endNested(struct nlmsghdr *header, struct rtattr *nest)
{
    nest->rta_len = (uint16_t)((uint8_t *)header + NLMSG_ALIGN(header->nlmsg_len) - (uint8_t *)nest);
}

static const struct rtattr *findAttribute(const void *data, uint32_t length, uint16_t type)
{
    const struct rtattr *attribute = data;
    uint32_t remaining = length;

    while (RTA_OK(attribute, remaining)) {
        if ((attribute->rta_type & ATTRIBUTE_TYPE_MASK) == type) {
            return attribute;
        }
        attribute = RTA_NEXT(attribute, remaining);
    }

    return NULL;
}

static uint32_t parseBitrate(const void *attributes, uint32_t length)
{
    const struct rtattr *link_info;
    const struct rtattr *info_data;
    const struct rtattr *bittiming;

    link_info = findAttribute(attributes, length, IFLA_LINKINFO);
    if (link_info == NULL) {
        return 0;
    }

    info_data = findAttribute(RTA_DATA(link_info), RTA_PAYLOAD(link_info), IFLA_INFO_DATA);
    if (info_data == NULL) {
        return 0;
    }

    bittiming = findAttribute(RTA_DATA(info_data), RTA_PAYLOAD(info_data), IFLA_CAN_BITTIMING);
    if (bittiming == NULL || RTA_PAYLOAD(bittiming) < sizeof(struct can_bittiming)) {
        return 0;
    }

    return ((const struct can_bittiming *)RTA_DATA(bittiming))->bitrate;
}
