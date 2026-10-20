#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canhub.h"

#define CANHUB_UDS_NO_ANSWER_TEMPLATE 0

/*
 * What a UDS request turns into, for both libraries: an ISO-TP template per
 * length of the expected start of the answer (its template id is that
 * length; 0 does not wait), and each request's message with the value its
 * answer must start with.
 */
void CanHubUds_Template(const CanHubUds *uds, uint8_t expect_length, CanHubTxnTemplate *definition);
bool CanHubUds_Message(
    const CanHubUds *uds,
    const uint8_t *request,
    uint16_t length,
    const uint8_t *expect,
    uint8_t expect_length,
    CanHubTxnMessage *message
);
