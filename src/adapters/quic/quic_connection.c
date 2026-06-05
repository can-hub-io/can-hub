#include "adapters/quic/quic_connection.h"

#include <string.h>
#include <time.h>

#include <gnutls/crypto.h>

#define CONNECTION_ID_LENGTH 16
#define NO_STREAM (-1)
#define MAX_DATAGRAM_FRAME_SIZE 1350
#define IDLE_TIMEOUT (30 * NGTCP2_SECONDS)
#define INITIAL_MAX_DATA (1024 * 1024)
#define INITIAL_MAX_STREAM_DATA (64 * 1024)

static uint64_t timestampNs(void);
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
static int receiveStreamDataCallback(
    ngtcp2_conn *connection,
    uint32_t flags,
    int64_t stream_id,
    uint64_t stream_offset,
    const uint8_t *data,
    size_t data_length,
    void *user_data,
    void *stream_user_data
);
static int ackedStreamDataOffsetCallback(
    ngtcp2_conn *connection,
    int64_t stream_id,
    uint64_t stream_offset,
    uint64_t data_length,
    void *user_data,
    void *stream_user_data
);

/* ---------- public ---------- */

void QuicConnection_Bind(QuicConnection *self, const QuicConnectionEvents *events)
{
    memset(self, 0, sizeof(*self));
    self->events = *events;
    self->connection_ref.get_conn = getConnection;
    self->connection_ref.user_data = self;
}

ngtcp2_crypto_conn_ref *QuicConnection_Ref(QuicConnection *self)
{
    return &self->connection_ref;
}

bool QuicConnection_Open(QuicConnection *self, gnutls_session_t session, const ngtcp2_path *path)
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
        .handshake_completed = handshakeCompletedCallback,
        .recv_datagram = receiveDatagramCallback,
        .recv_stream_data = receiveStreamDataCallback,
        .acked_stream_data_offset = ackedStreamDataOffsetCallback,
    };
    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    ngtcp2_cid destination_cid;
    ngtcp2_cid source_cid;
    int result;

    gnutls_rnd(GNUTLS_RND_RANDOM, destination_cid.data, CONNECTION_ID_LENGTH);
    destination_cid.datalen = CONNECTION_ID_LENGTH;
    gnutls_rnd(GNUTLS_RND_RANDOM, source_cid.data, CONNECTION_ID_LENGTH);
    source_cid.datalen = CONNECTION_ID_LENGTH;

    ngtcp2_settings_default(&settings);
    settings.initial_ts = timestampNs();

    ngtcp2_transport_params_default(&params);
    params.initial_max_data = INITIAL_MAX_DATA;
    params.initial_max_stream_data_bidi_local = INITIAL_MAX_STREAM_DATA;
    params.initial_max_stream_data_bidi_remote = INITIAL_MAX_STREAM_DATA;
    params.max_idle_timeout = IDLE_TIMEOUT;
    params.max_datagram_frame_size = MAX_DATAGRAM_FRAME_SIZE;

    result = ngtcp2_conn_client_new(
        &self->connection,
        &destination_cid,
        &source_cid,
        path,
        NGTCP2_PROTO_VER_V1,
        &callbacks,
        &settings,
        &params,
        NULL,
        self
    );
    if (result != 0) {
        return false;
    }

    ngtcp2_conn_set_tls_native_handle(self->connection, session);

    return true;
}

void QuicConnection_Close(QuicConnection *self)
{
    if (self->connection == NULL) {
        return;
    }

    ngtcp2_conn_del(self->connection);
    self->connection = NULL;
}

bool QuicConnection_IsOpen(const QuicConnection *self)
{
    return self->connection != NULL;
}

bool QuicConnection_ReadPacket(QuicConnection *self, const ngtcp2_path *path, const uint8_t *data, size_t size)
{
    ngtcp2_pkt_info packet_info = { 0 };

    return ngtcp2_conn_read_pkt(self->connection, path, &packet_info, data, size, timestampNs()) == 0;
}

bool QuicConnection_HandleExpiry(QuicConnection *self)
{
    return ngtcp2_conn_handle_expiry(self->connection, timestampNs()) == 0;
}

uint64_t QuicConnection_NextExpiryNs(const QuicConnection *self)
{
    uint64_t expiry = ngtcp2_conn_get_expiry(self->connection);

    if (expiry == UINT64_MAX) {
        return UINT64_MAX;
    }
    if (expiry <= timestampNs()) {
        return timestampNs() + 1;
    }

    return expiry;
}

