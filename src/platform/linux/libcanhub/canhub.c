#include "canhub.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>

#include <unistd.h>

#include "client/client.h"
#include "protocol/open_message.h"
#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_client_transport.h"
#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/shared/hub_defaults.h"
#include "platform/linux/shared/tls_identity.h"
#include "platform/linux/tcp/tcp_client_transport.h"
#include "platform/linux/tls/tls_client_transport.h"

#define IDENTITY_NAME "client"
#define KNOWN_HUBS_FILE "known_hubs"
#define KNOWN_HUBS_PATH_MAX (TLS_IDENTITY_PATH_MAX + sizeof(KNOWN_HUBS_FILE))
#define PIN_KEY_MAX (CONNECT_URL_HOST_MAX + CONNECT_URL_PORT_TEXT_MAX)

#define MAX_EPOLL_EVENTS 8
#define PUMP_SLICE_MS 50
#define DEFAULT_OPERATION_TIMEOUT_MS 5000
#define LAST_ERROR_SIZE 256
#define FRAME_RING_SIZE 512

#define STREAM_SLOT 0
#define TAG_STREAM 1
#define TAG_QUIC_UDP 2
#define TAG_QUIC_TIMER 3

struct CanHubSession {
    Client client;
    TcpClientTransport tcp_transport;
    TlsClientTransport tls_transport;
    QuicClientTransport quic_transport;
    TransportPort *port;
    EpollRegistry poll_registry;
    uint8_t scheme;
    bool disconnected;
    char state_directory[TLS_IDENTITY_PATH_MAX];
    char identity_certificate_path[TLS_IDENTITY_PATH_MAX];
    char identity_key_path[TLS_IDENTITY_PATH_MAX];
    char known_hubs_path[KNOWN_HUBS_PATH_MAX];
    char pin_key[PIN_KEY_MAX];
    char hub_fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];
    char last_error[LAST_ERROR_SIZE];
    CanHubInterfaceInfo *list_out;
    size_t list_max;
    size_t list_count;
    bool list_done;
    bool list_truncated;
    bool open_done;
    int32_t open_result;
    int32_t hub_error;
    CanHubFrame frame_ring[FRAME_RING_SIZE];
    uint16_t ring_head;
    uint16_t ring_count;
};

static void ignoreSigpipeIfUnhandled(void);
static bool initTransport(CanHubSession *self, const char *host, const char *port_text, const TransportEvents *events);
static bool prepareSecurityMaterial(CanHubSession *self, const CanHubConnectConfig *config, const char *host, const char *port_text);
static bool prepareIdentity(CanHubSession *self, const CanHubConnectConfig *config);
static bool waitForConnection(CanHubSession *self, int32_t timeout_ms);
static void pumpOnce(CanHubSession *self, int32_t timeout_ms);
static void dispatchEvent(CanHubSession *self, const struct epoll_event *event);
static void syncStreamSlot(CanHubSession *self);
static uint64_t deadlineFrom(int32_t timeout_ms);
static int32_t remainingMs(uint64_t deadline_us);
static int32_t effectiveTimeout(int32_t timeout_ms);
static int32_t openResultFromStatus(uint8_t status);
static void setError(CanHubSession *self, const char *message);
static void pushFrame(CanHubSession *self, const FrameMessage *message);
static bool popFrame(CanHubSession *self, CanHubFrame *frame);
static void onListReply(void *context, const ListReplyMessage *reply);
static void onOpenResult(void *context, uint8_t status, uint8_t channel, uint32_t interface_id);
static void onFrame(void *context, const FrameMessage *message);
static void onError(void *context, uint16_t code, const char *detail);
static void onDisconnected(void *context);

/* ---------- public ---------- */

uint32_t canhub_api_version(void)
{
    return CANHUB_API_VERSION;
}

