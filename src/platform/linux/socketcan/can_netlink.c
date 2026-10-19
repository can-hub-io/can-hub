#include "platform/linux/socketcan/can_netlink.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/netlink.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define NETLINK_BUFFER_SIZE 512
#define NETLINK_RESPONSE_SIZE 8192
#define ATTRIBUTE_TYPE_MASK 0x3FFF
#define NETLINK_OPTION_ON 1
#define VCAN_KIND "vcan"
#define CAN_KIND "can"
#define UNKNOWN_INTERFACE_DETAIL "no such interface"
#define CAN_DATA_FIELDS (INTERFACE_FIELDS_NEED_LINK_DOWN | INTERFACE_FIELD_TERMINATION)

typedef struct {
    bool has_rate;
    bool has_sample_point;
    bool has_raw;
    uint32_t rate;
    uint16_t sample_point;
    InterfaceBitTiming raw;
} TimingRequest;

static_assert(INTERFACE_CTRLMODE_LOOPBACK == CAN_CTRLMODE_LOOPBACK, "ctrlmode bits follow the kernel");
static_assert(INTERFACE_CTRLMODE_LISTEN_ONLY == CAN_CTRLMODE_LISTENONLY, "ctrlmode bits follow the kernel");
static_assert(INTERFACE_CTRLMODE_TRIPLE_SAMPLING == CAN_CTRLMODE_3_SAMPLES, "ctrlmode bits follow the kernel");
static_assert(INTERFACE_CTRLMODE_ONE_SHOT == CAN_CTRLMODE_ONE_SHOT, "ctrlmode bits follow the kernel");
static_assert(INTERFACE_CTRLMODE_BERR_REPORTING == CAN_CTRLMODE_BERR_REPORTING, "ctrlmode bits follow the kernel");
static_assert(INTERFACE_CTRLMODE_FD == CAN_CTRLMODE_FD, "ctrlmode bits follow the kernel");

static bool setLink(const char *interface_name, bool up, char *detail);
static void restoreLink(const char *interface_name, bool up);
static bool applyLinkSettings(
    const char *interface_name,
    const InterfaceSettings *requested,
    const InterfaceSettings *current,
    char *detail
);
static void appendCanData(struct nlmsghdr *header, const InterfaceSettings *requested, const InterfaceSettings *current);
static TimingRequest nominalTiming(const InterfaceSettings *requested, const InterfaceSettings *current);
static TimingRequest dataTiming(const InterfaceSettings *requested, const InterfaceSettings *current);
static void appendBitTiming(struct nlmsghdr *header, uint16_t type, const TimingRequest *request);
static void readBitTiming(const struct can_bittiming *timing, InterfaceBitTiming *out);
static void beginRequest(uint8_t *buffer, uint16_t type, uint16_t flags, uint32_t index);
static bool sendAndAck(const void *request, uint32_t length, char *detail);
static int32_t openNetlinkSocket(void);
static void describeError(const struct nlmsghdr *header, char *detail);
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
static void parseLink(const struct nlmsghdr *header, InterfaceSettings *effective);
static void parseCanData(const struct rtattr *info_data, InterfaceSettings *effective);
static uint32_t readU32Attribute(const struct rtattr *attribute);

/* ---------- public ---------- */

bool CanNetlink_Apply(const char *interface_name, const InterfaceSettings *requested, char *detail)
{
    InterfaceSettings current;
    bool link_target;

    detail[0] = '\0';

    if (!CanNetlink_Query(interface_name, &current)) {
        snprintf(detail, IFCONFIG_DETAIL_SIZE, "%s", UNKNOWN_INTERFACE_DETAIL);
        return false;
    }

    link_target = current.link_up;
    if (requested->fields & INTERFACE_FIELD_LINK) {
        link_target = requested->link_up;
    }

    if ((requested->fields & INTERFACE_FIELDS_NEED_LINK_DOWN) && current.link_up) {
        if (!setLink(interface_name, false, detail)) {
            return false;
        }
    }
    if (!applyLinkSettings(interface_name, requested, &current, detail)) {
        restoreLink(interface_name, current.link_up);
        return false;
    }

    return setLink(interface_name, link_target, detail);
}

