#pragma once

#include <stdbool.h>

#include "protocol/frame_message.h"

/*
 * cansend frame syntax, as can-hub-client send takes it:
 *   <id>#<data>          classic frame (FD once the data exceeds 8 bytes)
 *   <id>##<flags><data>  CAN FD frame; flags is one hex digit, bit 0 = BRS
 *   <id>#R[<len>]        remote request, optional length 0-8
 * The id is hex; more than 3 digits or a value above 0x7FF makes it extended.
 * Data bytes are hex pairs, optionally separated by '.'.
 */
bool FrameSpec_Parse(const char *text, FrameMessage *frame);