CanHubSession *canhub_connect(const CanHubConnectConfig *config)
{
    CanHubSession *self;
    char host[CONNECT_URL_HOST_MAX];
    char port_text[CONNECT_URL_PORT_TEXT_MAX];
    TransportEvents transport_events;
    ClientEvents client_events;

    if (config == NULL || config->struct_size != sizeof(*config)) {
        return NULL;
    }

    ignoreSigpipeIfUnhandled();

    self = calloc(1, sizeof(*self));
    if (self == NULL) {
        return NULL;
    }

    if (config->url == NULL) {
        self->scheme = kCONNECT_SCHEME_UNIX;
        snprintf(host, sizeof(host), "%s", HUB_DEFAULT_UNIX_SOCKET_PATH);
        port_text[0] = '\0';
    } else if (!ConnectUrl_Parse(config->url, &self->scheme, host, port_text)) {
        free(self);
        return NULL;
    }

    client_events.context = self;
    client_events.on_list_reply = onListReply;
    client_events.on_open_result = onOpenResult;
    client_events.on_frame = onFrame;
    client_events.on_error = onError;
    client_events.on_disconnected = onDisconnected;

    transport_events = Client_TransportEvents(&self->client);

    if (self->scheme == kCONNECT_SCHEME_TLS || self->scheme == kCONNECT_SCHEME_QUIC) {
        if (!prepareSecurityMaterial(self, config, host, port_text)) {
            free(self);
            return NULL;
        }
    }
    if (!initTransport(self, host, port_text, &transport_events)) {
        free(self);
        return NULL;
    }

    Client_Init(&self->client, self->port, &client_events);

    if (!EpollRegistry_Open(&self->poll_registry)) {
        free(self);
        return NULL;
    }
    if (self->scheme == kCONNECT_SCHEME_QUIC) {
        EpollRegistry_AddStatic(&self->poll_registry, QuicClientTransport_UdpFd(&self->quic_transport), TAG_QUIC_UDP);
        EpollRegistry_AddStatic(&self->poll_registry, QuicClientTransport_TimerFd(&self->quic_transport), TAG_QUIC_TIMER);
    }

    if (!self->port->connect(self->port->context)) {
        canhub_close(self);
        return NULL;
    }
    if (!waitForConnection(self, config->connect_timeout_ms)) {
        canhub_close(self);
        return NULL;
    }

    return self;
}

void canhub_close(CanHubSession *session)
{
    if (session == NULL) {
        return;
    }

    if (session->port != NULL) {
        session->port->disconnect(session->port->context);
    }
    if (session->poll_registry.epoll_fd >= 0) {
        close(session->poll_registry.epoll_fd);
    }

    free(session);
}

const char *canhub_last_error(const CanHubSession *session)
{
    return session->last_error;
}

int32_t canhub_list(
    CanHubSession *session,
    CanHubInterfaceInfo *interfaces,
    size_t interfaces_max,
    int32_t timeout_ms
)
{
    uint64_t deadline = deadlineFrom(effectiveTimeout(timeout_ms));

    if (interfaces == NULL || interfaces_max == 0) {
        setError(session, "interfaces buffer required");
        return CANHUB_ERR_ARGUMENT;
    }
    if (Client_State(&session->client) != kCLIENT_READY) {
        setError(session, "list requires a connected session with no open interface");
        return CANHUB_ERR_STATE;
    }

    session->list_out = interfaces;
    session->list_max = interfaces_max;
    session->list_count = 0;
    session->list_done = false;
    session->list_truncated = false;
    session->hub_error = 0;

    Client_RequestList(&session->client, 0);

    while (!session->list_done && session->hub_error == 0 && !session->disconnected) {
        if (remainingMs(deadline) == 0) {
            session->list_out = NULL;
            setError(session, "list timed out");
            return CANHUB_ERR_TIMEOUT;
        }
        pumpOnce(session, remainingMs(deadline));
    }

    session->list_out = NULL;
    if (session->disconnected) {
        return CANHUB_ERR_DISCONNECTED;
    }
    if (session->hub_error != 0) {
        return session->hub_error;
    }

    return (int32_t)session->list_count;
}

