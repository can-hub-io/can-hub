#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/register_message.h"

#define ECHO_CORRELATOR_PENDING_MAX 32

/*
 * Pairs injected TX frames with their bus echo, per interface. The hub tags
 * each injection with an origin token; the correlator queues it when the
 * frame is written to the bus and gives it back when the kernel echo
 * returns. Echoes arrive in TX order, so a match scans from the oldest
 * entry and discards anything older than the match (their TX was lost — no
 * echo will ever come). A failed write drops its entry immediately.
 */
typedef struct {
    uint8_t tokens[REGISTER_INTERFACES_MAX][ECHO_CORRELATOR_PENDING_MAX];
    uint32_t can_ids[REGISTER_INTERFACES_MAX][ECHO_CORRELATOR_PENDING_MAX];
    uint8_t head[REGISTER_INTERFACES_MAX];
    uint8_t count[REGISTER_INTERFACES_MAX];
} EchoCorrelator;

void EchoCorrelator_Reset(EchoCorrelator *self);
void EchoCorrelator_Push(EchoCorrelator *self, uint8_t interface_index, uint8_t token, uint32_t can_id);
void EchoCorrelator_DropNewest(EchoCorrelator *self, uint8_t interface_index);
bool EchoCorrelator_PopMatch(EchoCorrelator *self, uint8_t interface_index, uint32_t can_id, uint8_t *token);
