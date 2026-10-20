#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/frame_message.h"
#include "protocol/txn_message.h"

/*
 * One ISO 15765-2 conversation with a peer at the bus. It sends a message as a
 * single frame, or a first frame and consecutive frames paced by the peer's
 * flow control, and reassembles the peer's messages, answering their first
 * frames with flow control of its own. Frames arrive through OnFrame, which
 * only updates state; Tick carries the time and sends whatever is due.
 */

#define ISOTP_TIMEOUT_US 1000000
#define ISOTP_WAIT_FRAMES_MAX 10
#define ISOTP_FRAMES_PER_TICK_MAX 64
#define ISOTP_RETRY_US 1000

typedef enum tisotp_event_e {
    kISOTP_EVENT_NONE = 0,
    kISOTP_EVENT_SENT = 1,
    kISOTP_EVENT_MESSAGE = 2,
    kISOTP_EVENT_FAILED = 3,
    kISOTP_EVENT_MAX,
} TISOTP_EVENT;

typedef enum tisotp_sender_state_e {
    kISOTP_SENDER_IDLE = 0,
    kISOTP_SENDER_FIRST_FRAME = 1,
    kISOTP_SENDER_FLOW_CONTROL = 2,
    kISOTP_SENDER_CONSECUTIVE_FRAMES = 3,
    kISOTP_SENDER_MAX,
} TISOTP_SENDER_STATE;

typedef enum tisotp_receiver_state_e {
    kISOTP_RECEIVER_IDLE = 0,
    kISOTP_RECEIVER_CONSECUTIVE_FRAMES = 1,
    kISOTP_RECEIVER_MAX,
} TISOTP_RECEIVER_STATE;

typedef struct {
    void *context;
    bool (*transmit)(void *context, const FrameMessage *frame);
} IsotpLinkPort;

typedef struct {
    const uint8_t *message;
    uint16_t length;
    uint16_t offset;
    uint8_t state;
    uint8_t sequence;
    bool block_limited;
    uint8_t block_remaining;
    uint8_t wait_count;
    uint32_t st_min_us;
    uint64_t next_frame_us;
    uint64_t deadline_us;
} IsotpSender;

typedef struct {
    uint8_t message[TXN_ISOTP_MESSAGE_SIZE_MAX];
    uint16_t length;
    uint16_t received;
    uint8_t state;
    uint8_t sequence;
    uint8_t block_received;
    bool flow_control_pending;
    uint64_t flow_control_due_us;
    uint64_t deadline_us;
} IsotpReceiver;

typedef struct {
    IsotpLinkPort port;
    uint32_t request_id;
    uint8_t channel;
    TxnIsotpParameters parameters;
    IsotpSender sender;
    IsotpReceiver receiver;
    uint8_t failure_reason;
} IsotpLink;

void IsotpLink_Init(IsotpLink *self, IsotpLinkPort port);
void IsotpLink_Open(IsotpLink *self, uint32_t request_id, uint8_t channel, const TxnIsotpParameters *parameters);
void IsotpLink_Send(IsotpLink *self, const uint8_t *message, uint16_t length, uint64_t now_us);
uint8_t IsotpLink_OnFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us);
uint8_t IsotpLink_Tick(IsotpLink *self, uint64_t now_us);
uint64_t IsotpLink_NextWakeUs(const IsotpLink *self);
uint8_t IsotpLink_FailureReason(const IsotpLink *self);
void IsotpLink_MessageFrame(const IsotpLink *self, FrameMessage *frame);