int32_t canhub_open(CanHubSession *session, const char *interface, uint32_t flags, int32_t timeout_ms)
{
    uint64_t deadline = deadlineFrom(effectiveTimeout(timeout_ms));
    uint8_t open_flags = 0;
    char *number_end = NULL;
    uint32_t interface_id;

    if (interface == NULL || interface[0] == '\0') {
        setError(session, "interface required");
        return CANHUB_ERR_ARGUMENT;
    }
    if (Client_State(&session->client) != kCLIENT_READY) {
        setError(session, "open requires a connected session with no open interface");
        return CANHUB_ERR_STATE;
    }

    if ((flags & CANHUB_OPEN_FLAG_NO_ECHO) != 0) {
        open_flags |= OPEN_FLAG_SUPPRESS_OWN_ECHO;
    }
    if ((flags & CANHUB_OPEN_FLAG_WRITE) != 0) {
        open_flags |= OPEN_FLAG_WANT_WRITE;
    }

    session->open_done = false;
    session->open_result = CANHUB_OK;
    session->hub_error = 0;

    interface_id = (uint32_t)strtoul(interface, &number_end, 10);
    if (number_end != interface && *number_end == '\0') {
        Client_OpenById(&session->client, interface_id, open_flags);
    } else {
        Client_OpenByName(&session->client, interface, open_flags);
    }

    while (!session->open_done && session->hub_error == 0 && !session->disconnected) {
        if (remainingMs(deadline) == 0) {
            setError(session, "open timed out");
            return CANHUB_ERR_TIMEOUT;
        }
        pumpOnce(session, remainingMs(deadline));
    }

    if (session->disconnected) {
        return CANHUB_ERR_DISCONNECTED;
    }
    if (!session->open_done) {
        return session->hub_error;
    }

    return session->open_result;
}

int32_t canhub_set_filters(CanHubSession *session, const CanHubFilter *filters, uint8_t filter_count)
{
    CanFilter wire_filters[SUBSCRIBE_FILTERS_MAX];
    uint8_t i;

    if (filter_count > CANHUB_FILTERS_MAX) {
        setError(session, "too many filters");
        return CANHUB_ERR_ARGUMENT;
    }

    for(i=0; i<filter_count; i++) {
        wire_filters[i].can_id = filters[i].can_id;
        wire_filters[i].can_mask = filters[i].can_mask;
    }
    Client_SetFilters(&session->client, wire_filters, filter_count);
    pumpOnce(session, 0);

    return CANHUB_OK;
}

int32_t canhub_recv(CanHubSession *session, CanHubFrame *frame, int32_t timeout_ms)
{
    uint64_t deadline = deadlineFrom(timeout_ms);

    if (frame == NULL) {
        setError(session, "frame buffer required");
        return CANHUB_ERR_ARGUMENT;
    }

    for (;;) {
        if (popFrame(session, frame)) {
            return CANHUB_RECEIVED;
        }
        if (session->disconnected) {
            setError(session, "connection lost");
            return CANHUB_ERR_DISCONNECTED;
        }
        if (timeout_ms >= 0 && remainingMs(deadline) == 0) {
            return CANHUB_ERR_TIMEOUT;
        }
        pumpOnce(session, (timeout_ms < 0) ? PUMP_SLICE_MS : remainingMs(deadline));
    }
}

int32_t canhub_send(CanHubSession *session, const CanHubFrame *frame)
{
    FrameMessage message;

    if (frame == NULL || frame->length > CANHUB_FRAME_PAYLOAD_MAX) {
        setError(session, "invalid frame");
        return CANHUB_ERR_ARGUMENT;
    }
    if (session->disconnected) {
        setError(session, "connection lost");
        return CANHUB_ERR_DISCONNECTED;
    }

    memset(&message, 0, sizeof(message));
    message.can_id = frame->can_id;
    message.timestamp_us = (frame->timestamp_us != 0) ? frame->timestamp_us : Clock_RealtimeUs();
    message.payload_length = frame->length;
    message.frame_flags = frame->flags;
    memcpy(message.payload, frame->payload, frame->length);

    if (!Client_SendFrame(&session->client, &message)) {
        setError(session, "send failed: no open writable channel");
        return CANHUB_ERR_STATE;
    }
    pumpOnce(session, 0);

    return CANHUB_OK;
}

