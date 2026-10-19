#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>

#include "platform/linux/shared/cli_meta.h"
#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/shared/filter_spec.h"
#include "platform/linux/shared/hub_defaults.h"
#include "platform/linux/shared/interface_settings_text.h"
#include "platform/linux/shared/log.h"
#include "platform/linux/tcp/tcp_client_transport.h"
#include "hub/ports/hub_transport_events.h"
#include "protocol/admin_message.h"
#include "protocol/control_buffer.h"
#include "protocol/error_message.h"
#include "protocol/hello_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/interface_name.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"

#define MAX_EPOLL_EVENTS 8
#define POLL_PERIOD_MS 100
#define TCP_SLOT 0
#define COMMAND_WORDS_MAX 32
#define INTERFACE_COMMAND_TARGET_WORDS 3
#define BRIDGE_COMMAND_TARGET_WORDS 4
#define GROUP_COMMAND_NAME_WORDS 3
#define GROUP_COMMAND_MEMBER_WORDS 4
#define FILTER_TEXT_SIZE 24

typedef enum tcli_command_e {
    kCLI_COMMAND_STATUS = 0,
    kCLI_COMMAND_PEERS,
    kCLI_COMMAND_PEERS_KICK,
    kCLI_COMMAND_AGENTS,
    kCLI_COMMAND_AGENTS_SHOW,
    kCLI_COMMAND_AGENTS_KICK,
    kCLI_COMMAND_CLIENTS,
    kCLI_COMMAND_INTERFACES,
    kCLI_COMMAND_PINS,
    kCLI_COMMAND_PINS_FORGET,
    kCLI_COMMAND_PINS_ADD,
    kCLI_COMMAND_ACL,
    kCLI_COMMAND_ACL_ADD,
    kCLI_COMMAND_ACL_REVOKE,
    kCLI_COMMAND_PENDING,
    kCLI_COMMAND_INTERFACE_SET,
    kCLI_COMMAND_INTERFACE_SHOW,
    kCLI_COMMAND_BRIDGES,
    kCLI_COMMAND_BRIDGE_ADD,
    kCLI_COMMAND_BRIDGE_DELETE,
    kCLI_COMMAND_GROUPS,
    kCLI_COMMAND_GROUP_ADD,
    kCLI_COMMAND_GROUP_DELETE,
    kCLI_COMMAND_GROUP_JOIN,
    kCLI_COMMAND_GROUP_LEAVE,
    kCLI_COMMAND_MAX,
} TCLI_COMMAND;

typedef enum tcli_show_stage_e {
    kCLI_SHOW_STAGE_AGENT = 0,
    kCLI_SHOW_STAGE_INTERFACES,
    kCLI_SHOW_STAGE_CLIENTS,
    kCLI_SHOW_STAGE_MAX,
} TCLI_SHOW_STAGE;

static TcpClientTransport transport;
static uint8_t command;
static uint8_t connect_scheme;
static uint8_t show_stage;
static char target_agent[REGISTER_AGENT_NAME_SIZE];
static char target_interface[REGISTER_INTERFACE_NAME_SIZE];
static char target_fingerprint[ADMIN_FINGERPRINT_HEX_SIZE];
static bool target_can_read;
static bool target_can_write;
static bool target_can_admin;
static InterfaceSettings target_settings;
static AdminBridgeSetMessage target_bridge;
static AdminGroupSetMessage target_group;
static AdminBridgeEndpoint target_member;
static uint32_t target_peer_id;
static uint16_t page_offset;
static bool page_header_printed;
static bool complete_mode;
static int32_t exit_code = -1;

static void printUsage(FILE *stream, const char *program);
static bool parseArguments(int argc, char **argv, char *host, char *port_text);
static bool mapCommand(const char *words[], uint8_t word_count);
static bool mapCompleteKind(const char *kind);
static bool parsePeerId(const char *text);
static bool mapInterfaceCommand(const char *words[], uint8_t word_count);
static bool mapBridgeCommand(const char *words[], uint8_t word_count);
static bool parseBridgeEndpoint(const char *text, AdminBridgeEndpoint *endpoint);
static bool mapGroupCommand(const char *words[], uint8_t word_count);
static bool parseFilters(const char *words[], uint8_t word_count, CanFilter *filters, uint8_t *filter_count);
static bool parseAclLevel(const char *text);
static bool parseInterfaceRef(const char *text);
static bool parseNamespacedInterface(const char *text);
static bool initTransport(const char *host, const char *port_text, const TransportEvents *events);
static void onConnected(void *context);
static void onDisconnected(void *context, uint64_t now_us);
static void onControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void onFrame(void *context, const uint8_t *data, size_t size);
static void sendRequest(void);
static void sendShowStage(uint8_t stage);
static void handleStatusReply(const uint8_t *payload, uint16_t payload_length);
static void handlePeersReply(const uint8_t *payload, uint16_t payload_length);
static void handleAgentsReply(const uint8_t *payload, uint16_t payload_length);
static void handleClientsReply(const uint8_t *payload, uint16_t payload_length);
static void handleInterfacesReply(const uint8_t *payload, uint16_t payload_length);
static void handleListReply(const uint8_t *payload, uint16_t payload_length);
static void handlePinsReply(const uint8_t *payload, uint16_t payload_length);
static void handleAclListReply(const uint8_t *payload, uint16_t payload_length);
static void handleRejectedReply(const uint8_t *payload, uint16_t payload_length);
static void handleBridgeListReply(const uint8_t *payload, uint16_t payload_length);
static void handleGroupListReply(const uint8_t *payload, uint16_t payload_length);
static void printGroupReply(uint8_t status);
static const char *groupStatusText(uint8_t status);
static void printBridgeReply(uint8_t status, const char *action);
static void formatBridgeFilters(const CanFilter *filters, uint8_t filter_count, char *out, size_t out_size);
static void formatEndpoint(const AdminBridgeEndpoint *endpoint, char *out, size_t out_size);
static const char *rejectionReasonName(uint8_t reason);
static void printClientRow(const AdminClientsReplyEntry *entry, const char *indent);
static void printActionReply(uint8_t status, const char *action, const char *target_kind, const char *target);
static void printConfigureReply(const ConfigureReplyMessage *reply);
static const char *aclLevelName(bool can_read, bool can_write, bool can_admin);
static const char *roleName(uint8_t role);
static const char *transportName(uint8_t transport_kind);
static void formatUptime(uint32_t seconds, char *out, size_t size);
static const char *textOrDash(const char *text);
static bool requestNextPage(uint8_t flags, uint8_t count);
static int32_t runEventLoop(void);

