#include "platform/linux/tcp/tcp_channel.h"

#include <errno.h>
#include <string.h>

#include <sys/socket.h>

#define READ_CHUNK_SIZE 2048

/* ---------- public ---------- */

void TcpChannel_Bind(TcpChannel *self, int32_t fd)
{
    self->fd = fd;
    self->tx_used = 0;
    MessageFramer_Reset(&self->framer);
}

void TcpChannel_Unbind(TcpChannel *self)
{
    self->fd = TCP_CHANNEL_NO_SOCKET;
    self->tx_used = 0;
    MessageFramer_Reset(&self->framer);
}

bool TcpChannel_IsBound(const TcpChannel *self)
{
    return self->fd != TCP_CHANNEL_NO_SOCKET;
}

size_t TcpChannel_FreeTxSpace(const TcpChannel *self)
{
    return TCP_CHANNEL_TX_BACKLOG_SIZE - self->tx_used;
}

bool TcpChannel_Queue(TcpChannel *self, const uint8_t *data, size_t size)
{
    if (self->tx_used + size > TCP_CHANNEL_TX_BACKLOG_SIZE) {
        return false;
    }

    memcpy(self->tx_backlog + self->tx_used, data, size);
    self->tx_used += size;

    return true;
}

bool TcpChannel_Flush(TcpChannel *self)
{
    ssize_t bytes_sent;

    while (self->tx_used > 0) {
        bytes_sent = send(self->fd, self->tx_backlog, self->tx_used, MSG_NOSIGNAL);
        if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        if (bytes_sent <= 0) {
            return false;
        }

        memmove(self->tx_backlog, self->tx_backlog + bytes_sent, self->tx_used - (size_t)bytes_sent);
        self->tx_used -= (size_t)bytes_sent;
    }

    return true;
}

bool TcpChannel_HasPendingTx(const TcpChannel *self)
{
    return self->tx_used > 0;
}

bool TcpChannel_Receive(TcpChannel *self, const MessageSink *sink)
{
    uint8_t chunk[READ_CHUNK_SIZE];
    ssize_t bytes_received;
    size_t offset;
    size_t taken;

    for (;;) {
        bytes_received = recv(self->fd, chunk, sizeof(chunk), 0);
        if (bytes_received == 0 || (bytes_received < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            return false;
        }
        if (bytes_received < 0) {
            return true;
        }

        offset = 0;
        while (offset < (size_t)bytes_received) {
            taken = MessageFramer_Push(&self->framer, chunk + offset, (size_t)bytes_received - offset);
            offset += taken;
            MessageFramer_Drain(&self->framer, sink);
            if (!TcpChannel_IsBound(self)) {
                return true;
            }
            if (taken == 0) {
                return false;
            }
        }
    }
}
