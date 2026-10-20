#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/message_header.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"

/*
 * The client side of one transaction (see doc/protocol.md "Transactions"):
 * keeps the step record bytes in flight within the agent's window, packs
 * offered steps into TXN_STEPS batches and holds the last offered step back,
 * so End can flag it last without the caller knowing in advance which step
 * that is. A batch the transport could not take stays pending, in order,
 * until it is sent.
 */

#define TRANSACTION_SENDER_BATCH_SIZE (MESSAGE_HEADER_SIZE + TXN_STEPS_BODY_SIZE_MAX)

typedef enum ttransaction_sender_state_e {
    kTRANSACTION_SENDER_IDLE = 0,
    kTRANSACTION_SENDER_BEGINNING = 1,
    kTRANSACTION_SENDER_RUNNING = 2,
    kTRANSACTION_SENDER_FINISHED = 3,
    kTRANSACTION_SENDER_STATE_MAX,
} TTRANSACTION_SENDER_STATE;

typedef enum ttransaction_offer_e {
    kTRANSACTION_OFFER_ACCEPTED = 0,
    kTRANSACTION_OFFER_FLUSH_NEEDED = 1,
    kTRANSACTION_OFFER_NO_CREDIT = 2,
    kTRANSACTION_OFFER_REJECTED = 3,
    kTRANSACTION_OFFER_MAX,
} TTRANSACTION_OFFER;

typedef struct {
    uint8_t state;
    uint8_t channel;
    uint16_t txn_id;
    uint8_t begin_status;
    bool ended;
    uint32_t window_bytes;
    uint32_t sent_bytes;
    uint32_t consumed_bytes;
    uint32_t completed_steps;
    uint32_t next_seq;
    bool has_held;
    TxnStep held;
    bool batch_open;
    TxnStepsWriter writer;
    uint8_t batch[TRANSACTION_SENDER_BATCH_SIZE];
    TxnStatusMessage outcome;
} TransactionSender;

void TransactionSender_Reset(TransactionSender *self);
void TransactionSender_Begin(TransactionSender *self, uint8_t channel);
void TransactionSender_OnBeginAck(TransactionSender *self, const TxnBeginAckMessage *ack);
void TransactionSender_OnProgress(TransactionSender *self, const TxnProgressMessage *progress);
void TransactionSender_OnStatus(TransactionSender *self, const TxnStatusMessage *status);
TTRANSACTION_OFFER TransactionSender_Offer(TransactionSender *self, const TxnStep *step);
TTRANSACTION_OFFER TransactionSender_End(TransactionSender *self);
size_t TransactionSender_PendingBatch(TransactionSender *self, const uint8_t **data);
void TransactionSender_BatchSent(TransactionSender *self);
uint32_t TransactionSender_BytesInFlight(const TransactionSender *self);