int main(int argc, char **argv)
{
    char host[CONNECT_URL_HOST_MAX];
    char port_text[CONNECT_URL_PORT_TEXT_MAX];
    TransportEvents events = {
        .context = &transport,
        .on_connected = onConnected,
        .on_disconnected = onDisconnected,
        .on_control = onControl,
        .on_frame = onFrame,
    };

    signal(SIGPIPE, SIG_IGN);

    if (CliMeta_HandleVersionAndHelp(argc, argv, "can-hub-cli", printUsage)) {
        return 0;
    }

    Log_InitFromArgs("can-hub-cli", argc, argv);

    if (!parseArguments(argc, argv, host, port_text)) {
        printUsage(stderr, argv[0]);
        return 1;
    }

    if (!initTransport(host, port_text, &events)) {
        LOG_ERROR("could not initialize transport");
        return 1;
    }
    if (!transport.port.connect(transport.port.context)) {
        LOG_ERROR("could not connect to %s", host);
        return 1;
    }

    return runEventLoop();
}

/* ---------- private: arguments ---------- */

static void printUsage(FILE *stream, const char *program)
{
    fprintf(stream, "usage: %s [--connect unix://<path>] [--log-level error|warn|info|debug] <command>\n", program);
    fprintf(stream, "commands:\n");
    fprintf(stream, "  status                 hub counters\n");
    fprintf(stream, "  peers                  every live connection\n");
    fprintf(stream, "  peers kick <peer-id>   disconnect any peer (id as printed, 0x... or decimal)\n");
    fprintf(stream, "  agents                 live agents\n");
    fprintf(stream, "  agents show <name>     agent detail: interfaces and clients\n");
    fprintf(stream, "  agents kick <name>     disconnect an agent\n");
    fprintf(stream, "  clients                open client channels\n");
    fprintf(stream, "  interfaces             interface catalogue\n");
    fprintf(stream, "  interface show <agent>/<iface>                the settings the bus holds now\n");
    fprintf(stream, "  interface set <agent>/<iface> <setting>...    reconfigure a bus in one step:\n");
    fprintf(stream, "      bitrate <bps> sample-point <0.xxx> dbitrate <bps> dsample-point <0.xxx>\n");
    fprintf(stream, "      fd|listen-only|loopback|one-shot|triple-sampling|berr-reporting on|off\n");
    fprintf(stream, "      restart-ms <ms> txqueuelen <n> termination <ohms> up|down\n");
    fprintf(stream, "      tq <ns> prop-seg <n> phase-seg1 <n> phase-seg2 <n> sjw <n>   (dtq ... dsjw: data phase)\n");
    fprintf(stream, "  interface up|down <agent>/<iface>             bring a bus up or down\n");
    fprintf(stream, "  pins                   pinned TOFU identities\n");
    fprintf(stream, "  pins add <name> <fp>   allow an agent (its --show-identity fingerprint)\n");
    fprintf(stream, "  pins delete <name>     drop a pin so the agent can re-pin\n");
    fprintf(stream, "  acl                    client read/write grants\n");
    fprintf(stream, "  acl add <fp|*> <agent|*>/<iface|*> none|r|rw|rwa  grant (* = baseline, a = configure)\n");
    fprintf(stream, "  acl delete <fp|*> <agent|*>/<iface|*>          drop a grant\n");
    fprintf(stream, "  pending                identities the hub turned away, to authorize\n");
    fprintf(stream, "  bridge                 bus-to-bus routes with their counters\n");
    fprintf(stream, "  bridge add <agent>/<iface> <agent>/<iface> [<id>[:<mask>] ...]  forward frames from the first bus\n");
    fprintf(stream, "                         into the second, all or only the ids given (adding again replaces the ids)\n");
    fprintf(stream, "  bridge delete <agent>/<iface> <agent>/<iface>  drop a route\n");
    fprintf(stream, "  group                  bridge groups: buses that behave as one, with their members\n");
    fprintf(stream, "  group add <name> [<id>[:<mask>] ...]          create a group, or replace its ids\n");
    fprintf(stream, "  group delete <name>    drop a group\n");
    fprintf(stream, "  group join|leave <name> <agent>/<iface>       add or remove a member bus\n");
    fprintf(stream, "default: --connect unix://" HUB_DEFAULT_UNIX_SOCKET_PATH "\n");
}

static bool parseArguments(int argc, char **argv, char *host, char *port_text)
{
    const char *connect_url = NULL;
    const char *words[COMMAND_WORDS_MAX];
    uint8_t word_count = 0;
    int32_t i;

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            connect_url = argv[++i];
        } else if (strcmp(argv[i], "--complete") == 0 && i + 1 < argc) {
            if (!mapCompleteKind(argv[++i])) {
                return false;
            }
            complete_mode = true;
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            i++;
        } else if (word_count < COMMAND_WORDS_MAX) {
            words[word_count++] = argv[i];
        } else {
            return false;
        }
    }

    if (!complete_mode && !mapCommand(words, word_count)) {
        return false;
    }

    if (connect_url == NULL) {
        connect_scheme = kCONNECT_SCHEME_UNIX;
        snprintf(host, CONNECT_URL_HOST_MAX, "%s", HUB_DEFAULT_UNIX_SOCKET_PATH);
        port_text[0] = '\0';
        return true;
    }

    if (!ConnectUrl_Parse(connect_url, &connect_scheme, host, port_text)) {
        return false;
    }

    return connect_scheme == kCONNECT_SCHEME_UNIX;
}

static bool mapCommand(const char *words[], uint8_t word_count)
{
    if (word_count >= INTERFACE_COMMAND_TARGET_WORDS && strcmp(words[0], "interface") == 0) {
        return mapInterfaceCommand(words, word_count);
    }
    if (word_count >= 1 && strcmp(words[0], "bridge") == 0) {
        return mapBridgeCommand(words, word_count);
    }
    if (word_count >= 1 && strcmp(words[0], "group") == 0) {
        return mapGroupCommand(words, word_count);
    }

    if (word_count == 1) {
        if (strcmp(words[0], "status") == 0) {
            command = kCLI_COMMAND_STATUS;
        } else if (strcmp(words[0], "peers") == 0) {
            command = kCLI_COMMAND_PEERS;
        } else if (strcmp(words[0], "agents") == 0) {
            command = kCLI_COMMAND_AGENTS;
        } else if (strcmp(words[0], "clients") == 0) {
            command = kCLI_COMMAND_CLIENTS;
        } else if (strcmp(words[0], "interfaces") == 0) {
            command = kCLI_COMMAND_INTERFACES;
        } else if (strcmp(words[0], "pins") == 0) {
            command = kCLI_COMMAND_PINS;
        } else if (strcmp(words[0], "acl") == 0) {
            command = kCLI_COMMAND_ACL;
        } else if (strcmp(words[0], "pending") == 0) {
            command = kCLI_COMMAND_PENDING;
        } else {
            return false;
        }
        return true;
    }

    if (word_count == 5) {
        if (strcmp(words[0], "acl") != 0 || strcmp(words[1], "add") != 0) {
            return false;
        }
        command = kCLI_COMMAND_ACL_ADD;
        snprintf(target_fingerprint, sizeof(target_fingerprint), "%s", words[2]);
        return parseAclLevel(words[4]) && parseInterfaceRef(words[3]);
    }

    if (word_count == 4) {
        if (strcmp(words[0], "pins") == 0 && strcmp(words[1], "add") == 0) {
            command = kCLI_COMMAND_PINS_ADD;
            snprintf(target_agent, sizeof(target_agent), "%s", words[2]);
            snprintf(target_fingerprint, sizeof(target_fingerprint), "%s", words[3]);
            return target_agent[0] != '\0' && target_fingerprint[0] != '\0';
        }
        if (strcmp(words[0], "acl") == 0 && strcmp(words[1], "delete") == 0) {
            command = kCLI_COMMAND_ACL_REVOKE;
            snprintf(target_fingerprint, sizeof(target_fingerprint), "%s", words[2]);
            return parseInterfaceRef(words[3]);
        }
        return false;
    }

    if (word_count != 3) {
        return false;
    }

    if (strcmp(words[0], "peers") == 0 && strcmp(words[1], "kick") == 0) {
        command = kCLI_COMMAND_PEERS_KICK;
        return parsePeerId(words[2]);
    }
    if (strcmp(words[0], "agents") == 0 && strcmp(words[1], "show") == 0) {
        command = kCLI_COMMAND_AGENTS_SHOW;
    } else if (strcmp(words[0], "agents") == 0 && strcmp(words[1], "kick") == 0) {
        command = kCLI_COMMAND_AGENTS_KICK;
    } else if (strcmp(words[0], "pins") == 0 && strcmp(words[1], "delete") == 0) {
        command = kCLI_COMMAND_PINS_FORGET;
    } else {
        return false;
    }

    snprintf(target_agent, sizeof(target_agent), "%s", words[2]);

    return target_agent[0] != '\0';
}