/* ---------- private ---------- */

static void ignoreSigpipeIfUnhandled(void)
{
    struct sigaction current;

    if (sigaction(SIGPIPE, NULL, &current) == 0 && current.sa_handler == SIG_DFL) {
        signal(SIGPIPE, SIG_IGN);
    }
}

static bool initTransport(CanHubSession *self, const char *host, const char *port_text, const TransportEvents *events)
{
    TlsClientSecurityConfig tls_config;
    QuicClientSecurityConfig quic_config;

    if (self->scheme == kCONNECT_SCHEME_QUIC) {
        quic_config.certificate_path = self->identity_certificate_path;
        quic_config.key_path = self->identity_key_path;
        quic_config.pin_store_path = self->known_hubs_path;
        quic_config.pin_key = self->pin_key;
        quic_config.pinned_fingerprint = (self->hub_fingerprint[0] != '\0') ? self->hub_fingerprint : NULL;
        if (!QuicClientTransport_Init(&self->quic_transport, host, port_text, events, &quic_config)) {
            return false;
        }
        self->port = QuicClientTransport_Port(&self->quic_transport);
        return true;
    }

    if (self->scheme == kCONNECT_SCHEME_TLS) {
        tls_config.certificate_path = self->identity_certificate_path;
        tls_config.key_path = self->identity_key_path;
        tls_config.pin_store_path = self->known_hubs_path;
        tls_config.pin_key = self->pin_key;
        tls_config.pinned_fingerprint = (self->hub_fingerprint[0] != '\0') ? self->hub_fingerprint : NULL;
        if (!TlsClientTransport_Init(&self->tls_transport, host, port_text, events, &tls_config)) {
            return false;
        }
        self->port = TlsClientTransport_Port(&self->tls_transport);
        return true;
    }

    if (self->scheme == kCONNECT_SCHEME_UNIX) {
        if (!TcpClientTransport_InitUnix(&self->tcp_transport, host, events)) {
            return false;
        }
    } else if (!TcpClientTransport_Init(&self->tcp_transport, host, port_text, events)) {
        return false;
    }
    self->port = TcpClientTransport_Port(&self->tcp_transport);

    return true;
}

static bool prepareSecurityMaterial(CanHubSession *self, const CanHubConnectConfig *config, const char *host, const char *port_text)
{
    if (!prepareIdentity(self, config)) {
        return false;
    }

    if (config->hub_fingerprint != NULL) {
        snprintf(self->hub_fingerprint, sizeof(self->hub_fingerprint), "%s", config->hub_fingerprint);
        return true;
    }

    if (self->state_directory[0] == '\0'
        && !TlsIdentity_ResolveStateDirectory(config->state_directory, self->state_directory)) {
        return false;
    }
    snprintf(self->known_hubs_path, sizeof(self->known_hubs_path), "%s/%s", self->state_directory, KNOWN_HUBS_FILE);
    snprintf(self->pin_key, sizeof(self->pin_key), "%s:%s", host, port_text);

    return true;
}

static bool prepareIdentity(CanHubSession *self, const CanHubConnectConfig *config)
{
    if (config->certificate_path != NULL && config->key_path != NULL) {
        snprintf(self->identity_certificate_path, sizeof(self->identity_certificate_path), "%s", config->certificate_path);
        snprintf(self->identity_key_path, sizeof(self->identity_key_path), "%s", config->key_path);
        return true;
    }

    if (!TlsIdentity_ResolveStateDirectory(config->state_directory, self->state_directory)) {
        return false;
    }

    return TlsIdentity_LoadOrCreate(self->state_directory, IDENTITY_NAME, self->identity_certificate_path, self->identity_key_path);
}

