#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/shared/message_framer.h"

#define TCP_CHANNEL_TX_BACKLOG_SIZE 8192
#define TCP_CHANNEL_NO_SOCKET (-1)

/*
 * One established TCP stream carrying protocol messages: RX reassembly via
 * the framer, TX backlog so partial writes never corrupt the stream. Owns
 * no connection lifecycle — the owner binds an fd and reacts to Receive/
 * Flush reporting the stream dead.
 */
typedef struct {
    int32_t fd;
    MessageFramer framer;
    uint8_t tx_backlog[TCP_CHANNEL_TX_BACKLOG_SIZE];
    size_t tx_used;
} TcpChannel;

void TcpChannel_Bind(TcpChannel *self, int32_t fd);
void TcpChannel_Unbind(TcpChannel *self);
bool TcpChannel_IsBound(const TcpChannel *self);
size_t TcpChannel_FreeTxSpace(const TcpChannel *self);
bool TcpChannel_Queue(TcpChannel *self, const uint8_t *data, size_t size);
bool TcpChannel_Flush(TcpChannel *self);
bool TcpChannel_HasPendingTx(const TcpChannel *self);
bool TcpChannel_Receive(TcpChannel *self);