static bool mapCompleteKind(const char *kind)
{
    if (strcmp(kind, "interfaces") == 0) {
        command = kCLI_COMMAND_INTERFACES;
        return true;
    }
    if (strcmp(kind, "agents") == 0) {
        command = kCLI_COMMAND_AGENTS;
        return true;
    }
    if (strcmp(kind, "peers") == 0) {
        command = kCLI_COMMAND_PEERS;
        return true;
    }

    return false;
}

static bool parsePeerId(const char *text)
{
    char *end = NULL;

    target_peer_id = (uint32_t)strtoul(text, &end, 0);

    return end != NULL && end != text && *end == '\0';
}

static bool mapInterfaceCommand(const char *words[], uint8_t word_count)
{
    char error[INTERFACE_SETTINGS_TEXT_ERROR_SIZE];

    command = kCLI_COMMAND_INTERFACE_SET;
    memset(&target_settings, 0, sizeof(target_settings));

    if (strcmp(words[1], "show") == 0 && word_count == INTERFACE_COMMAND_TARGET_WORDS) {
        command = kCLI_COMMAND_INTERFACE_SHOW;
    } else if (strcmp(words[1], "up") == 0 && word_count == INTERFACE_COMMAND_TARGET_WORDS) {
        target_settings.fields = INTERFACE_FIELD_LINK;
        target_settings.link_up = true;
    } else if (strcmp(words[1], "down") == 0 && word_count == INTERFACE_COMMAND_TARGET_WORDS) {
        target_settings.fields = INTERFACE_FIELD_LINK;
    } else if (strcmp(words[1], "set") != 0) {
        return false;
    } else if (!InterfaceSettingsText_Parse(
                   &target_settings,
                   words + INTERFACE_COMMAND_TARGET_WORDS,
                   (uint8_t)(word_count - INTERFACE_COMMAND_TARGET_WORDS),
                   error
               )) {
        LOG_ERROR("%s", error);
        return false;
    }

    return parseNamespacedInterface(words[2]);
}

static bool mapBridgeCommand(const char *words[], uint8_t word_count)
{
    memset(&target_bridge, 0, sizeof(target_bridge));
    if (word_count == 1) {
        command = kCLI_COMMAND_BRIDGES;
        return true;
    }
    if (word_count < BRIDGE_COMMAND_TARGET_WORDS) {
        return false;
    }
    if (!parseBridgeEndpoint(words[2], &target_bridge.source) || !parseBridgeEndpoint(words[3], &target_bridge.destination)) {
        return false;
    }

    if (strcmp(words[1], "delete") == 0) {
        command = kCLI_COMMAND_BRIDGE_DELETE;
        return word_count == BRIDGE_COMMAND_TARGET_WORDS;
    }
    if (strcmp(words[1], "add") != 0 || word_count - BRIDGE_COMMAND_TARGET_WORDS > SUBSCRIBE_FILTERS_MAX) {
        return false;
    }

    command = kCLI_COMMAND_BRIDGE_ADD;

    return parseFilters(
        words + BRIDGE_COMMAND_TARGET_WORDS,
        (uint8_t)(word_count - BRIDGE_COMMAND_TARGET_WORDS),
        target_bridge.filters,
        &target_bridge.filter_count
    );
}

static bool mapGroupCommand(const char *words[], uint8_t word_count)
{
    memset(&target_group, 0, sizeof(target_group));
    memset(&target_member, 0, sizeof(target_member));
    if (word_count == 1) {
        command = kCLI_COMMAND_GROUPS;
        return true;
    }
    if (word_count < GROUP_COMMAND_NAME_WORDS) {
        return false;
    }
    snprintf(target_group.name, sizeof(target_group.name), "%s", words[2]);

    if (strcmp(words[1], "add") == 0 && word_count - GROUP_COMMAND_NAME_WORDS <= SUBSCRIBE_FILTERS_MAX) {
        command = kCLI_COMMAND_GROUP_ADD;
        return parseFilters(
            words + GROUP_COMMAND_NAME_WORDS,
            (uint8_t)(word_count - GROUP_COMMAND_NAME_WORDS),
            target_group.filters,
            &target_group.filter_count
        );
    }
    if (strcmp(words[1], "delete") == 0) {
        command = kCLI_COMMAND_GROUP_DELETE;
        return word_count == GROUP_COMMAND_NAME_WORDS;
    }
    if (word_count != GROUP_COMMAND_MEMBER_WORDS || !parseBridgeEndpoint(words[3], &target_member)) {
        return false;
    }
    if (strcmp(words[1], "join") == 0) {
        command = kCLI_COMMAND_GROUP_JOIN;
        return true;
    }
    if (strcmp(words[1], "leave") == 0) {
        command = kCLI_COMMAND_GROUP_LEAVE;
        return true;
    }

    return false;
}

static bool parseFilters(const char *words[], uint8_t word_count, CanFilter *filters, uint8_t *filter_count)
{
    uint8_t i;

    *filter_count = 0;
    for(i=0; i<word_count; i++) {
        if (!FilterSpec_Parse(words[i], &filters[*filter_count])) {
            LOG_ERROR("bad filter %s, expected <id>[:<mask>] in hex", words[i]);
            return false;
        }
        (*filter_count)++;
    }

    return true;
}