static bool waitForConnection(CanHubSession *self, int32_t timeout_ms)
{
    uint64_t deadline = deadlineFrom(effectiveTimeout(timeout_ms));

    while (Client_State(&self->client) == kCLIENT_DISCONNECTED && !self->disconnected) {
        if (remainingMs(deadline) == 0) {
            return false;
        }
        pumpOnce(self, remainingMs(deadline));
    }

    return !self->disconnected;
}

static void pumpOnce(CanHubSession *self, int32_t timeout_ms)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int32_t event_count;
    int32_t i;

    syncStreamSlot(self);
    event_count = EpollRegistry_Wait(&self->poll_registry, events, MAX_EPOLL_EVENTS, timeout_ms);
    for(i=0; i<event_count; i++) {
        dispatchEvent(self, &events[i]);
    }
}

static void dispatchEvent(CanHubSession *self, const struct epoll_event *event)
{
    uint32_t tag = event->data.u32;
    bool writable = (event->events & EPOLLOUT) != 0;
    bool readable = (event->events & EPOLLIN) != 0;

    if (tag == TAG_QUIC_UDP) {
        QuicClientTransport_OnUdpReadable(&self->quic_transport);
        return;
    }
    if (tag == TAG_QUIC_TIMER) {
        QuicClientTransport_OnTimer(&self->quic_transport);
        return;
    }
    if (tag != TAG_STREAM) {
        return;
    }

    if (self->scheme == kCONNECT_SCHEME_TLS) {
        if (writable) {
            TlsClientTransport_OnWritable(&self->tls_transport);
        }
        if (readable) {
            TlsClientTransport_OnReadable(&self->tls_transport);
        }
        return;
    }
    if (writable) {
        TcpClientTransport_OnWritable(&self->tcp_transport);
    }
    if (readable) {
        TcpClientTransport_OnReadable(&self->tcp_transport);
    }
}

static void syncStreamSlot(CanHubSession *self)
{
    int32_t current_fd;
    uint32_t wanted_mask = EPOLLIN;

    if (self->scheme == kCONNECT_SCHEME_QUIC) {
        return;
    }
    if (self->scheme == kCONNECT_SCHEME_TLS) {
        current_fd = TlsClientTransport_Fd(&self->tls_transport);
        if (TlsClientTransport_WantsWritable(&self->tls_transport)) {
            wanted_mask |= EPOLLOUT;
        }
    } else {
        current_fd = TcpClientTransport_Fd(&self->tcp_transport);
        if (TcpClientTransport_WantsWritable(&self->tcp_transport)) {
            wanted_mask |= EPOLLOUT;
        }
    }
    EpollRegistry_SyncSlot(&self->poll_registry, STREAM_SLOT, current_fd, wanted_mask, TAG_STREAM);
}

static uint64_t deadlineFrom(int32_t timeout_ms)
{
    if (timeout_ms < 0) {
        return UINT64_MAX;
    }

    return Clock_MonotonicNs() / 1000 + (uint64_t)timeout_ms * 1000;
}

static int32_t remainingMs(uint64_t deadline_us)
{
    uint64_t now_us;

    if (deadline_us == UINT64_MAX) {
        return PUMP_SLICE_MS;
    }

    now_us = Clock_MonotonicNs() / 1000;
    if (now_us >= deadline_us) {
        return 0;
    }

    return (int32_t)((deadline_us - now_us) / 1000) + 1;
}

static int32_t effectiveTimeout(int32_t timeout_ms)
{
    return (timeout_ms <= 0) ? DEFAULT_OPERATION_TIMEOUT_MS : timeout_ms;
}

