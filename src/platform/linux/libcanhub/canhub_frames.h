#pragma once

#include <stdbool.h>

#include "canhub.h"
#include "protocol/frame_message.h"
#include "protocol/subscribe_message.h"
#include "protocol/txn_message.h"

/*
 * The libcanhub view of frames and filters and the wire one, for both the
 * linux and the windows library: the id with its extended flag next to it
 * becomes the wire can_id with its flag bits, and back.
 */
void CanHubFrames_FromWire(const FrameMessage *message, CanHubFrame *frame);
void CanHubFrames_FromRxFrame(const TxnRxFrame *rx_frame, CanHubFrame *frame);
bool CanHubFrames_ToWire(const CanHubFrame *frame, FrameMessage *message);
bool CanHubFrames_FilterToWire(const CanHubFilter *filter, CanFilter *wire);
