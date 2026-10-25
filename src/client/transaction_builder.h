#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canhub.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"

#define TRANSACTION_BUILDER_MESSAGE_RECORD_BYTES 248
#define TRANSACTION_BUILDER_RECORDS_MAX 17

/*
 * Turns the steps of the libcanhub transaction API into the wire (see
 * doc/protocol.md "Transactions"): each distinct shape a step waits with -
 * its response, the bytes and the id its where compares, its gap, its ISO-TP
 * link - is one wire template, defined the first time a step needs it and
 * found again by content, so a response built anew for every step costs
 * nothing. The roster goes out once, with the first response that names
 * nodes. A step the agent could not run is refused with a reason, before
 * anything of it is sent.
 */
typedef struct {
    bool defines_roster;
    TxnRosterMessage roster;
    bool defines_template;
    TxnTemplateMessage template_message;
    uint8_t record_count;
    TxnStep records[TRANSACTION_BUILDER_RECORDS_MAX];
} TransactionBuild;

typedef struct {
    bool bitrate_switch;
    bool has_roster;
    TxnRosterMessage roster;
    uint8_t template_count;
    TxnTemplateMessage templates[TXN_TEMPLATES_MAX];
    uint8_t last_template;
    const char *error;
} TransactionBuilder;

typedef struct {
    bool has_roster;
    uint8_t template_count;
    uint8_t last_template;
} TransactionBuilderMark;

bool TransactionBuilder_Begin(const CanHubTxnOptions *options, TxnBeginMessage *begin, const char **error);
void TransactionBuilder_Init(TransactionBuilder *self, bool bitrate_switch);
bool TransactionBuilder_Step(TransactionBuilder *self, const CanHubTxnStep *step, TransactionBuild *build);
TransactionBuilderMark TransactionBuilder_Mark(const TransactionBuilder *self);
void TransactionBuilder_Rewind(TransactionBuilder *self, const TransactionBuilderMark *mark);
const char *TransactionBuilder_Error(const TransactionBuilder *self);
