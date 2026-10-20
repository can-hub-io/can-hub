#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "agent/domain/isotp_link.h"
#include "agent/domain/step_ring.h"
#include "protocol/frame_message.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"

/*
 * Executes one transaction at the bus (see doc/protocol.md "Transactions").
 * Frames are matched as they arrive (OnFrame) but the sequence only moves on
 * Tick, which carries the time: the platform ticks right after every batch of
 * events, so a matched answer costs no extra latency, and NextWakeUs tells it
 * when the runner needs a tick without one.
 */

#define TRANSACTION_STARVATION_TIMEOUT_US 5000000
#define TRANSACTION_TX_RETRY_US 1000
#define TRANSACTION_DEFAULT_TX_TIMEOUT_MS 1000
#define TRANSACTION_PROGRESS_FLUSH_US 10000
#define TRANSACTION_STEPS_PER_TICK_MAX 64
#define TRANSACTION_EARLY_FRAMES_MAX 16

typedef enum ttransaction_phase_e {
    kTRANSACTION_PHASE_IDLE = 0,
    kTRANSACTION_PHASE_TRANSMIT = 1,
    kTRANSACTION_PHASE_WAIT = 2,
    kTRANSACTION_PHASE_MAX,
} TTRANSACTION_PHASE;

typedef struct {
    void *context;
    bool (*transmit)(void *context, uint8_t interface_index, const FrameMessage *frame);
    void (*report_progress)(void *context, const TxnProgressMessage *progress);
    void (*report_status)(void *context, const TxnStatusMessage *status);
} TransactionRunnerPort;

typedef struct {
    TxnStep step;
    uint32_t record_size;
    uint8_t phase;
    bool satisfied;
    bool extend_requested;
    bool message_started;
    uint64_t deadline_us;
    uint64_t retry_at_us;
    uint8_t match_count;
    uint8_t seen_count;
    uint64_t seen_bitmap;
    bool has_rx_frame;
    TxnRxFrame rx_frame;
} ActiveStep;

typedef struct {
    TransactionRunnerPort port;
    StepRing ring;
    TxnTemplateMessage templates[TXN_TEMPLATES_MAX];
    uint16_t defined_templates;
    TxnOriginExtractor origin;
    uint32_t roster[TXN_ROSTER_MAX];
    uint8_t roster_count;
    bool active;
    uint16_t txn_id;
    uint8_t channel;
    uint8_t interface_index;
    uint32_t window_bytes;
    uint32_t next_seq;
    uint32_t completed_steps;
    uint32_t pushed_bytes;
    uint32_t consumed_bytes;
    uint32_t reported_bytes;
    uint64_t report_due_us;
    uint64_t starve_deadline_us;
    uint64_t next_transmit_us;
    uint64_t last_tick_us;
    uint64_t idle_timeout_us;
    uint64_t last_frame_us;
    TxnKeepalive keepalive;
    ActiveStep current;
    IsotpLink isotp;
    uint8_t message[TXN_ISOTP_MESSAGE_SIZE_MAX];
    uint16_t message_length;
    FrameMessage early_frames[TRANSACTION_EARLY_FRAMES_MAX];
    uint8_t early_frame_count;
} TransactionRunner;

void TransactionRunner_Init(TransactionRunner *self, TransactionRunnerPort port, uint8_t *ring_storage, uint32_t ring_capacity);
void TransactionRunner_Begin(
    TransactionRunner *self,
    const TxnBeginMessage *begin,
    uint8_t interface_index,
    uint64_t now_us,
    TxnBeginAckMessage *ack
);
void TransactionRunner_DefineTemplate(TransactionRunner *self, const TxnTemplateMessage *template_message);
void TransactionRunner_PushSteps(TransactionRunner *self, const TxnStepsMessage *steps);
void TransactionRunner_OnFrame(TransactionRunner *self, uint8_t interface_index, const FrameMessage *frame);
void TransactionRunner_Tick(TransactionRunner *self, uint64_t now_us);
void TransactionRunner_Abort(TransactionRunner *self, uint16_t txn_id);
void TransactionRunner_Cancel(TransactionRunner *self);
bool TransactionRunner_IsActive(const TransactionRunner *self);
uint64_t TransactionRunner_NextWakeUs(const TransactionRunner *self);
