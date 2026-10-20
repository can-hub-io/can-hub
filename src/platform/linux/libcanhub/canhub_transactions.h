#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canhub.h"
#include "client/transaction_sender.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"

#define CANHUB_TRANSACTIONS_MESSAGE_RECORD_BYTES 248
#define CANHUB_TRANSACTIONS_MESSAGE_RECORDS_MAX 17

/*
 * The libcanhub view of a transaction and the wire one, for both the linux and
 * the windows library: begin, template, step and ISO-TP message (cut into
 * records) move into wire messages, the
 * agent's terminal status into a result, and a begin status onto the
 * library's error codes and a reason.
 */
bool CanHubTransactions_BeginToWire(const CanHubTxnBegin *begin, TxnBeginMessage *message);
bool CanHubTransactions_TemplateToWire(const CanHubTxnTemplate *template_definition, TxnTemplateMessage *message);
void CanHubTransactions_StepToWire(const CanHubTxnStep *step, TxnStep *wire_step);
uint8_t CanHubTransactions_MessageToWire(const CanHubTxnMessage *message, TxnStep *records);
bool CanHubTransactions_HasCreditFor(const TransactionSender *sender, const TxnStep *records, uint8_t count);
void CanHubTransactions_ResultFromWire(const TxnStatusMessage *status, CanHubTxnResult *result);
int32_t CanHubTransactions_BeginResult(uint8_t begin_status);
const char *CanHubTransactions_BeginDetail(uint8_t begin_status);