static bool parseBridgeEndpoint(const char *text, AdminBridgeEndpoint *endpoint)
{
    return InterfaceName_Split(text, endpoint->agent_name, endpoint->interface_name);
}

static bool parseAclLevel(const char *text)
{
    target_can_read = false;
    target_can_write = false;
    target_can_admin = false;

    if (strcmp(text, "rwa") == 0) {
        target_can_admin = true;
    }
    if (target_can_admin || strcmp(text, "rw") == 0) {
        target_can_write = true;
    }
    if (target_can_write || strcmp(text, "r") == 0) {
        target_can_read = true;
    }

    return target_can_read || strcmp(text, "none") == 0;
}

static bool parseNamespacedInterface(const char *text)
{
    const char *separator = strchr(text, '/');
    size_t agent_length;

    if (separator == NULL || separator == text || separator[1] == '\0') {
        return false;
    }

    agent_length = (size_t)(separator - text);
    if (agent_length >= sizeof(target_agent)) {
        return false;
    }

    memcpy(target_agent, text, agent_length);
    target_agent[agent_length] = '\0';
    snprintf(target_interface, sizeof(target_interface), "%s", separator + 1);

    return true;
}

static bool parseInterfaceRef(const char *text)
{
    const char *separator;
    size_t agent_length;

    if (target_fingerprint[0] == '\0') {
        return false;
    }

    if (strcmp(text, "*") == 0) {
        snprintf(target_agent, sizeof(target_agent), "*");
        snprintf(target_interface, sizeof(target_interface), "*");
        return true;
    }

    separator = strchr(text, '/');
    if (separator == NULL || separator == text || separator[1] == '\0') {
        return false;
    }

    agent_length = (size_t)(separator - text);
    if (agent_length >= sizeof(target_agent)) {
        return false;
    }

    memcpy(target_agent, text, agent_length);
    target_agent[agent_length] = '\0';
    snprintf(target_interface, sizeof(target_interface), "%s", separator + 1);

    return true;
}

static bool initTransport(const char *host, const char *port_text, const TransportEvents *events)
{
    (void)port_text;

    return TcpClientTransport_InitUnix(&transport, host, events);
}

/* ---------- private: transport events ---------- */

static void onConnected(void *context)
{
    TcpClientTransport *self = context;
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size;

    encoded_size = HelloMessage_Build(kPEER_ROLE_ADMIN, NULL, 0, encoded, sizeof(encoded));
    self->port.send_control(self->port.context, encoded, encoded_size);

    sendRequest();
}

static void onDisconnected(void *context, uint64_t now_us)
{
    (void)context;
    (void)now_us;

    LOG_WARN("connection lost");
    exit_code = 1;
}

static void onControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    MessageHeader header;
    AdminKickReplyMessage kick_reply;
    AdminKickPeerReplyMessage kick_peer_reply;
    AdminForgetReplyMessage forget_reply;
    AdminPinAddReplyMessage pin_add_reply;
    AdminAclSetReplyMessage acl_set_reply;
    AdminAclRevokeReplyMessage acl_revoke_reply;
    AdminBridgeSetReplyMessage bridge_set_reply;
    AdminBridgeDeleteReplyMessage bridge_delete_reply;
    AdminGroupReplyMessage group_reply;
    ConfigureReplyMessage configure_reply;
    ErrorMessage error;
    char peer_id_text[16];

    (void)context;
    (void)now_us;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }

    if (header.type == kMESSAGE_TYPE_ERROR) {
        if (ErrorMessage_Decode(&error, data + MESSAGE_HEADER_SIZE, header.length)) {
            LOG_ERROR("hub error %u: %s", error.code, error.detail);
        }
        exit_code = 1;
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_STATUS_REPLY) {
        handleStatusReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_PEERS_REPLY) {
        handlePeersReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_AGENTS_REPLY) {
        handleAgentsReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_CLIENTS_REPLY) {
        handleClientsReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_INTERFACES_REPLY) {
        handleInterfacesReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_LIST_REPLY) {
        handleListReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_PINS_REPLY) {
        handlePinsReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_REJECTED_REPLY) {
        handleRejectedReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_KICK_REPLY) {
        AdminKickReplyMessage_Decode(&kick_reply, data + MESSAGE_HEADER_SIZE, header.length);
        printActionReply(kick_reply.status, "kicked", "agent", target_agent);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_KICK_PEER_REPLY) {
        AdminKickPeerReplyMessage_Decode(&kick_peer_reply, data + MESSAGE_HEADER_SIZE, header.length);
        snprintf(peer_id_text, sizeof(peer_id_text), "0x%08X", target_peer_id);
        printActionReply(kick_peer_reply.status, "kicked", "peer", peer_id_text);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_PIN_ADD_REPLY) {
        AdminPinAddReplyMessage_Decode(&pin_add_reply, data + MESSAGE_HEADER_SIZE, header.length);
        if (pin_add_reply.status != ADMIN_STATUS_OK) {
            LOG_ERROR("could not pin %s", target_agent);
            exit_code = 1;
            return;
        }
        printf("pinned %s %s\n", target_agent, target_fingerprint);
        exit_code = 0;
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_FORGET_REPLY) {
        AdminForgetReplyMessage_Decode(&forget_reply, data + MESSAGE_HEADER_SIZE, header.length);
        printActionReply(forget_reply.status, "deleted pin for", "agent", target_agent);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_ACL_LIST_REPLY) {
        handleAclListReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_ACL_SET_REPLY) {
        AdminAclSetReplyMessage_Decode(&acl_set_reply, data + MESSAGE_HEADER_SIZE, header.length);
        if (acl_set_reply.status != ADMIN_STATUS_OK) {
            LOG_ERROR("could not set acl");
            exit_code = 1;
            return;
        }
        printf(
            "acl %s %s/%s %s\n",
            target_fingerprint,
            target_agent,
            target_interface,
            aclLevelName(target_can_read, target_can_write, target_can_admin)
        );
        exit_code = 0;
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_ACL_REVOKE_REPLY) {
        AdminAclRevokeReplyMessage_Decode(&acl_revoke_reply, data + MESSAGE_HEADER_SIZE, header.length);
        if (acl_revoke_reply.status != ADMIN_STATUS_OK) {
            LOG_ERROR("no acl for %s on %s/%s", target_fingerprint, target_agent, target_interface);
            exit_code = 1;
            return;
        }
        printf("deleted %s %s/%s\n", target_fingerprint, target_agent, target_interface);
        exit_code = 0;
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_BRIDGE_LIST_REPLY) {
        handleBridgeListReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_BRIDGE_SET_REPLY) {
        if (AdminBridgeSetReplyMessage_Decode(&bridge_set_reply, data + MESSAGE_HEADER_SIZE, header.length)) {
            printBridgeReply(bridge_set_reply.status, "bridge");
        }
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_BRIDGE_DELETE_REPLY) {
        if (AdminBridgeDeleteReplyMessage_Decode(&bridge_delete_reply, data + MESSAGE_HEADER_SIZE, header.length)) {
            printBridgeReply(bridge_delete_reply.status, "deleted bridge");
        }
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_GROUP_LIST_REPLY) {
        handleGroupListReply(data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_ADMIN_GROUP_SET_REPLY || header.type == kMESSAGE_TYPE_ADMIN_GROUP_DELETE_REPLY
        || header.type == kMESSAGE_TYPE_ADMIN_GROUP_JOIN_REPLY || header.type == kMESSAGE_TYPE_ADMIN_GROUP_LEAVE_REPLY) {
        if (AdminGroupReplyMessage_Decode(&group_reply, data + MESSAGE_HEADER_SIZE, header.length)) {
            printGroupReply(group_reply.status);
        }
        return;
    }
    if (header.type == kMESSAGE_TYPE_CONFIGURE_REPLY) {
        if (!ConfigureReplyMessage_Decode(&configure_reply, data + MESSAGE_HEADER_SIZE, header.length)) {
            exit_code = 1;
            return;
        }
        printConfigureReply(&configure_reply);
        return;
    }
}