static int32_t openResultFromStatus(uint8_t status)
{
    if (status == OPEN_STATUS_OK) {
        return CANHUB_OK;
    }
    if (status == OPEN_STATUS_WRITE_DENIED) {
        return CANHUB_ERR_WRITE_DENIED;
    }
    if (status == OPEN_STATUS_READ_DENIED) {
        return CANHUB_ERR_READ_DENIED;
    }

    return CANHUB_ERR_OPEN_REJECTED;
}

static void setError(CanHubSession *self, const char *message)
{
    snprintf(self->last_error, sizeof(self->last_error), "%s", message);
}

static void pushFrame(CanHubSession *self, const FrameMessage *message)
{
    CanHubFrame *slot;
    uint16_t tail;

    if (self->ring_count == FRAME_RING_SIZE) {
        self->ring_head = (uint16_t)((self->ring_head + 1) % FRAME_RING_SIZE);
        self->ring_count--;
    }

    tail = (uint16_t)((self->ring_head + self->ring_count) % FRAME_RING_SIZE);
    slot = &self->frame_ring[tail];
    memset(slot, 0, sizeof(*slot));
    slot->timestamp_us = message->timestamp_us;
    slot->can_id = message->can_id;
    slot->flags = message->frame_flags;
    slot->length = message->payload_length;
    memcpy(slot->payload, message->payload, message->payload_length);
    self->ring_count++;
}

static bool popFrame(CanHubSession *self, CanHubFrame *frame)
{
    if (self->ring_count == 0) {
        return false;
    }

    *frame = self->frame_ring[self->ring_head];
    self->ring_head = (uint16_t)((self->ring_head + 1) % FRAME_RING_SIZE);
    self->ring_count--;

    return true;
}

static void onListReply(void *context, const ListReplyMessage *reply)
{
    CanHubSession *self = context;
    CanHubInterfaceInfo *out;
    uint8_t i;

    if (self->list_out == NULL) {
        return;
    }

    for(i=0; i<reply->count; i++) {
        if (self->list_count == self->list_max) {
            self->list_truncated = true;
            break;
        }
        out = &self->list_out[self->list_count];
        out->interface_id = reply->entries[i].interface_id;
        snprintf(out->agent, sizeof(out->agent), "%s", reply->entries[i].agent_name);
        snprintf(out->interface, sizeof(out->interface), "%s", reply->entries[i].interface_name);
        self->list_count++;
    }

    if (self->list_truncated || (reply->flags & LIST_REPLY_FLAG_MORE) == 0 || reply->count == 0) {
        self->list_done = true;
        return;
    }

    Client_RequestList(&self->client, (uint16_t)self->list_count);
}

static void onOpenResult(void *context, uint8_t status, uint8_t channel, uint32_t interface_id)
{
    CanHubSession *self = context;

    (void)channel;
    (void)interface_id;

    self->open_done = true;
    self->open_result = openResultFromStatus(status);
    if (self->open_result != CANHUB_OK) {
        setError(self, "open refused by the hub");
    }
}

static void onFrame(void *context, const FrameMessage *message)
{
    CanHubSession *self = context;

    pushFrame(self, message);
}

static void onError(void *context, uint16_t code, const char *detail)
{
    CanHubSession *self = context;
    char detail_text[ERROR_DETAIL_SIZE + 1];

    snprintf(detail_text, sizeof(detail_text), "%.*s", (int)ERROR_DETAIL_SIZE, detail);

    if (code == CLIENT_ERROR_INTERFACE_NOT_FOUND) {
        self->hub_error = CANHUB_ERR_NOT_FOUND;
        snprintf(self->last_error, sizeof(self->last_error), "interface %s not found", detail_text);
        return;
    }

    self->hub_error = CANHUB_ERR_HUB;
    snprintf(self->last_error, sizeof(self->last_error), "hub error %u: %s", code, detail_text);
}

static void onDisconnected(void *context)
{
    CanHubSession *self = context;

    self->disconnected = true;
}