bool CanNetlink_Query(const char *interface_name, InterfaceSettings *effective)
{
    uint8_t request[NETLINK_BUFFER_SIZE];
    uint8_t response[NETLINK_RESPONSE_SIZE];
    struct nlmsghdr *header = (struct nlmsghdr *)request;
    struct sockaddr_nl address;
    ssize_t received;
    int32_t fd;
    uint32_t index = if_nametoindex(interface_name);

    memset(effective, 0, sizeof(*effective));

    if (index == 0) {
        return false;
    }

    beginRequest(request, RTM_GETLINK, NLM_F_REQUEST, index);

    fd = openNetlinkSocket();
    if (fd < 0) {
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.nl_family = AF_NETLINK;
    if (sendto(fd, request, header->nlmsg_len, 0, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        return false;
    }

    received = recv(fd, response, sizeof(response), 0);
    close(fd);
    if (received < (ssize_t)sizeof(struct nlmsghdr)) {
        return false;
    }

    header = (struct nlmsghdr *)response;
    if (!NLMSG_OK(header, (uint32_t)received) || header->nlmsg_type != RTM_NEWLINK) {
        return false;
    }

    parseLink(header, effective);

    return true;
}

uint32_t CanNetlink_GetBitrate(const char *interface_name)
{
    InterfaceSettings effective;

    if (!CanNetlink_Query(interface_name, &effective)) {
        return 0;
    }

    return effective.bitrate;
}

bool CanNetlink_InterfaceExists(const char *interface_name)
{
    return if_nametoindex(interface_name) != 0;
}

bool CanNetlink_CreateVcan(const char *interface_name)
{
    uint8_t buffer[NETLINK_BUFFER_SIZE];
    struct nlmsghdr *header = (struct nlmsghdr *)buffer;
    struct ifinfomsg *info;
    struct rtattr *link_info;
    char detail[IFCONFIG_DETAIL_SIZE];
    uint32_t mtu = CANFD_MTU;

    beginRequest(buffer, RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL, 0);
    info = NLMSG_DATA(header);
    info->ifi_change = IFF_UP;
    info->ifi_flags = IFF_UP;

    appendAttribute(header, sizeof(buffer), IFLA_IFNAME, interface_name, (uint16_t)(strlen(interface_name) + 1));
    appendAttribute(header, sizeof(buffer), IFLA_MTU, &mtu, sizeof(mtu));
    link_info = beginNested(header, sizeof(buffer), IFLA_LINKINFO);
    appendAttribute(header, sizeof(buffer), IFLA_INFO_KIND, VCAN_KIND, sizeof(VCAN_KIND));
    endNested(header, link_info);

    return sendAndAck(buffer, header->nlmsg_len, detail);
}

/* ---------- private ---------- */

static bool setLink(const char *interface_name, bool up, char *detail)
{
    uint8_t buffer[NETLINK_BUFFER_SIZE];
    struct nlmsghdr *header = (struct nlmsghdr *)buffer;
    struct ifinfomsg *info;
    uint32_t index = if_nametoindex(interface_name);

    if (index == 0) {
        return false;
    }

    beginRequest(buffer, RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK, index);
    info = NLMSG_DATA(header);
    info->ifi_change = IFF_UP;
    info->ifi_flags = up ? IFF_UP : 0;

    return sendAndAck(buffer, header->nlmsg_len, detail);
}

static void restoreLink(const char *interface_name, bool up)
{
    char ignored[IFCONFIG_DETAIL_SIZE];

    setLink(interface_name, up, ignored);
}

static bool applyLinkSettings(
    const char *interface_name,
    const InterfaceSettings *requested,
    const InterfaceSettings *current,
    char *detail
)
{
    uint8_t buffer[NETLINK_BUFFER_SIZE];
    struct nlmsghdr *header = (struct nlmsghdr *)buffer;
    uint32_t index = if_nametoindex(interface_name);

    if ((requested->fields & (CAN_DATA_FIELDS | INTERFACE_FIELD_TXQUEUELEN)) == 0) {
        return true;
    }
    if (index == 0) {
        return false;
    }

    beginRequest(buffer, RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK, index);
    if (requested->fields & INTERFACE_FIELD_TXQUEUELEN) {
        appendAttribute(header, sizeof(buffer), IFLA_TXQLEN, &requested->txqueuelen, sizeof(requested->txqueuelen));
    }
    if (requested->fields & CAN_DATA_FIELDS) {
        appendCanData(header, requested, current);
    }

    return sendAndAck(buffer, header->nlmsg_len, detail);
}

static void appendCanData(struct nlmsghdr *header, const InterfaceSettings *requested, const InterfaceSettings *current)
{
    TimingRequest nominal = nominalTiming(requested, current);
    TimingRequest data = dataTiming(requested, current);
    struct can_ctrlmode ctrlmode;
    struct rtattr *link_info;
    struct rtattr *info_data;

    link_info = beginNested(header, NETLINK_BUFFER_SIZE, IFLA_LINKINFO);
    appendAttribute(header, NETLINK_BUFFER_SIZE, IFLA_INFO_KIND, CAN_KIND, sizeof(CAN_KIND));
    info_data = beginNested(header, NETLINK_BUFFER_SIZE, IFLA_INFO_DATA);

    appendBitTiming(header, IFLA_CAN_BITTIMING, &nominal);
    appendBitTiming(header, IFLA_CAN_DATA_BITTIMING, &data);
    if (requested->fields & INTERFACE_FIELD_CTRLMODE) {
        ctrlmode.mask = requested->ctrlmode_mask & INTERFACE_CTRLMODES_KNOWN;
        ctrlmode.flags = requested->ctrlmode_flags & ctrlmode.mask;
        appendAttribute(header, NETLINK_BUFFER_SIZE, IFLA_CAN_CTRLMODE, &ctrlmode, sizeof(ctrlmode));
    }
    if (requested->fields & INTERFACE_FIELD_RESTART_MS) {
        appendAttribute(header, NETLINK_BUFFER_SIZE, IFLA_CAN_RESTART_MS, &requested->restart_ms, sizeof(requested->restart_ms));
    }
    if (requested->fields & INTERFACE_FIELD_TERMINATION) {
        appendAttribute(header, NETLINK_BUFFER_SIZE, IFLA_CAN_TERMINATION, &requested->termination, sizeof(requested->termination));
    }

    endNested(header, info_data);
    endNested(header, link_info);
}

static TimingRequest nominalTiming(const InterfaceSettings *requested, const InterfaceSettings *current)
{
    TimingRequest request;

    memset(&request, 0, sizeof(request));
    request.has_rate = (requested->fields & INTERFACE_FIELD_BITRATE) != 0;
    request.has_sample_point = (requested->fields & INTERFACE_FIELD_SAMPLE_POINT) != 0;
    request.has_raw = (requested->fields & INTERFACE_FIELD_BIT_TIMING) != 0;
    request.rate = request.has_rate ? requested->bitrate : current->bitrate;
    request.sample_point = requested->sample_point;
    request.raw = requested->bit_timing;

    return request;
}

static TimingRequest dataTiming(const InterfaceSettings *requested, const InterfaceSettings *current)
{
    TimingRequest request;

    memset(&request, 0, sizeof(request));
    request.has_rate = (requested->fields & INTERFACE_FIELD_DATA_BITRATE) != 0;
    request.has_sample_point = (requested->fields & INTERFACE_FIELD_DATA_SAMPLE_POINT) != 0;
    request.has_raw = (requested->fields & INTERFACE_FIELD_DATA_BIT_TIMING) != 0;
    request.rate = request.has_rate ? requested->data_bitrate : current->data_bitrate;
    request.sample_point = requested->data_sample_point;
    request.raw = requested->data_bit_timing;

    return request;
}

static void appendBitTiming(struct nlmsghdr *header, uint16_t type, const TimingRequest *request)
{
    struct can_bittiming bittiming;

    if (!request->has_rate && !request->has_sample_point && !request->has_raw) {
        return;
    }

    memset(&bittiming, 0, sizeof(bittiming));
    if (request->has_raw) {
        bittiming.tq = request->raw.tq;
        bittiming.prop_seg = request->raw.prop_seg;
        bittiming.phase_seg1 = request->raw.phase_seg1;
        bittiming.phase_seg2 = request->raw.phase_seg2;
        bittiming.sjw = request->raw.sjw;
        bittiming.brp = request->raw.brp;
    }
    if (bittiming.tq == 0 && bittiming.brp == 0) {
        bittiming.bitrate = request->rate;
        bittiming.sample_point = request->has_sample_point ? request->sample_point : 0;
    }

    appendAttribute(header, NETLINK_BUFFER_SIZE, type, &bittiming, sizeof(bittiming));
}

static void beginRequest(uint8_t *buffer, uint16_t type, uint16_t flags, uint32_t index)
{
    struct nlmsghdr *header = (struct nlmsghdr *)buffer;
    struct ifinfomsg *info;

    memset(buffer, 0, NETLINK_BUFFER_SIZE);
    header->nlmsg_len = NLMSG_LENGTH(sizeof(*info));
    header->nlmsg_type = type;
    header->nlmsg_flags = flags;
    info = NLMSG_DATA(header);
    info->ifi_family = AF_UNSPEC;
    info->ifi_index = (int32_t)index;
}

static bool sendAndAck(const void *request, uint32_t length, char *detail)
{
    struct sockaddr_nl address;
    uint8_t response[NETLINK_RESPONSE_SIZE];
    struct nlmsghdr *header;
    struct nlmsgerr *error;
    ssize_t received;
    int32_t fd;

    fd = openNetlinkSocket();
    if (fd < 0) {
        snprintf(detail, IFCONFIG_DETAIL_SIZE, "%s", strerror(errno));
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.nl_family = AF_NETLINK;

    if (sendto(fd, request, length, 0, (struct sockaddr *)&address, sizeof(address)) < 0) {
        snprintf(detail, IFCONFIG_DETAIL_SIZE, "%s", strerror(errno));
        close(fd);
        return false;
    }

    received = recv(fd, response, sizeof(response), 0);
    close(fd);
    if (received < (ssize_t)NLMSG_LENGTH(sizeof(*error))) {
        return false;
    }

    header = (struct nlmsghdr *)response;
    if (header->nlmsg_type != NLMSG_ERROR) {
        return false;
    }

    error = NLMSG_DATA(header);
    if (error->error != 0) {
        describeError(header, detail);
        return false;
    }

    return true;
}

static int32_t openNetlinkSocket(void)
{
    int32_t option = NETLINK_OPTION_ON;
    int32_t fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

    if (fd < 0) {
        return -1;
    }

    setsockopt(fd, SOL_NETLINK, NETLINK_EXT_ACK, &option, sizeof(option));
    setsockopt(fd, SOL_NETLINK, NETLINK_CAP_ACK, &option, sizeof(option));

    return fd;
}

static void describeError(const struct nlmsghdr *header, char *detail)
{
    const struct nlmsgerr *error = NLMSG_DATA(header);
    const struct rtattr *message = NULL;
    uint32_t attributes_offset = NLMSG_LENGTH(sizeof(*error));

    if ((header->nlmsg_flags & NLM_F_ACK_TLVS) && header->nlmsg_len > attributes_offset) {
        message = findAttribute(
            (const uint8_t *)header + attributes_offset,
            header->nlmsg_len - attributes_offset,
            NLMSGERR_ATTR_MSG
        );
    }

    if (message != NULL) {
        snprintf(detail, IFCONFIG_DETAIL_SIZE, "%s", (const char *)RTA_DATA(message));
        return;
    }

    snprintf(detail, IFCONFIG_DETAIL_SIZE, "%s", strerror(-error->error));
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

static void parseLink(const struct nlmsghdr *header, InterfaceSettings *effective)
{
    const struct ifinfomsg *info = NLMSG_DATA(header);
    const struct rtattr *txqueuelen;
    const struct rtattr *link_info;
    const struct rtattr *info_data;

    effective->fields = INTERFACE_FIELD_LINK;
    effective->link_up = (info->ifi_flags & IFF_UP) != 0;

    txqueuelen = findAttribute(IFLA_RTA(info), IFLA_PAYLOAD(header), IFLA_TXQLEN);
    if (txqueuelen != NULL) {
        effective->fields |= INTERFACE_FIELD_TXQUEUELEN;
        effective->txqueuelen = readU32Attribute(txqueuelen);
    }

    link_info = findAttribute(IFLA_RTA(info), IFLA_PAYLOAD(header), IFLA_LINKINFO);
    if (link_info == NULL) {
        return;
    }

    info_data = findAttribute(RTA_DATA(link_info), RTA_PAYLOAD(link_info), IFLA_INFO_DATA);
    if (info_data != NULL) {
        parseCanData(info_data, effective);
    }
}

static void parseCanData(const struct rtattr *info_data, InterfaceSettings *effective)
{
    const struct rtattr *bittiming = findAttribute(RTA_DATA(info_data), RTA_PAYLOAD(info_data), IFLA_CAN_BITTIMING);
    const struct rtattr *data_bittiming = findAttribute(RTA_DATA(info_data), RTA_PAYLOAD(info_data), IFLA_CAN_DATA_BITTIMING);
    const struct rtattr *ctrlmode = findAttribute(RTA_DATA(info_data), RTA_PAYLOAD(info_data), IFLA_CAN_CTRLMODE);
    const struct rtattr *restart_ms = findAttribute(RTA_DATA(info_data), RTA_PAYLOAD(info_data), IFLA_CAN_RESTART_MS);
    const struct rtattr *state = findAttribute(RTA_DATA(info_data), RTA_PAYLOAD(info_data), IFLA_CAN_STATE);
    const struct rtattr *termination = findAttribute(RTA_DATA(info_data), RTA_PAYLOAD(info_data), IFLA_CAN_TERMINATION);
    const struct can_bittiming *timing;
    const struct can_ctrlmode *modes;
    uint16_t ohms;

    if (bittiming != NULL && RTA_PAYLOAD(bittiming) >= sizeof(*timing)) {
        timing = RTA_DATA(bittiming);
        if (timing->bitrate != 0) {
            effective->fields |= INTERFACE_FIELDS_NOMINAL_RATE | INTERFACE_FIELD_BIT_TIMING;
            effective->bitrate = timing->bitrate;
            effective->sample_point = (uint16_t)timing->sample_point;
            readBitTiming(timing, &effective->bit_timing);
        }
    }
    if (data_bittiming != NULL && RTA_PAYLOAD(data_bittiming) >= sizeof(*timing)) {
        timing = RTA_DATA(data_bittiming);
        if (timing->bitrate != 0) {
            effective->fields |= INTERFACE_FIELDS_DATA_RATE | INTERFACE_FIELD_DATA_BIT_TIMING;
            effective->data_bitrate = timing->bitrate;
            effective->data_sample_point = (uint16_t)timing->sample_point;
            readBitTiming(timing, &effective->data_bit_timing);
        }
    }
    if (ctrlmode != NULL && RTA_PAYLOAD(ctrlmode) >= sizeof(*modes)) {
        modes = RTA_DATA(ctrlmode);
        effective->fields |= INTERFACE_FIELD_CTRLMODE;
        effective->ctrlmode_mask = INTERFACE_CTRLMODES_KNOWN;
        effective->ctrlmode_flags = modes->flags & INTERFACE_CTRLMODES_KNOWN;
    }
    if (restart_ms != NULL) {
        effective->fields |= INTERFACE_FIELD_RESTART_MS;
        effective->restart_ms = readU32Attribute(restart_ms);
    }
    if (state != NULL) {
        effective->fields |= INTERFACE_FIELD_STATE;
        effective->can_state = (uint8_t)readU32Attribute(state);
    }
    if (termination != NULL && RTA_PAYLOAD(termination) >= sizeof(ohms)) {
        memcpy(&ohms, RTA_DATA(termination), sizeof(ohms));
        effective->fields |= INTERFACE_FIELD_TERMINATION;
        effective->termination = ohms;
    }
}

static void readBitTiming(const struct can_bittiming *timing, InterfaceBitTiming *out)
{
    out->tq = timing->tq;
    out->prop_seg = timing->prop_seg;
    out->phase_seg1 = timing->phase_seg1;
    out->phase_seg2 = timing->phase_seg2;
    out->sjw = timing->sjw;
    out->brp = timing->brp;
}

static uint32_t readU32Attribute(const struct rtattr *attribute)
{
    uint32_t value = 0;

    if (RTA_PAYLOAD(attribute) >= sizeof(value)) {
        memcpy(&value, RTA_DATA(attribute), sizeof(value));
    }

    return value;
}