static void onFrame(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    (void)data;
    (void)size;
}

/* ---------- private: requests ---------- */

static void sendRequest(void)
{
    AdminPeersMessage peers = { page_offset };
    AdminPinsMessage pins = { page_offset };
    AdminInterfacesMessage interfaces = { page_offset };
    AdminAgentsMessage agents;
    AdminClientsMessage clients;
    AdminKickMessage kick;
    AdminKickPeerMessage kick_peer = { target_peer_id };
    AdminForgetMessage forget;
    AdminPinAddMessage pin_add;
    AdminAclSetMessage acl_set;
    AdminAclRevokeMessage acl_revoke;
    AdminAclListMessage acl_list = { page_offset };
    AdminRejectedMessage rejected = { page_offset };
    AdminBridgeListMessage bridge_list = { page_offset };
    AdminBridgeDeleteMessage bridge_delete;
    AdminGroupDeleteMessage group_delete;
    AdminGroupMemberMessage group_member;
    AdminGroupListMessage group_list = { page_offset };
    ConfigureMessage configure;
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size = 0;

    memset(&agents, 0, sizeof(agents));
    memset(&clients, 0, sizeof(clients));
    agents.offset = page_offset;
    clients.offset = page_offset;

    if (command == kCLI_COMMAND_STATUS) {
        encoded_size = AdminStatusMessage_Encode(encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_PEERS) {
        encoded_size = AdminPeersMessage_Encode(&peers, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_PEERS_KICK) {
        encoded_size = AdminKickPeerMessage_Encode(&kick_peer, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_AGENTS) {
        encoded_size = AdminAgentsMessage_Encode(&agents, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_AGENTS_SHOW) {
        sendShowStage(show_stage);
        return;
    } else if (command == kCLI_COMMAND_AGENTS_KICK) {
        memset(&kick, 0, sizeof(kick));
        snprintf(kick.agent_name, sizeof(kick.agent_name), "%s", target_agent);
        encoded_size = AdminKickMessage_Encode(&kick, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_CLIENTS) {
        encoded_size = AdminClientsMessage_Encode(&clients, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_INTERFACES) {
        encoded_size = AdminInterfacesMessage_Encode(&interfaces, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_PINS) {
        encoded_size = AdminPinsMessage_Encode(&pins, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_PENDING) {
        encoded_size = AdminRejectedMessage_Encode(&rejected, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_PINS_FORGET) {
        memset(&forget, 0, sizeof(forget));
        snprintf(forget.agent_name, sizeof(forget.agent_name), "%s", target_agent);
        encoded_size = AdminForgetMessage_Encode(&forget, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_PINS_ADD) {
        memset(&pin_add, 0, sizeof(pin_add));
        snprintf(pin_add.agent_name, sizeof(pin_add.agent_name), "%s", target_agent);
        snprintf(pin_add.fingerprint_hex, sizeof(pin_add.fingerprint_hex), "%s", target_fingerprint);
        encoded_size = AdminPinAddMessage_Encode(&pin_add, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_ACL) {
        encoded_size = AdminAclListMessage_Encode(&acl_list, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_ACL_ADD) {
        memset(&acl_set, 0, sizeof(acl_set));
        snprintf(acl_set.agent_name, sizeof(acl_set.agent_name), "%s", target_agent);
        snprintf(acl_set.interface_name, sizeof(acl_set.interface_name), "%s", target_interface);
        snprintf(acl_set.fingerprint_hex, sizeof(acl_set.fingerprint_hex), "%s", target_fingerprint);
        acl_set.can_read = target_can_read ? 1 : 0;
        acl_set.can_write = target_can_write ? 1 : 0;
        acl_set.can_admin = target_can_admin ? 1 : 0;
        encoded_size = AdminAclSetMessage_Encode(&acl_set, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_ACL_REVOKE) {
        memset(&acl_revoke, 0, sizeof(acl_revoke));
        snprintf(acl_revoke.agent_name, sizeof(acl_revoke.agent_name), "%s", target_agent);
        snprintf(acl_revoke.interface_name, sizeof(acl_revoke.interface_name), "%s", target_interface);
        snprintf(acl_revoke.fingerprint_hex, sizeof(acl_revoke.fingerprint_hex), "%s", target_fingerprint);
        encoded_size = AdminAclRevokeMessage_Encode(&acl_revoke, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_BRIDGES) {
        encoded_size = AdminBridgeListMessage_Encode(&bridge_list, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_BRIDGE_ADD) {
        encoded_size = AdminBridgeSetMessage_Encode(&target_bridge, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_BRIDGE_DELETE) {
        bridge_delete.source = target_bridge.source;
        bridge_delete.destination = target_bridge.destination;
        encoded_size = AdminBridgeDeleteMessage_Encode(&bridge_delete, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_GROUPS) {
        encoded_size = AdminGroupListMessage_Encode(&group_list, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_GROUP_ADD) {
        encoded_size = AdminGroupSetMessage_Encode(&target_group, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_GROUP_DELETE) {
        memcpy(group_delete.name, target_group.name, sizeof(group_delete.name));
        encoded_size = AdminGroupDeleteMessage_Encode(&group_delete, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_GROUP_JOIN || command == kCLI_COMMAND_GROUP_LEAVE) {
        memcpy(group_member.name, target_group.name, sizeof(group_member.name));
        group_member.member = target_member;
        encoded_size = AdminGroupMemberMessage_Encode(
            command == kCLI_COMMAND_GROUP_JOIN ? kMESSAGE_TYPE_ADMIN_GROUP_JOIN : kMESSAGE_TYPE_ADMIN_GROUP_LEAVE,
            &group_member,
            encoded,
            sizeof(encoded)
        );
    } else if (command == kCLI_COMMAND_INTERFACE_SET || command == kCLI_COMMAND_INTERFACE_SHOW) {
        memset(&configure, 0, sizeof(configure));
        snprintf(configure.agent_name, sizeof(configure.agent_name), "%s", target_agent);
        snprintf(configure.interface_name, sizeof(configure.interface_name), "%s", target_interface);
        configure.op = command == kCLI_COMMAND_INTERFACE_SHOW ? IFCONFIG_OP_QUERY : IFCONFIG_OP_APPLY;
        configure.settings = target_settings;
        encoded_size = ConfigureMessage_Encode(&configure, encoded, sizeof(encoded));
    }

    if (encoded_size == 0) {
        exit_code = 1;
        return;
    }

    transport.port.send_control(transport.port.context, encoded, encoded_size);
}

static void sendShowStage(uint8_t stage)
{
    AdminAgentsMessage agents;
    AdminClientsMessage clients;
    ListMessage list = { page_offset };
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size = 0;

    if (stage == kCLI_SHOW_STAGE_AGENT) {
        memset(&agents, 0, sizeof(agents));
        agents.offset = page_offset;
        snprintf(agents.agent_name, sizeof(agents.agent_name), "%s", target_agent);
        encoded_size = AdminAgentsMessage_Encode(&agents, encoded, sizeof(encoded));
    } else if (stage == kCLI_SHOW_STAGE_INTERFACES) {
        encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));
    } else if (stage == kCLI_SHOW_STAGE_CLIENTS) {
        memset(&clients, 0, sizeof(clients));
        clients.offset = page_offset;
        snprintf(clients.agent_name, sizeof(clients.agent_name), "%s", target_agent);
        encoded_size = AdminClientsMessage_Encode(&clients, encoded, sizeof(encoded));
    }

    if (encoded_size == 0) {
        exit_code = 1;
        return;
    }

    transport.port.send_control(transport.port.context, encoded, encoded_size);
}

/* ---------- private: replies ---------- */

static void handleStatusReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminStatusReplyMessage reply;

    if (!AdminStatusReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    printf("peers: %u (agents %u, clients %u)\n", reply.peer_count, reply.agent_count, reply.client_count);
    printf("interfaces: %u\n", reply.interface_count);
    printf(
        "frames: received %llu, forwarded %llu, dropped %llu, unroutable %llu\n",
        (unsigned long long)reply.frames_received,
        (unsigned long long)reply.frames_forwarded,
        (unsigned long long)reply.frames_dropped,
        (unsigned long long)reply.frames_unroutable
    );
    printf("receive overflows: %u\n", reply.receive_overflows);
    exit_code = 0;
}

static void handlePeersReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminPeersReplyMessage reply;
    uint8_t i;

    if (!AdminPeersReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    if (complete_mode) {
        for(i=0; i<reply.count; i++) {
            printf("0x%08X\n", reply.entries[i].peer_id);
        }
        if (!requestNextPage(reply.flags, reply.count)) {
            exit_code = 0;
        }
        return;
    }

    if (!page_header_printed) {
        printf(
            "%-12s %-8s %-6s %-24s %-22s %-8s %-10s %-8s %s\n",
            "peer-id", "role", "via", "name", "origin", "uptime", "forwarded", "dropped", "fingerprint"
        );
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        char uptime[16];

        formatUptime(reply.entries[i].uptime_seconds, uptime, sizeof(uptime));
        printf(
            "0x%08X   %-8s %-6s %-24s %-22s %-8s %-10u %-8u %s\n",
            reply.entries[i].peer_id,
            roleName(reply.entries[i].role),
            transportName(reply.entries[i].transport_kind),
            textOrDash(reply.entries[i].agent_name),
            textOrDash(reply.entries[i].origin),
            uptime,
            reply.entries[i].frames_forwarded,
            reply.entries[i].frames_dropped,
            textOrDash(reply.entries[i].fingerprint_hex)
        );
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

static void handleAgentsReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminAgentsReplyMessage reply;
    uint8_t i;

    if (!AdminAgentsReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    if (complete_mode) {
        for(i=0; i<reply.count; i++) {
            printf("%s\n", reply.entries[i].agent_name);
        }
        if (!requestNextPage(reply.flags, reply.count)) {
            exit_code = 0;
        }
        return;
    }

    if (command == kCLI_COMMAND_AGENTS_SHOW) {
        if (reply.count == 0) {
            LOG_ERROR("unknown agent %s", target_agent);
            exit_code = 1;
            return;
        }
        printf(
            "agent: %s   peer-id: 0x%08X   fingerprint: %s\n",
            reply.entries[0].agent_name,
            reply.entries[0].peer_id,
            textOrDash(reply.entries[0].fingerprint_hex)
        );
        printf("interfaces:\n");
        show_stage = kCLI_SHOW_STAGE_INTERFACES;
        page_offset = 0;
        sendShowStage(show_stage);
        return;
    }

    if (!page_header_printed) {
        printf("%-12s %-32s %-10s %s\n", "peer-id", "name", "interfaces", "fingerprint");
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        printf(
            "0x%08X   %-32s %-10u %s\n",
            reply.entries[i].peer_id,
            reply.entries[i].agent_name,
            reply.entries[i].interface_count,
            textOrDash(reply.entries[i].fingerprint_hex)
        );
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

static void handleClientsReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminClientsReplyMessage reply;
    const char *indent = command == kCLI_COMMAND_AGENTS_SHOW ? "  " : "";
    uint8_t i;

    if (!AdminClientsReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    if (!page_header_printed) {
        if (command == kCLI_COMMAND_AGENTS_SHOW) {
            printf("%s%-12s %-8s %-16s %-10s %s\n", indent, "peer-id", "channel", "interface", "forwarded", "dropped");
        } else {
            printf("%-12s %-8s %-13s %-32s %-16s %-10s %s\n", "peer-id", "channel", "interface-id", "agent", "interface", "forwarded", "dropped");
        }
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        printClientRow(&reply.entries[i], indent);
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

static void handleInterfacesReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminInterfacesReplyMessage reply;
    uint8_t i;

    if (!AdminInterfacesReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    if (complete_mode) {
        for(i=0; i<reply.count; i++) {
            printf("%s/%s\n", reply.entries[i].agent_name, reply.entries[i].interface_name);
        }
        if (!requestNextPage(reply.flags, reply.count)) {
            exit_code = 0;
        }
        return;
    }

    if (!page_header_printed) {
        printf(
            "%-10s %-32s %-16s %-12s %-12s %s\n",
            "id", "agent", "interface", "subscribers", "frames", "tx-dropped"
        );
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        printf(
            "%-10u %-32s %-16s %-12u %-12llu %llu\n",
            reply.entries[i].interface_id,
            reply.entries[i].agent_name,
            reply.entries[i].interface_name,
            reply.entries[i].subscriber_count,
            (unsigned long long)reply.entries[i].frames_received,
            (unsigned long long)reply.entries[i].tx_dropped
        );
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

static void handleListReply(const uint8_t *payload, uint16_t payload_length)
{
    ListReplyMessage reply;
    uint8_t i;

    if (!ListReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    for(i=0; i<reply.count; i++) {
        if (strcmp(reply.entries[i].agent_name, target_agent) == 0) {
            printf("  %-10u %s\n", reply.entries[i].interface_id, reply.entries[i].interface_name);
        }
    }

    if (reply.flags & LIST_REPLY_FLAG_MORE) {
        page_offset += reply.count;
        sendShowStage(show_stage);
        return;
    }

    printf("clients:\n");
    show_stage = kCLI_SHOW_STAGE_CLIENTS;
    page_offset = 0;
    page_header_printed = false;
    sendShowStage(show_stage);
}

static void handlePinsReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminPinsReplyMessage reply;
    uint8_t i;

    if (!AdminPinsReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    if (!page_header_printed) {
        printf("%-32s %s\n", "agent", "fingerprint");
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        printf("%-32s %s\n", reply.entries[i].agent_name, reply.entries[i].fingerprint_hex);
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

static void handleAclListReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminAclListReplyMessage reply;
    uint8_t i;

    if (!AdminAclListReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    if (!page_header_printed) {
        printf("%-32s %-16s %-4s %s\n", "interface", "perm", "", "client");
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        printf(
            "%s/%-*s %-4s %s\n",
            reply.entries[i].agent_name,
            REGISTER_INTERFACE_NAME_SIZE,
            reply.entries[i].interface_name,
            aclLevelName(reply.entries[i].can_read != 0, reply.entries[i].can_write != 0, reply.entries[i].can_admin != 0),
            reply.entries[i].fingerprint_hex
        );
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

static void handleRejectedReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminRejectedReplyMessage reply;
    char last_seen[16];
    uint8_t i;

    if (!AdminRejectedReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    if (!page_header_printed) {
        printf(
            "%-8s %-24s %-22s %-20s %-8s %-9s %s\n",
            "role", "name", "origin", "reason", "attempts", "last-seen", "fingerprint"
        );
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        formatUptime(reply.entries[i].last_seen_seconds, last_seen, sizeof(last_seen));
        printf(
            "%-8s %-24s %-22s %-20s %-8u %-9s %s\n",
            roleName(reply.entries[i].role),
            textOrDash(reply.entries[i].name),
            textOrDash(reply.entries[i].origin),
            rejectionReasonName(reply.entries[i].reason),
            reply.entries[i].attempts,
            last_seen,
            reply.entries[i].fingerprint_hex
        );
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

/* ---------- private: printing ---------- */

static void printClientRow(const AdminClientsReplyEntry *entry, const char *indent)
{
    char channel_text[8] = "-";
    char interface_id_text[16] = "-";

    if (entry->channel != ADMIN_CLIENT_NO_CHANNEL) {
        snprintf(channel_text, sizeof(channel_text), "%u", entry->channel);
        snprintf(interface_id_text, sizeof(interface_id_text), "%u", entry->interface_id);
    }

    if (command == kCLI_COMMAND_AGENTS_SHOW) {
        printf(
            "%s0x%08X   %-8s %-16s %-10u %u\n",
            indent,
            entry->peer_id,
            channel_text,
            textOrDash(entry->interface_name),
            entry->frames_forwarded,
            entry->frames_dropped
        );
        return;
    }

    printf(
        "0x%08X   %-8s %-13s %-32s %-16s %-10u %u\n",
        entry->peer_id,
        channel_text,
        interface_id_text,
        textOrDash(entry->agent_name),
        textOrDash(entry->interface_name),
        entry->frames_forwarded,
        entry->frames_dropped
    );
}

static void handleBridgeListReply(const uint8_t *payload, uint16_t payload_length)
{
    AdminBridgeListReplyMessage reply;
    const AdminBridgeListReplyEntry *entry;
    char source[REGISTER_AGENT_NAME_SIZE + REGISTER_INTERFACE_NAME_SIZE];
    char destination[REGISTER_AGENT_NAME_SIZE + REGISTER_INTERFACE_NAME_SIZE];
    char filters[SUBSCRIBE_FILTERS_MAX * FILTER_TEXT_SIZE];
    uint8_t i;

    if (!AdminBridgeListReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    if (!page_header_printed) {
        printf("%-24s %-24s %-8s %-12s %-12s %s\n", "source", "destination", "state", "forwarded", "dropped", "ids");
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        entry = &reply.entries[i];
        formatEndpoint(&entry->source, source, sizeof(source));
        formatEndpoint(&entry->destination, destination, sizeof(destination));
        formatBridgeFilters(entry->filters, entry->filter_count, filters, sizeof(filters));
        printf(
            "%-24s %-24s %-8s %-12llu %-12llu %s\n",
            source,
            destination,
            entry->state == (ADMIN_BRIDGE_STATE_SOURCE_ONLINE | ADMIN_BRIDGE_STATE_DESTINATION_ONLINE) ? "active" : "waiting",
            (unsigned long long)entry->forwarded,
            (unsigned long long)entry->dropped,
            filters
        );
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

static void handleGroupListReply(const uint8_t *payload, uint16_t payload_length)
{
    static AdminGroupListReplyMessage reply;
    const AdminGroupListReplyEntry *entry;
    char member[REGISTER_AGENT_NAME_SIZE + REGISTER_INTERFACE_NAME_SIZE];
    char filters[SUBSCRIBE_FILTERS_MAX * FILTER_TEXT_SIZE];
    uint8_t i;
    uint8_t m;

    if (!AdminGroupListReplyMessage_Decode(&reply, payload, payload_length)) {
        exit_code = 1;
        return;
    }

    for(i=0; i<reply.count; i++) {
        entry = &reply.entries[i];
        formatBridgeFilters(entry->filters, entry->filter_count, filters, sizeof(filters));
        printf(
            "%s  forwarded %llu  dropped %llu  ids %s\n",
            entry->name,
            (unsigned long long)entry->forwarded,
            (unsigned long long)entry->dropped,
            filters
        );
        for(m=0; m<entry->member_count; m++) {
            formatEndpoint(&entry->members[m].endpoint, member, sizeof(member));
            printf("  %-24s %s\n", member, (entry->members[m].state & ADMIN_GROUP_MEMBER_ONLINE) ? "online" : "offline");
        }
    }

    if (!requestNextPage(reply.flags, reply.count)) {
        exit_code = 0;
    }
}

static void printGroupReply(uint8_t status)
{
    char member[REGISTER_AGENT_NAME_SIZE + REGISTER_INTERFACE_NAME_SIZE];
    char filters[SUBSCRIBE_FILTERS_MAX * FILTER_TEXT_SIZE];

    if (status != ADMIN_BRIDGE_STATUS_OK) {
        LOG_ERROR("%s", groupStatusText(status));
        exit_code = 1;
        return;
    }

    exit_code = 0;
    formatEndpoint(&target_member, member, sizeof(member));
    if (command == kCLI_COMMAND_GROUP_ADD) {
        formatBridgeFilters(target_group.filters, target_group.filter_count, filters, sizeof(filters));
        printf("group %s %s\n", target_group.name, filters);
    } else if (command == kCLI_COMMAND_GROUP_DELETE) {
        printf("deleted group %s\n", target_group.name);
    } else if (command == kCLI_COMMAND_GROUP_JOIN) {
        printf("%s joined %s\n", member, target_group.name);
    } else {
        printf("%s left %s\n", member, target_group.name);
    }
}

static const char *groupStatusText(uint8_t status)
{
    switch (status) {
        case ADMIN_BRIDGE_STATUS_INVALID:
            return "a group name is 1-15 of A-Z a-z 0-9 . _ -";
        case ADMIN_BRIDGE_STATUS_FULL:
            return "the hub holds no more groups, or the group already has 16 members";
        case ADMIN_BRIDGE_STATUS_UNKNOWN:
            return "no such group or member";
        default:
            return AdminBridgeStatus_Name(status);
    }
}

static void printBridgeReply(uint8_t status, const char *action)
{
    char source[REGISTER_AGENT_NAME_SIZE + REGISTER_INTERFACE_NAME_SIZE];
    char destination[REGISTER_AGENT_NAME_SIZE + REGISTER_INTERFACE_NAME_SIZE];
    char filters[SUBSCRIBE_FILTERS_MAX * FILTER_TEXT_SIZE];

    if (status != ADMIN_BRIDGE_STATUS_OK) {
        LOG_ERROR("%s", AdminBridgeStatus_Name(status));
        exit_code = 1;
        return;
    }

    formatEndpoint(&target_bridge.source, source, sizeof(source));
    formatEndpoint(&target_bridge.destination, destination, sizeof(destination));
    exit_code = 0;
    if (command != kCLI_COMMAND_BRIDGE_ADD) {
        printf("%s %s -> %s\n", action, source, destination);
        return;
    }

    formatBridgeFilters(target_bridge.filters, target_bridge.filter_count, filters, sizeof(filters));
    printf("%s %s -> %s %s\n", action, source, destination, filters);
}

static void formatBridgeFilters(const CanFilter *filters, uint8_t filter_count, char *out, size_t out_size)
{
    char filter[FILTER_TEXT_SIZE];
    size_t position = 0;
    uint8_t i;

    snprintf(out, out_size, "%s", filter_count == 0 ? "all" : "");
    for(i=0; i<filter_count; i++) {
        FilterSpec_Format(&filters[i], filter, sizeof(filter));
        position += (size_t)snprintf(out + position, out_size - position, "%s%s", i == 0 ? "" : " ", filter);
    }
}

static void formatEndpoint(const AdminBridgeEndpoint *endpoint, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s", endpoint->agent_name, endpoint->interface_name);
}

static void printActionReply(uint8_t status, const char *action, const char *target_kind, const char *target)
{
    if (status != ADMIN_STATUS_OK) {
        LOG_ERROR("unknown %s %s", target_kind, target);
        exit_code = 1;
        return;
    }

    printf("%s %s %s\n", action, target_kind, target);
    exit_code = 0;
}

static void printConfigureReply(const ConfigureReplyMessage *reply)
{
    if (reply->status != CONFIGURE_STATUS_OK) {
        LOG_ERROR(
            "%s/%s: %s%s%s",
            target_agent,
            target_interface,
            ConfigureStatus_Name(reply->status),
            reply->detail[0] != '\0' ? ": " : "",
            reply->detail
        );
        exit_code = 1;
        return;
    }

    printf("%s/%s\n", target_agent, target_interface);
    InterfaceSettingsText_Print(stdout, &reply->settings);
    exit_code = 0;
}

static const char *aclLevelName(bool can_read, bool can_write, bool can_admin)
{
    if (can_admin) {
        return "rwa";
    }
    if (can_write) {
        return "rw";
    }

    return can_read ? "r" : "none";
}

static const char *roleName(uint8_t role)
{
    if (role == kPEER_ROLE_AGENT) {
        return "agent";
    }
    if (role == kPEER_ROLE_CLIENT) {
        return "client";
    }
    if (role == kPEER_ROLE_ADMIN) {
        return "admin";
    }

    return "unknown";
}

static const char *rejectionReasonName(uint8_t reason)
{
    if (reason == ADMIN_REJECTED_REASON_FINGERPRINT_MISMATCH) {
        return "fingerprint-mismatch";
    }

    return "not-allowed";
}

static const char *transportName(uint8_t transport_kind)
{
    if (transport_kind == kPEER_TRANSPORT_UNIX) {
        return "unix";
    }
    if (transport_kind == kPEER_TRANSPORT_TCP) {
        return "tcp";
    }
    if (transport_kind == kPEER_TRANSPORT_TLS) {
        return "tls";
    }
    if (transport_kind == kPEER_TRANSPORT_QUIC) {
        return "quic";
    }

    return "-";
}

static void formatUptime(uint32_t seconds, char *out, size_t size)
{
    if (seconds < 60) {
        snprintf(out, size, "%us", seconds);
        return;
    }
    if (seconds < 3600) {
        snprintf(out, size, "%um%us", seconds / 60, seconds % 60);
        return;
    }
    if (seconds < 86400) {
        snprintf(out, size, "%uh%um", seconds / 3600, (seconds % 3600) / 60);
        return;
    }
    snprintf(out, size, "%ud%uh", seconds / 86400, (seconds % 86400) / 3600);
}

static const char *textOrDash(const char *text)
{
    return text[0] != '\0' ? text : "-";
}

static bool requestNextPage(uint8_t flags, uint8_t count)
{
    if ((flags & ADMIN_REPLY_FLAG_MORE) == 0) {
        return false;
    }

    page_offset += count;
    if (command == kCLI_COMMAND_AGENTS_SHOW) {
        sendShowStage(show_stage);
    } else {
        sendRequest();
    }

    return true;
}

/* ---------- private: event loop ---------- */

static int32_t runEventLoop(void)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    EpollRegistry poll_registry;
    int32_t event_count;
    uint32_t wanted_mask;
    int32_t current_fd;
    int32_t i;

    if (!EpollRegistry_Open(&poll_registry)) {
        return 1;
    }

    while (exit_code < 0) {
        current_fd = TcpClientTransport_Fd(&transport);
        wanted_mask = EPOLLIN;
        if (TcpClientTransport_WantsWritable(&transport)) {
            wanted_mask |= EPOLLOUT;
        }
        EpollRegistry_SyncSlot(&poll_registry, TCP_SLOT, current_fd, wanted_mask, (uint32_t)current_fd);

        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);
        for(i=0; i<event_count; i++) {
            if (events[i].events & EPOLLOUT) {
                TcpClientTransport_OnWritable(&transport);
            }
            if (events[i].events & EPOLLIN) {
                TcpClientTransport_OnReadable(&transport);
            }
        }
    }

    return exit_code;
}
