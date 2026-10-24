#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canhub.h"
#include "client/client.h"
#include "protocol/txn_message.h"

/*
 * What the libraries' shared modules need from a session beyond the public
 * API, implemented by each platform's canhub.c and kept out of the exports,
 * and what a session hands to the shared transaction module.
 */
void CanHubSession_SetError(CanHubSession *session, const char *message);
const char *CanHubSession_Error(const CanHubSession *session);
Client *CanHubSession_Client(CanHubSession *session);
void CanHubSession_Pump(CanHubSession *session, int32_t timeout_ms);
bool CanHubSession_IsDisconnected(const CanHubSession *session);
CanHubTxn *CanHubSession_Transaction(const CanHubSession *session);
void CanHubSession_SetTransaction(CanHubSession *session, CanHubTxn *transaction);

void CanHubTxn_OnRosterReport(CanHubTxn *transaction, const TxnRosterReportMessage *report);
