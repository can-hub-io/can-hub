#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canhub.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"

/*
 * The libcanhub view of a transaction and the wire one, for both the linux and
 * the windows library: begin, template and step move into wire messages, the
 * agent's terminal status into a result, and a begin status onto the
 * library's error codes and a reason.
 */
bool CanHubTransactions_BeginToWire(const CanHubTxnBegin *begin, TxnBeginMessage *message);
bool CanHubTransactions_TemplateToWire(const CanHubTxnTemplate *template_definition, TxnTemplateMessage *message);
void CanHubTransactions_StepToWire(const CanHubTxnStep *step, TxnStep *wire_step);
void CanHubTransactions_ResultFromWire(const TxnStatusMessage *status, CanHubTxnResult *result);
int32_t CanHubTransactions_BeginResult(uint8_t begin_status);
const char *CanHubTransactions_BeginDetail(uint8_t begin_status);