bool QuicConnection_OpenControlStream(QuicConnection *self, int64_t *stream_id)
{
    return ngtcp2_conn_open_bidi_stream(self->connection, stream_id, NULL) == 0;
}

void QuicConnection_ExtendStreamCredit(QuicConnection *self, int64_t stream_id, size_t size)
{
    ngtcp2_conn_extend_max_stream_offset(self->connection, stream_id, size);
    ngtcp2_conn_extend_max_offset(self->connection, size);
}

ngtcp2_ssize QuicConnection_WriteDatagram(
    QuicConnection *self,
    uint8_t *packet_buffer,
    size_t packet_buffer_size,
    const uint8_t *payload,
    size_t payload_size,
    bool *accepted
)
{
    ngtcp2_vec data_vector = { (uint8_t *)payload, payload_size };
    ngtcp2_path_storage path_storage;
    ngtcp2_pkt_info packet_info;
    ngtcp2_ssize bytes_written;
    int accepted_flag = 0;

    ngtcp2_path_storage_zero(&path_storage);
    bytes_written = ngtcp2_conn_writev_datagram(
        self->connection,
        &path_storage.path,
        &packet_info,
        packet_buffer,
        packet_buffer_size,
        &accepted_flag,
        NGTCP2_WRITE_DATAGRAM_FLAG_NONE,
        ++self->next_datagram_id,
        &data_vector,
        1,
        timestampNs()
    );

    *accepted = accepted_flag != 0;

    return bytes_written;
}

ngtcp2_ssize QuicConnection_WriteStream(
    QuicConnection *self,
    uint8_t *packet_buffer,
    size_t packet_buffer_size,
    int64_t stream_id,
    const uint8_t *data,
    size_t data_size,
    size_t *consumed
)
{
    ngtcp2_vec data_vector = { (uint8_t *)data, data_size };
    ngtcp2_path_storage path_storage;
    ngtcp2_pkt_info packet_info;
    ngtcp2_ssize bytes_written;
    ngtcp2_ssize stream_bytes_consumed = 0;

    ngtcp2_path_storage_zero(&path_storage);
    bytes_written = ngtcp2_conn_writev_stream(
        self->connection,
        &path_storage.path,
        &packet_info,
        packet_buffer,
        packet_buffer_size,
        &stream_bytes_consumed,
        NGTCP2_WRITE_STREAM_FLAG_NONE,
        stream_id,
        &data_vector,
        data == NULL ? 0 : 1,
        timestampNs()
    );

    *consumed = stream_bytes_consumed > 0 ? (size_t)stream_bytes_consumed : 0;

    return bytes_written;
}

ngtcp2_ssize QuicConnection_WritePacket(QuicConnection *self, uint8_t *packet_buffer, size_t packet_buffer_size)
{
    size_t ignored;

    return QuicConnection_WriteStream(self, packet_buffer, packet_buffer_size, NO_STREAM, NULL, 0, &ignored);
}

/* ---------- private ---------- */

static uint64_t timestampNs(void)
{
    struct timespec time_point;

    clock_gettime(CLOCK_MONOTONIC, &time_point);

    return (uint64_t)time_point.tv_sec * NGTCP2_SECONDS + (uint64_t)time_point.tv_nsec;
}

static ngtcp2_conn *getConnection(ngtcp2_crypto_conn_ref *connection_ref)
{
    QuicConnection *self = connection_ref->user_data;

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
    QuicConnection *self = user_data;

    (void)connection;

    self->events.on_handshake_completed(self->events.context);

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
    QuicConnection *self = user_data;

    (void)connection;
    (void)flags;

    self->events.on_datagram(self->events.context, data, data_length);

    return 0;
}

static int receiveStreamDataCallback(
    ngtcp2_conn *connection,
    uint32_t flags,
    int64_t stream_id,
    uint64_t stream_offset,
    const uint8_t *data,
    size_t data_length,
    void *user_data,
    void *stream_user_data
)
{
    QuicConnection *self = user_data;

    (void)connection;
    (void)flags;
    (void)stream_offset;
    (void)stream_user_data;

    self->events.on_stream_data(self->events.context, stream_id, data, data_length);

    return 0;
}

static int ackedStreamDataOffsetCallback(
    ngtcp2_conn *connection,
    int64_t stream_id,
    uint64_t stream_offset,
    uint64_t data_length,
    void *user_data,
    void *stream_user_data
)
{
    QuicConnection *self = user_data;

    (void)connection;
    (void)stream_user_data;

    self->events.on_stream_acked(self->events.context, stream_id, stream_offset + data_length);

    return 0;
}
