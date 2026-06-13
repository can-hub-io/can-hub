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
#include "platform/linux/shared/hub_defaults.h"
#include "platform/linux/shared/log.h"
#include "platform/linux/tcp/tcp_client_transport.h"
#include "hub/ports/hub_transport_events.h"
#include "protocol/admin_message.h"
#include "protocol/error_message.h"
#include "protocol/hello_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"

#define MAX_EPOLL_EVENTS 8
#define POLL_PERIOD_MS 100
#define TCP_SLOT 0
#define COMMAND_BUFFER_SIZE 256
#define COMMAND_WORDS_MAX 5

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
    kCLI_COMMAND_INTERFACE_SET,
    kCLI_COMMAND_INTERFACE_UP,
    kCLI_COMMAND_INTERFACE_DOWN,
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
static uint32_t target_peer_id;
static uint32_t target_bitrate;
static uint16_t page_offset;
static bool page_header_printed;
static bool complete_mode;
static int32_t exit_code = -1;

static void printUsage(FILE *stream, const char *program);
static bool parseArguments(int argc, char **argv, char *host, char *port_text);
static bool mapCommand(const char *words[], uint8_t word_count);
static bool mapCompleteKind(const char *kind);
static bool parsePeerId(const char *text);
static bool parseBitrate(const char *text);
static bool parseInterfaceRef(const char *text);
static bool parseNamespacedInterface(const char *text);
static bool initTransport(const char *host, const char *port_text, const TransportEvents *events);
static void onConnected(void *context);
static void onDisconnected(void *context, uint64_t now_us);
static void onControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void onFrame(void *context, const uint8_t *data, size_t size);
static void sendRequest(void);
static bool isInterfaceCommand(void);
static uint8_t ifconfigOp(void);
static void sendShowStage(uint8_t stage);
static void handleStatusReply(const uint8_t *payload, uint16_t payload_length);
static void handlePeersReply(const uint8_t *payload, uint16_t payload_length);
static void handleAgentsReply(const uint8_t *payload, uint16_t payload_length);
static void handleClientsReply(const uint8_t *payload, uint16_t payload_length);
static void handleInterfacesReply(const uint8_t *payload, uint16_t payload_length);
static void handleListReply(const uint8_t *payload, uint16_t payload_length);
static void handlePinsReply(const uint8_t *payload, uint16_t payload_length);
static void handleAclListReply(const uint8_t *payload, uint16_t payload_length);
static void printClientRow(const AdminClientsReplyEntry *entry, const char *indent);
static void printActionReply(uint8_t status, const char *action, const char *target_kind, const char *target);
static void printIfconfigReply(uint8_t status);
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
    fprintf(stream, "  interface set <agent>/<iface> bitrate <bps>   reconfigure a bus bitrate\n");
    fprintf(stream, "  interface up|down <agent>/<iface>             bring a bus up or down\n");
    fprintf(stream, "  pins                   pinned TOFU identities\n");
    fprintf(stream, "  pins add <name> <fp>   allow an agent (its --show-identity fingerprint)\n");
    fprintf(stream, "  pins delete <name>     drop a pin so the agent can re-pin\n");
    fprintf(stream, "  acl                    client read/write grants\n");
    fprintf(stream, "  acl add <fp|*> <agent|*>/<iface|*> none|ro|rw   grant (* = baseline)\n");
    fprintf(stream, "  acl delete <fp|*> <agent|*>/<iface|*>          drop a grant\n");
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
        } else {
            return false;
        }
        return true;
    }

    if (word_count == 5) {
        if (strcmp(words[0], "interface") == 0 && strcmp(words[1], "set") == 0 && strcmp(words[3], "bitrate") == 0) {
            command = kCLI_COMMAND_INTERFACE_SET;
            return parseBitrate(words[4]) && parseNamespacedInterface(words[2]);
        }
        if (strcmp(words[0], "acl") != 0 || strcmp(words[1], "add") != 0) {
            return false;
        }
        command = kCLI_COMMAND_ACL_ADD;
        if (strcmp(words[4], "rw") == 0) {
            target_can_read = true;
            target_can_write = true;
        } else if (strcmp(words[4], "ro") == 0) {
            target_can_read = true;
            target_can_write = false;
        } else if (strcmp(words[4], "none") == 0) {
            target_can_read = false;
            target_can_write = false;
        } else {
            return false;
        }
        snprintf(target_fingerprint, sizeof(target_fingerprint), "%s", words[2]);
        return parseInterfaceRef(words[3]);
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
    if (strcmp(words[0], "interface") == 0 && strcmp(words[1], "up") == 0) {
        command = kCLI_COMMAND_INTERFACE_UP;
        return parseNamespacedInterface(words[2]);
    }
    if (strcmp(words[0], "interface") == 0 && strcmp(words[1], "down") == 0) {
        command = kCLI_COMMAND_INTERFACE_DOWN;
        return parseNamespacedInterface(words[2]);
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

static bool parseBitrate(const char *text)
{
    char *end = NULL;

    target_bitrate = (uint32_t)strtoul(text, &end, 10);

    return end != text && *end == '\0' && target_bitrate > 0;
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
    uint8_t encoded[COMMAND_BUFFER_SIZE];
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
    AdminIfconfigReplyMessage ifconfig_reply;
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
            target_can_write ? "rw" : (target_can_read ? "ro" : "none")
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
    if (header.type == kMESSAGE_TYPE_ADMIN_IFCONFIG_REPLY) {
        AdminIfconfigReplyMessage_Decode(&ifconfig_reply, data + MESSAGE_HEADER_SIZE, header.length);
        printIfconfigReply(ifconfig_reply.status);
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
    AdminIfconfigMessage ifconfig;
    uint8_t encoded[COMMAND_BUFFER_SIZE];
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
        encoded_size = AdminAclSetMessage_Encode(&acl_set, encoded, sizeof(encoded));
    } else if (command == kCLI_COMMAND_ACL_REVOKE) {
        memset(&acl_revoke, 0, sizeof(acl_revoke));
        snprintf(acl_revoke.agent_name, sizeof(acl_revoke.agent_name), "%s", target_agent);
        snprintf(acl_revoke.interface_name, sizeof(acl_revoke.interface_name), "%s", target_interface);
        snprintf(acl_revoke.fingerprint_hex, sizeof(acl_revoke.fingerprint_hex), "%s", target_fingerprint);
        encoded_size = AdminAclRevokeMessage_Encode(&acl_revoke, encoded, sizeof(encoded));
    } else if (isInterfaceCommand()) {
        memset(&ifconfig, 0, sizeof(ifconfig));
        snprintf(ifconfig.agent_name, sizeof(ifconfig.agent_name), "%s", target_agent);
        snprintf(ifconfig.interface_name, sizeof(ifconfig.interface_name), "%s", target_interface);
        ifconfig.op = ifconfigOp();
        ifconfig.bitrate = target_bitrate;
        encoded_size = AdminIfconfigMessage_Encode(&ifconfig, encoded, sizeof(encoded));
    }

    if (encoded_size == 0) {
        exit_code = 1;
        return;
    }

    transport.port.send_control(transport.port.context, encoded, encoded_size);
}

static bool isInterfaceCommand(void)
{
    if (command == kCLI_COMMAND_INTERFACE_SET) {
        return true;
    }
    if (command == kCLI_COMMAND_INTERFACE_UP) {
        return true;
    }

    return command == kCLI_COMMAND_INTERFACE_DOWN;
}

static uint8_t ifconfigOp(void)
{
    if (command == kCLI_COMMAND_INTERFACE_UP) {
        return IFCONFIG_OP_UP;
    }
    if (command == kCLI_COMMAND_INTERFACE_DOWN) {
        return IFCONFIG_OP_DOWN;
    }

    return IFCONFIG_OP_SET_BITRATE;
}

static void sendShowStage(uint8_t stage)
{
    AdminAgentsMessage agents;
    AdminClientsMessage clients;
    ListMessage list = { page_offset };
    uint8_t encoded[COMMAND_BUFFER_SIZE];
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
        printf("%-10s %-32s %-16s %-12s %s\n", "id", "agent", "interface", "subscribers", "frames");
        page_header_printed = true;
    }
    for(i=0; i<reply.count; i++) {
        printf(
            "%-10u %-32s %-16s %-12u %llu\n",
            reply.entries[i].interface_id,
            reply.entries[i].agent_name,
            reply.entries[i].interface_name,
            reply.entries[i].subscriber_count,
            (unsigned long long)reply.entries[i].frames_received
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
            reply.entries[i].can_write ? "rw" : (reply.entries[i].can_read ? "ro" : "none"),
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

static void printIfconfigReply(uint8_t status)
{
    if (status == ADMIN_IFCONFIG_STATUS_UNKNOWN_INTERFACE) {
        LOG_ERROR("unknown interface %s/%s", target_agent, target_interface);
        exit_code = 1;
        return;
    }
    if (status == ADMIN_IFCONFIG_STATUS_AGENT_UNREACHABLE) {
        LOG_ERROR("agent for %s/%s is not connected", target_agent, target_interface);
        exit_code = 1;
        return;
    }
    if (status == ADMIN_IFCONFIG_STATUS_APPLY_FAILED) {
        LOG_ERROR("agent could not apply the change to %s/%s (needs CAP_NET_ADMIN)", target_agent, target_interface);
        exit_code = 1;
        return;
    }

    if (command == kCLI_COMMAND_INTERFACE_SET) {
        printf("%s/%s bitrate %u\n", target_agent, target_interface, target_bitrate);
    } else if (command == kCLI_COMMAND_INTERFACE_UP) {
        printf("%s/%s up\n", target_agent, target_interface);
    } else {
        printf("%s/%s down\n", target_agent, target_interface);
    }
    exit_code = 0;
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
