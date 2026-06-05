/*
 * E0 spike — CAN over QUIC datagrams (RFC 9221) with ngtcp2 0.12 + GnuTLS.
 *
 * Tunnels frames between a SocketCAN interface and a peer: every CAN frame
 * read from the interface is sent as one QUIC datagram; every received
 * datagram is injected into the interface.
 *
 * Throwaway spike code. Known shortcuts: single client, no certificate
 * verification, frames dropped until the handshake completes or when
 * congestion-blocked (counted), no reconnect.
 *
 *   server: ./tunnel server <port> <can-if> <cert.pem> <key.pem>
 *   client: ./tunnel client <host> <port> <can-if>
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/timerfd.h>

#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_gnutls.h>

#define UDP_BUFFER_SIZE 1452
#define CONNECTION_ID_LENGTH 16
#define MAX_EPOLL_EVENTS 8
#define IDLE_TIMEOUT (30 * NGTCP2_SECONDS)
#define MAX_DATAGRAM_FRAME_SIZE 1350
#define SERVER_NAME "localhost"

#define TLS_PRIORITY \
    "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CIPHER-ALL:+AES-128-GCM:+AES-256-GCM:" \
    "+CHACHA20-POLY1305:%DISABLE_TLS13_COMPAT_MODE"

typedef struct {
    bool is_server;
    int udp_fd;
    int can_fd;
    int timer_fd;
    int epoll_fd;
    struct sockaddr_storage local_address;
    socklen_t local_address_length;
    struct sockaddr_storage remote_address;
    socklen_t remote_address_length;
    ngtcp2_conn *connection;
    gnutls_session_t session;
    gnutls_certificate_credentials_t credentials;
    ngtcp2_crypto_conn_ref connection_ref;
    const char *certificate_file;
    const char *key_file;
    uint64_t next_datagram_id;
    uint64_t tx_count;
    uint64_t rx_count;
    uint64_t drop_count;
    bool handshake_done;
} Tunnel;

static Tunnel tunnel;

static uint64_t timestampNs(void);
static int openCanSocket(const char *interface_name);
static int openUdpClient(Tunnel *self, const char *host, const char *port);
static int openUdpServer(Tunnel *self, const char *port);
static int tlsInit(Tunnel *self);
static void defaultTransportParams(ngtcp2_transport_params *params);
static int connectionInitClient(Tunnel *self);
static int connectionInitServer(Tunnel *self, const ngtcp2_pkt_hd *header);
static void makePath(Tunnel *self, ngtcp2_path *path);
static int flushEgress(Tunnel *self, const struct can_frame *frame);
static void updateTimer(Tunnel *self);
static int handleUdpInput(Tunnel *self);
static int handleCanInput(Tunnel *self);
static int handleTimer(Tunnel *self);
static void printStatsAndExit(int signal_number);
static ngtcp2_conn *getConnection(ngtcp2_crypto_conn_ref *connection_ref);
static void randCallback(uint8_t *destination, size_t destination_length, const ngtcp2_rand_ctx *rand_context);
static int getNewConnectionIdCallback(
    ngtcp2_conn *connection,
    ngtcp2_cid *cid,
    uint8_t *token,
    size_t cid_length,
    void *user_data
);
static int handshakeCompletedCallback(ngtcp2_conn *connection, void *user_data);
static int receiveDatagramCallback(
    ngtcp2_conn *connection,
    uint32_t flags,
    const uint8_t *data,
    size_t data_length,
    void *user_data
);

int main(int argc, char **argv)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    struct epoll_event event;
    const char *can_interface;
    int event_count;
    int event_fd;
    int result;
    int i;

    if (argc == 6 && strcmp(argv[1], "server") == 0) {
        tunnel.is_server = true;
        can_interface = argv[3];
        tunnel.certificate_file = argv[4];
        tunnel.key_file = argv[5];
        if (openUdpServer(&tunnel, argv[2]) != 0) {
            return 1;
        }
    } else if (argc == 5 && strcmp(argv[1], "client") == 0) {
        tunnel.is_server = false;
        can_interface = argv[4];
        if (openUdpClient(&tunnel, argv[2], argv[3]) != 0) {
            return 1;
        }
    } else {
        fprintf(stderr, "usage: %s server <port> <can-if> <cert.pem> <key.pem>\n", argv[0]);
        fprintf(stderr, "       %s client <host> <port> <can-if>\n", argv[0]);
        return 1;
    }

    tunnel.can_fd = openCanSocket(can_interface);
    if (tunnel.can_fd < 0) {
        return 1;
    }

    tunnel.timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    tunnel.epoll_fd = epoll_create1(0);
    if (tunnel.timer_fd < 0 || tunnel.epoll_fd < 0) {
        fprintf(stderr, "timerfd/epoll: %s\n", strerror(errno));
        return 1;
    }

    event.events = EPOLLIN;
    event.data.fd = tunnel.udp_fd;
    epoll_ctl(tunnel.epoll_fd, EPOLL_CTL_ADD, tunnel.udp_fd, &event);
    event.data.fd = tunnel.can_fd;
    epoll_ctl(tunnel.epoll_fd, EPOLL_CTL_ADD, tunnel.can_fd, &event);
    event.data.fd = tunnel.timer_fd;
    epoll_ctl(tunnel.epoll_fd, EPOLL_CTL_ADD, tunnel.timer_fd, &event);

    tunnel.connection_ref.get_conn = getConnection;
    tunnel.connection_ref.user_data = &tunnel;

    if (!tunnel.is_server) {
        if (tlsInit(&tunnel) != 0 || connectionInitClient(&tunnel) != 0) {
            return 1;
        }
        if (flushEgress(&tunnel, NULL) != 0) {
            return 1;
        }
    }

    signal(SIGINT, printStatsAndExit);
    signal(SIGTERM, printStatsAndExit);

    for (;;) {
        event_count = epoll_wait(tunnel.epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if (event_count < 0 && errno == EINTR) {
            continue;
        }
        for(i=0; i<event_count; i++) {
            event_fd = events[i].data.fd;
            result = 0;

            if (event_fd == tunnel.udp_fd) {
                result = handleUdpInput(&tunnel);
            } else if (event_fd == tunnel.can_fd) {
                result = handleCanInput(&tunnel);
            } else if (event_fd == tunnel.timer_fd) {
                result = handleTimer(&tunnel);
            }
            if (result != 0) {
                fprintf(stderr, "fatal event loop error\n");
                return 1;
            }
        }
    }
}

static uint64_t timestampNs(void)
{
    struct timespec time_point;

    clock_gettime(CLOCK_MONOTONIC, &time_point);

    return (uint64_t)time_point.tv_sec * NGTCP2_SECONDS + (uint64_t)time_point.tv_nsec;
}

static int openCanSocket(const char *interface_name)
{
    struct sockaddr_can address;
    struct ifreq interface_request;
    int can_fd;

    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_fd < 0) {
        fprintf(stderr, "CAN socket: %s\n", strerror(errno));
        return -1;
    }

    snprintf(interface_request.ifr_name, IFNAMSIZ, "%s", interface_name);
    if (ioctl(can_fd, SIOCGIFINDEX, &interface_request) < 0) {
        fprintf(stderr, "SIOCGIFINDEX %s: %s\n", interface_name, strerror(errno));
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.can_family = AF_CAN;
    address.can_ifindex = interface_request.ifr_ifindex;
    if (bind(can_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "CAN bind: %s\n", strerror(errno));
        return -1;
    }

    return can_fd;
}

static int openUdpClient(Tunnel *self, const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *resolved;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &resolved) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        return -1;
    }

    self->udp_fd = socket(resolved->ai_family, resolved->ai_socktype, 0);
    if (self->udp_fd < 0 || connect(self->udp_fd, resolved->ai_addr, resolved->ai_addrlen) < 0) {
        fprintf(stderr, "UDP connect: %s\n", strerror(errno));
        return -1;
    }

    memcpy(&self->remote_address, resolved->ai_addr, resolved->ai_addrlen);
    self->remote_address_length = resolved->ai_addrlen;
    freeaddrinfo(resolved);

    self->local_address_length = sizeof(self->local_address);
    getsockname(self->udp_fd, (struct sockaddr *)&self->local_address, &self->local_address_length);

    return 0;
}

static int openUdpServer(Tunnel *self, const char *port)
{
    struct sockaddr_in address;

    self->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (self->udp_fd < 0) {
        fprintf(stderr, "UDP socket: %s\n", strerror(errno));
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)atoi(port));
    if (bind(self->udp_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "UDP bind: %s\n", strerror(errno));
        return -1;
    }

    self->local_address_length = sizeof(self->local_address);
    getsockname(self->udp_fd, (struct sockaddr *)&self->local_address, &self->local_address_length);

    return 0;
}

static int tlsInit(Tunnel *self)
{
    static const gnutls_datum_t alpn = { (unsigned char *)"e0", 2 };
    unsigned int session_flags = self->is_server ? GNUTLS_SERVER : GNUTLS_CLIENT;
    int result;

    gnutls_certificate_allocate_credentials(&self->credentials);
    if (self->is_server) {
        result = gnutls_certificate_set_x509_key_file(
            self->credentials,
            self->certificate_file,
            self->key_file,
            GNUTLS_X509_FMT_PEM
        );
        if (result != 0) {
            fprintf(stderr, "load cert/key: %s\n", gnutls_strerror(result));
            return -1;
        }
    }

    gnutls_init(&self->session, session_flags);
    gnutls_priority_set_direct(self->session, TLS_PRIORITY, NULL);
    gnutls_credentials_set(self->session, GNUTLS_CRD_CERTIFICATE, self->credentials);
    gnutls_alpn_set_protocols(self->session, &alpn, 1, GNUTLS_ALPN_MANDATORY);

    if (self->is_server) {
        result = ngtcp2_crypto_gnutls_configure_server_session(self->session);
    } else {
        gnutls_server_name_set(self->session, GNUTLS_NAME_DNS, SERVER_NAME, strlen(SERVER_NAME));
        result = ngtcp2_crypto_gnutls_configure_client_session(self->session);
    }
    if (result != 0) {
        fprintf(stderr, "ngtcp2 gnutls session setup failed\n");
        return -1;
    }

    gnutls_session_set_ptr(self->session, &self->connection_ref);

    return 0;
}

static void defaultTransportParams(ngtcp2_transport_params *params)
{
    ngtcp2_transport_params_default(params);
    params->initial_max_data = 1024 * 1024;
    params->initial_max_stream_data_bidi_local = 64 * 1024;
    params->initial_max_stream_data_bidi_remote = 64 * 1024;
    params->max_idle_timeout = IDLE_TIMEOUT;
    params->max_datagram_frame_size = MAX_DATAGRAM_FRAME_SIZE;
}

static int connectionInitClient(Tunnel *self)
{
    ngtcp2_callbacks callbacks = {
        .client_initial = ngtcp2_crypto_client_initial_cb,
        .recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb,
        .encrypt = ngtcp2_crypto_encrypt_cb,
        .decrypt = ngtcp2_crypto_decrypt_cb,
        .hp_mask = ngtcp2_crypto_hp_mask_cb,
        .recv_retry = ngtcp2_crypto_recv_retry_cb,
        .rand = randCallback,
        .get_new_connection_id = getNewConnectionIdCallback,
        .update_key = ngtcp2_crypto_update_key_cb,
        .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
        .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
        .get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb,
        .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
        .recv_datagram = receiveDatagramCallback,
        .handshake_completed = handshakeCompletedCallback,
    };
    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    ngtcp2_cid destination_cid;
    ngtcp2_cid source_cid;
    ngtcp2_path path;
    int result;

    gnutls_rnd(GNUTLS_RND_RANDOM, destination_cid.data, CONNECTION_ID_LENGTH);
    destination_cid.datalen = CONNECTION_ID_LENGTH;
    gnutls_rnd(GNUTLS_RND_RANDOM, source_cid.data, CONNECTION_ID_LENGTH);
    source_cid.datalen = CONNECTION_ID_LENGTH;

    ngtcp2_settings_default(&settings);
    settings.initial_ts = timestampNs();
    defaultTransportParams(&params);
    makePath(self, &path);

    result = ngtcp2_conn_client_new(
        &self->connection,
        &destination_cid,
        &source_cid,
        &path,
        NGTCP2_PROTO_VER_V1,
        &callbacks,
        &settings,
        &params,
        NULL,
        self
    );
    if (result != 0) {
        fprintf(stderr, "ngtcp2_conn_client_new: %s\n", ngtcp2_strerror(result));
        return -1;
    }

    ngtcp2_conn_set_tls_native_handle(self->connection, self->session);

    return 0;
}

static int connectionInitServer(Tunnel *self, const ngtcp2_pkt_hd *header)
{
    ngtcp2_callbacks callbacks = {
        .recv_client_initial = ngtcp2_crypto_recv_client_initial_cb,
        .recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb,
        .encrypt = ngtcp2_crypto_encrypt_cb,
        .decrypt = ngtcp2_crypto_decrypt_cb,
        .hp_mask = ngtcp2_crypto_hp_mask_cb,
        .rand = randCallback,
        .get_new_connection_id = getNewConnectionIdCallback,
        .update_key = ngtcp2_crypto_update_key_cb,
        .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
        .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
        .get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb,
        .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
        .recv_datagram = receiveDatagramCallback,
        .handshake_completed = handshakeCompletedCallback,
    };
    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    ngtcp2_cid source_cid;
    ngtcp2_path path;
    int result;

    if (tlsInit(self) != 0) {
        return -1;
    }

    gnutls_rnd(GNUTLS_RND_RANDOM, source_cid.data, CONNECTION_ID_LENGTH);
    source_cid.datalen = CONNECTION_ID_LENGTH;

    ngtcp2_settings_default(&settings);
    settings.initial_ts = timestampNs();
    defaultTransportParams(&params);
    params.original_dcid = header->dcid;
    params.original_dcid_present = 1;
    makePath(self, &path);

    result = ngtcp2_conn_server_new(
        &self->connection,
        &header->scid,
        &source_cid,
        &path,
        header->version,
        &callbacks,
        &settings,
        &params,
        NULL,
        self
    );
    if (result != 0) {
        fprintf(stderr, "ngtcp2_conn_server_new: %s\n", ngtcp2_strerror(result));
        return -1;
    }

    ngtcp2_conn_set_tls_native_handle(self->connection, self->session);

    return 0;
}

static void makePath(Tunnel *self, ngtcp2_path *path)
{
    path->local.addr = (ngtcp2_sockaddr *)&self->local_address;
    path->local.addrlen = self->local_address_length;
    path->remote.addr = (ngtcp2_sockaddr *)&self->remote_address;
    path->remote.addrlen = self->remote_address_length;
    path->user_data = NULL;
}

static int flushEgress(Tunnel *self, const struct can_frame *frame)
{
    uint8_t buffer[UDP_BUFFER_SIZE];
    ngtcp2_path_storage path_storage;
    ngtcp2_pkt_info packet_info;
    ngtcp2_ssize bytes_written;
    int accepted = 0;

    ngtcp2_path_storage_zero(&path_storage);

    if (frame != NULL) {
        ngtcp2_vec data_vector = { (uint8_t *)frame, sizeof(*frame) };

        bytes_written = ngtcp2_conn_writev_datagram(
            self->connection,
            &path_storage.path,
            &packet_info,
            buffer,
            sizeof(buffer),
            &accepted,
            NGTCP2_WRITE_DATAGRAM_FLAG_NONE,
            ++self->next_datagram_id,
            &data_vector,
            1,
            timestampNs()
        );
        if (bytes_written < 0) {
            fprintf(stderr, "writev_datagram: %s\n", ngtcp2_strerror((int)bytes_written));
            return -1;
        }
        if (bytes_written > 0) {
            send(self->udp_fd, buffer, (size_t)bytes_written, 0);
        }
        if (accepted) {
            self->tx_count++;
        } else {
            self->drop_count++;
        }
    }

    for (;;) {
        bytes_written = ngtcp2_conn_write_pkt(
            self->connection,
            &path_storage.path,
            &packet_info,
            buffer,
            sizeof(buffer),
            timestampNs()
        );
        if (bytes_written < 0) {
            fprintf(stderr, "write_pkt: %s\n", ngtcp2_strerror((int)bytes_written));
            return -1;
        }
        if (bytes_written == 0) {
            break;
        }
        send(self->udp_fd, buffer, (size_t)bytes_written, 0);
    }

    updateTimer(self);

    return 0;
}

static void updateTimer(Tunnel *self)
{
    ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry(self->connection);
    struct itimerspec timer_specification;

    memset(&timer_specification, 0, sizeof(timer_specification));
    if (expiry == UINT64_MAX) {
        timerfd_settime(self->timer_fd, 0, &timer_specification, NULL);
        return;
    }
    if (expiry <= timestampNs()) {
        expiry = timestampNs() + 1;
    }

    timer_specification.it_value.tv_sec = (time_t)(expiry / NGTCP2_SECONDS);
    timer_specification.it_value.tv_nsec = (long)(expiry % NGTCP2_SECONDS);
    timerfd_settime(self->timer_fd, TFD_TIMER_ABSTIME, &timer_specification, NULL);
}

static int handleUdpInput(Tunnel *self)
{
    uint8_t buffer[UDP_BUFFER_SIZE];
    struct sockaddr_storage peer_address;
    socklen_t peer_address_length = sizeof(peer_address);
    ngtcp2_pkt_info packet_info = { 0 };
    ngtcp2_path path;
    ssize_t bytes_received;
    int result;

    bytes_received = recvfrom(
        self->udp_fd,
        buffer,
        sizeof(buffer),
        0,
        (struct sockaddr *)&peer_address,
        &peer_address_length
    );
    if (bytes_received <= 0) {
        return 0;
    }
    if (self->connection == NULL) {
        ngtcp2_pkt_hd header;

        if (!self->is_server) {
            return 0;
        }
        if (ngtcp2_accept(&header, buffer, (size_t)bytes_received) != 0) {
            return 0;
        }
        memcpy(&self->remote_address, &peer_address, peer_address_length);
        self->remote_address_length = peer_address_length;
        connect(self->udp_fd, (struct sockaddr *)&peer_address, peer_address_length);
        if (connectionInitServer(self, &header) != 0) {
            return -1;
        }
    }

    makePath(self, &path);
    result = ngtcp2_conn_read_pkt(
        self->connection,
        &path,
        &packet_info,
        buffer,
        (size_t)bytes_received,
        timestampNs()
    );
    if (result != 0) {
        fprintf(stderr, "read_pkt: %s\n", ngtcp2_strerror(result));
        return (result == NGTCP2_ERR_DRAINING || result == NGTCP2_ERR_DROP_CONN) ? -1 : 0;
    }

    return flushEgress(self, NULL);
}

static int handleCanInput(Tunnel *self)
{
    struct can_frame frame;
    ssize_t bytes_received;

    bytes_received = read(self->can_fd, &frame, sizeof(frame));
    if (bytes_received != (ssize_t)sizeof(frame)) {
        return 0;
    }
    if (self->connection == NULL || !self->handshake_done) {
        self->drop_count++;
        return 0;
    }

    return flushEgress(self, &frame);
}

static int handleTimer(Tunnel *self)
{
    uint64_t expiration_count;
    int result;

    if (read(self->timer_fd, &expiration_count, sizeof(expiration_count)) < 0) {
        return 0;
    }
    if (self->connection == NULL) {
        return 0;
    }

    result = ngtcp2_conn_handle_expiry(self->connection, timestampNs());
    if (result != 0) {
        fprintf(stderr, "handle_expiry: %s\n", ngtcp2_strerror(result));
        return -1;
    }

    return flushEgress(self, NULL);
}

static void printStatsAndExit(int signal_number)
{
    (void)signal_number;

    fprintf(
        stderr,
        "[%s] tx=%llu rx=%llu dropped=%llu\n",
        tunnel.is_server ? "server" : "client",
        (unsigned long long)tunnel.tx_count,
        (unsigned long long)tunnel.rx_count,
        (unsigned long long)tunnel.drop_count
    );
    _exit(0);
}

static ngtcp2_conn *getConnection(ngtcp2_crypto_conn_ref *connection_ref)
{
    Tunnel *self = connection_ref->user_data;

    return self->connection;
}

static void randCallback(uint8_t *destination, size_t destination_length, const ngtcp2_rand_ctx *rand_context)
{
    (void)rand_context;

    gnutls_rnd(GNUTLS_RND_RANDOM, destination, destination_length);
}

static int getNewConnectionIdCallback(
    ngtcp2_conn *connection,
    ngtcp2_cid *cid,
    uint8_t *token,
    size_t cid_length,
    void *user_data
)
{
    (void)connection;
    (void)user_data;

    if (gnutls_rnd(GNUTLS_RND_RANDOM, cid->data, cid_length) != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    cid->datalen = cid_length;

    if (gnutls_rnd(GNUTLS_RND_RANDOM, token, NGTCP2_STATELESS_RESET_TOKENLEN) != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    return 0;
}

static int handshakeCompletedCallback(ngtcp2_conn *connection, void *user_data)
{
    Tunnel *self = user_data;

    (void)connection;

    self->handshake_done = true;
    fprintf(stderr, "[%s] handshake completed\n", self->is_server ? "server" : "client");

    return 0;
}

static int receiveDatagramCallback(
    ngtcp2_conn *connection,
    uint32_t flags,
    const uint8_t *data,
    size_t data_length,
    void *user_data
)
{
    Tunnel *self = user_data;

    (void)connection;
    (void)flags;

    if (data_length != sizeof(struct can_frame)) {
        return 0;
    }
    if (write(self->can_fd, data, data_length) != (ssize_t)data_length) {
        fprintf(stderr, "CAN write: %s\n", strerror(errno));
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    self->rx_count++;

    return 0;
}
