#include "client/transaction_builder.h"

#include <string.h>

#include "client/can_id.h"
#include "protocol/frame_message.h"

#define BITS_PER_BYTE 8
#define BYTE_VALUE_MAX 0xFF
#define ID_BITS_MAX 32
#define NO_TEMPLATE 0xFF

static const uint8_t FD_LENGTHS[] = { 12, 16, 20, 24, 32, 48, 64 };

static bool fail(TransactionBuilder *self, const char *message);
static bool isFrameLength(uint32_t length);
static bool isKeepaliveValid(const CanHubTxnKeepalive *keepalive, const char **error);
static bool isMatchValid(const CanHubTxnMatch *match);
static bool isSameId(const CanHubTxnMatch *first, const CanHubTxnMatch *second);
static bool checkStep(TransactionBuilder *self, const CanHubTxnStep *step);
static bool checkLink(TransactionBuilder *self, const CanHubTxnStep *step);
static bool checkFrame(TransactionBuilder *self, const CanHubTxnStep *step);
static bool checkResponse(TransactionBuilder *self, const CanHubTxnResponse *response, const CanHubTxnMatch *where);
static bool checkNodes(TransactionBuilder *self, const CanHubTxnResponse *response);
static bool checkIsotpResponse(TransactionBuilder *self, const CanHubTxnStep *step);
static void buildTemplate(const CanHubTxnStep *step, TxnTemplateMessage *template_message);
static void buildAdvance(const CanHubTxnResponse *response, const CanHubTxnMatch *where, TxnPattern *pattern);
static void toPattern(const CanHubTxnMatch *match, TxnPattern *pattern);
static void bytesToWord(const CanHubTxnByte *bytes, uint64_t *mask, uint64_t *value);
static void toIsotpParameters(const CanHubTxnIsotpLink *link, TxnIsotpParameters *parameters);
static bool buildRoster(TransactionBuilder *self, const CanHubTxnResponse *response, TransactionBuild *build);
static void toRoster(const CanHubTxnResponse *response, TxnRosterMessage *roster);
static uint8_t lowestSetBit(uint32_t value);
static bool placeTemplate(TransactionBuilder *self, TxnTemplateMessage *template_message, TransactionBuild *build);
static bool findTemplate(const TransactionBuilder *self, const TxnTemplateMessage *template_message, uint8_t *template_id);
static void buildRecords(const TransactionBuilder *self, const CanHubTxnStep *step, uint8_t template_id, TransactionBuild *build);
static void buildFrameRecord(const TransactionBuilder *self, const CanHubTxnStep *step, TxnStep *record);
static void buildMessageRecords(const CanHubTxnStep *step, TransactionBuild *build);
static uint64_t expectValue(const CanHubTxnStep *step);

/* ---------- public ---------- */

bool TransactionBuilder_Begin(const CanHubTxnOptions *options, TxnBeginMessage *begin, const char **error)
{
    const CanHubTxnKeepalive *keepalive;

    memset(begin, 0, sizeof(*begin));
    if (options == NULL) {
        return true;
    }
    if (options->idle_timeout_ms == 0) {
        *error = "options need idle_timeout_ms (pass NULL options for the defaults)";
        return false;
    }

    begin->flags = options->exclusive ? TXN_BEGIN_FLAG_EXCLUSIVE : 0;
    begin->idle_timeout_ms = options->idle_timeout_ms;
    keepalive = options->keepalive;
    if (keepalive == NULL) {
        return true;
    }
    if (!isKeepaliveValid(keepalive, error)) {
        return false;
    }

    begin->keepalive.period_ms = keepalive->every_ms;
    begin->keepalive.can_id = CanId_ToWire(keepalive->id);
    begin->keepalive.payload_length = keepalive->length;
    if (keepalive->length > FRAME_PAYLOAD_MAX_CLASSIC) {
        begin->keepalive.frame_flags = (uint8_t)(FRAME_FLAG_FD | (options->bitrate_switch ? FRAME_FLAG_BRS : 0));
    }
    memcpy(begin->keepalive.payload, keepalive->data, keepalive->length);
    return true;
}

void TransactionBuilder_Init(TransactionBuilder *self, bool bitrate_switch)
{
    memset(self, 0, sizeof(*self));
    self->bitrate_switch = bitrate_switch;
    self->last_template = NO_TEMPLATE;
}

bool TransactionBuilder_Step(TransactionBuilder *self, const CanHubTxnStep *step, TransactionBuild *build)
{
    TxnTemplateMessage template_message;

    build->defines_roster = false;
    build->defines_template = false;
    build->record_count = 0;
    self->error = NULL;

    if (!checkStep(self, step)) {
        return false;
    }
    if (step->response != NULL && step->response->nodes != NULL && !buildRoster(self, step->response, build)) {
        return false;
    }

    buildTemplate(step, &template_message);
    if (!placeTemplate(self, &template_message, build)) {
        build->defines_roster = false;
        return false;
    }
    if (build->defines_roster) {
        self->has_roster = true;
        self->roster = build->roster;
    }

    buildRecords(self, step, self->last_template, build);
    return true;
}

const char *TransactionBuilder_Error(const TransactionBuilder *self)
{
    return self->error;
}

/* ---------- private ---------- */

static bool fail(TransactionBuilder *self, const char *message)
{
    self->error = message;
    return false;
}

static bool isFrameLength(uint32_t length)
{
    uint8_t i;

    if (length <= FRAME_PAYLOAD_MAX_CLASSIC) {
        return true;
    }
    for(i=0; i<sizeof(FD_LENGTHS); i++) {
        if (FD_LENGTHS[i] == length) {
            return true;
        }
    }
    return false;
}

static bool isKeepaliveValid(const CanHubTxnKeepalive *keepalive, const char **error)
{
    if (keepalive->every_ms == 0) {
        *error = "a keepalive needs every_ms";
        return false;
    }
    if (!CanId_IsValid(keepalive->id)) {
        *error = "the keepalive id does not fit its 11 or 29 bits";
        return false;
    }
    if (!isFrameLength(keepalive->length)) {
        *error = "a keepalive frame is 0-8 bytes, or 12, 16, 20, 24, 32, 48 or 64 for CAN FD";
        return false;
    }
    return true;
}

static bool isMatchValid(const CanHubTxnMatch *match)
{
    return CanId_IsValid(match->id) && CanId_AreIgnoredBitsValid(match->id, match->id_ignored_bits);
}

static bool isSameId(const CanHubTxnMatch *first, const CanHubTxnMatch *second)
{
    return first->id.value == second->id.value && first->id.extended == second->id.extended &&
           first->id_ignored_bits == second->id_ignored_bits;
}

static bool checkStep(TransactionBuilder *self, const CanHubTxnStep *step)
{
    if (step->isotp != NULL) {
        if (!checkLink(self, step)) {
            return false;
        }
    } else if (!checkFrame(self, step)) {
        return false;
    }
    if (step->response == NULL) {
        return step->where == NULL || fail(self, "where needs a response");
    }
    if (!checkResponse(self, step->response, step->where)) {
        return false;
    }
    return step->isotp == NULL || checkIsotpResponse(self, step);
}

static bool checkLink(TransactionBuilder *self, const CanHubTxnStep *step)
{
    const CanHubTxnIsotpLink *link = step->isotp;

    if (!CanId_IsValid(link->tx_id) || !CanId_IsValid(link->rx_id)) {
        return fail(self, "an ISO-TP link id does not fit its 11 or 29 bits");
    }
    if (step->id.value != link->tx_id.value || step->id.extended != link->tx_id.extended) {
        return fail(self, "an ISO-TP step is sent on its link's tx_id");
    }
    if (link->fd && (link->fd_data_length < FRAME_PAYLOAD_MAX_CLASSIC || !isFrameLength(link->fd_data_length))) {
        return fail(self, "an ISO-TP link over CAN FD needs fd_data_length 8, 12, 16, 20, 24, 32, 48 or 64");
    }
    if (!link->fd && link->bitrate_switch) {
        return fail(self, "bit rate switch is CAN FD: the ISO-TP link needs fd");
    }
    if (step->data == NULL) {
        return step->response != NULL || fail(self, "an ISO-TP step that sends nothing waits for a response");
    }
    if (step->length == 0 || step->length > CANHUB_ISOTP_MESSAGE_MAX) {
        return fail(self, "an ISO-TP message is 1-4095 bytes");
    }
    return true;
}

static bool checkFrame(TransactionBuilder *self, const CanHubTxnStep *step)
{
    if (step->data == NULL) {
        return step->response != NULL || fail(self, "a step that sends nothing waits for a response");
    }
    if (!CanId_IsValid(step->id)) {
        return fail(self, "the id does not fit its 11 or 29 bits");
    }
    if (!isFrameLength(step->length)) {
        return fail(self, "a frame is 0-8 bytes, or 12, 16, 20, 24, 32, 48 or 64 for CAN FD");
    }
    return true;
}

static bool checkResponse(TransactionBuilder *self, const CanHubTxnResponse *response, const CanHubTxnMatch *where)
{
    if (response->timeout_ms == 0) {
        return fail(self, "a response needs timeout_ms");
    }
    if (response->pending != NULL && response->timeout_after_pending_ms == 0) {
        return fail(self, "a response with pending needs timeout_after_pending_ms");
    }
    if (!isMatchValid(&response->success)) {
        return fail(self, "the success id or its ignored bits do not fit 11 or 29 bits");
    }
    if (response->failure != NULL && !isMatchValid(response->failure)) {
        return fail(self, "the failure id or its ignored bits do not fit 11 or 29 bits");
    }
    if (response->pending != NULL && !isMatchValid(response->pending)) {
        return fail(self, "the pending id or its ignored bits do not fit 11 or 29 bits");
    }
    if (where != NULL && !isMatchValid(where)) {
        return fail(self, "the where id or its ignored bits do not fit 11 or 29 bits");
    }
    if (where != NULL && response->nodes != NULL && !isSameId(where, &response->success)) {
        return fail(self, "where on a response with nodes keeps the response's id");
    }
    return checkNodes(self, response);
}

static bool checkNodes(TransactionBuilder *self, const CanHubTxnResponse *response)
{
    uint32_t ignored = response->success.id_ignored_bits;
    uint8_t shift = lowestSetBit(ignored);
    uint32_t i;

    if (response->nodes == NULL) {
        if (response->node_count != 0 || response->allowed_missing != 0) {
            return fail(self, "node_count and allowed_missing need nodes");
        }
        return true;
    }
    if (response->node_count == 0 || response->node_count > CANHUB_TXN_ROSTER_MAX) {
        return fail(self, "a response waits for 1 to 64 nodes");
    }
    if (response->allowed_missing >= response->node_count) {
        return fail(self, "allowed_missing must leave at least one node to wait for");
    }
    if (response->node_from == CANHUB_TXN_NODE_IN_BYTE) {
        if (response->node_byte >= CANHUB_FRAME_PAYLOAD_MAX) {
            return fail(self, "node_byte is a data byte, 0-63");
        }
        for(i=0; i<response->node_count; i++) {
            if (response->nodes[i] > BYTE_VALUE_MAX) {
                return fail(self, "a node told by a data byte is 0-255");
            }
        }
        return true;
    }
    if (response->node_from != CANHUB_TXN_NODE_IN_ID) {
        return fail(self, "node_from is CANHUB_TXN_NODE_IN_ID or CANHUB_TXN_NODE_IN_BYTE");
    }
    if (ignored == 0) {
        return fail(self, "nodes told by the id need the bits that tell them in success.id_ignored_bits");
    }
    for(i=0; i<response->node_count; i++) {
        if ((((uint64_t)response->nodes[i] << shift) & ~(uint64_t)ignored) != 0) {
            return fail(self, "a node does not fit the bits success.id_ignored_bits leaves free");
        }
    }
    return true;
}

static bool checkIsotpResponse(TransactionBuilder *self, const CanHubTxnStep *step)
{
    const CanHubTxnMatch *where = step->where;
    CanHubTxnMatch rx = { step->isotp->rx_id, 0, { { 0, 0 } } };

    if (step->response->nodes != NULL) {
        return fail(self, "an ISO-TP response does not wait for nodes");
    }
    if (!isSameId(&step->response->success, &rx) || (where != NULL && !isSameId(where, &rx))) {
        return fail(self, "an ISO-TP response matches the link's rx_id exactly");
    }
    return true;
}

static void buildTemplate(const CanHubTxnStep *step, TxnTemplateMessage *template_message)
{
    const CanHubTxnResponse *response = step->response;

    memset(template_message, 0, sizeof(*template_message));
    template_message->gap_us = step->gap_us;
    if (step->isotp != NULL) {
        template_message->type = kTXN_TEMPLATE_TYPE_ISOTP;
        toIsotpParameters(step->isotp, &template_message->isotp);
    }
    if (response == NULL) {
        template_message->advance_mode = kTXN_ADVANCE_MODE_NONE;
        return;
    }

    template_message->advance_mode = kTXN_ADVANCE_MODE_COUNT;
    template_message->count_k = 1;
    if (response->nodes != NULL) {
        template_message->advance_mode = response->allowed_missing == 0 ? kTXN_ADVANCE_MODE_ROSTER_ALL : kTXN_ADVANCE_MODE_ROSTER_K;
        template_message->count_k = (uint8_t)(response->node_count - response->allowed_missing);
    }
    template_message->timeout_ms = response->timeout_ms;
    template_message->flags = (uint8_t)((response->continue_on_timeout ? TXN_TEMPLATE_FLAG_SOFT : 0) |
                                        (response->on_mismatch_abort ? TXN_TEMPLATE_FLAG_MISMATCH_ABORT : 0));

    buildAdvance(response, step->where, &template_message->patterns[kTXN_PATTERN_ADVANCE]);
    template_message->patterns_present = 1u << kTXN_PATTERN_ADVANCE;
    if (response->failure != NULL) {
        toPattern(response->failure, &template_message->patterns[kTXN_PATTERN_ABORT]);
        template_message->patterns_present |= 1u << kTXN_PATTERN_ABORT;
    }
    if (response->pending != NULL) {
        toPattern(response->pending, &template_message->patterns[kTXN_PATTERN_EXTEND]);
        template_message->patterns_present |= 1u << kTXN_PATTERN_EXTEND;
        template_message->extend_timeout_ms = response->timeout_after_pending_ms;
    }
}

static void buildAdvance(const CanHubTxnResponse *response, const CanHubTxnMatch *where, TxnPattern *pattern)
{
    uint64_t where_mask;
    uint64_t where_value;

    toPattern(where != NULL ? where : &response->success, pattern);
    if (where == NULL) {
        return;
    }

    bytesToWord(response->success.bytes, &pattern->payload_mask, &pattern->payload_value);
    bytesToWord(where->bytes, &where_mask, &where_value);
    pattern->payload_value &= ~where_mask;
    pattern->payload_mask |= where_mask;
}

static void toPattern(const CanHubTxnMatch *match, TxnPattern *pattern)
{
    uint32_t width = CanId_Width(match->id);
    uint32_t compared = width & ~match->id_ignored_bits;

    pattern->can_id = match->id.value & compared;
    if (match->id.extended) {
        pattern->can_id |= FRAME_CAN_ID_FLAG_EFF;
    }
    pattern->can_mask = compared | FRAME_CAN_ID_FLAG_EFF;
    bytesToWord(match->bytes, &pattern->payload_mask, &pattern->payload_value);
}

static void bytesToWord(const CanHubTxnByte *bytes, uint64_t *mask, uint64_t *value)
{
    uint8_t i;

    *mask = 0;
    *value = 0;
    for(i=0; i<CANHUB_TXN_MATCH_BYTES; i++) {
        *mask |= (uint64_t)bytes[i].mask << (i * BITS_PER_BYTE);
        *value |= (uint64_t)(bytes[i].value & bytes[i].mask) << (i * BITS_PER_BYTE);
    }
}

static void toIsotpParameters(const CanHubTxnIsotpLink *link, TxnIsotpParameters *parameters)
{
    parameters->response_id = CanId_ToWire(link->rx_id);
    parameters->flags = (uint8_t)((link->padded ? TXN_ISOTP_FLAG_PADDING : 0) |
                                  (link->address_byte ? TXN_ISOTP_FLAG_ADDRESS_BYTE : 0) |
                                  (link->fd ? TXN_ISOTP_FLAG_FD : 0) |
                                  (link->bitrate_switch ? TXN_ISOTP_FLAG_BIT_RATE_SWITCH : 0));
    parameters->tx_address = link->address_byte ? link->tx_address : 0;
    parameters->rx_address = link->address_byte ? link->rx_address : 0;
    parameters->padding = link->padded ? link->padding : 0;
    parameters->tx_data_length = link->fd ? link->fd_data_length : FRAME_PAYLOAD_MAX_CLASSIC;
    parameters->block_size = link->block_size;
    parameters->st_min = link->st_min;
}

static bool buildRoster(TransactionBuilder *self, const CanHubTxnResponse *response, TransactionBuild *build)
{
    toRoster(response, &build->roster);
    if (!self->has_roster) {
        build->defines_roster = true;
        return true;
    }
    if (memcmp(&self->roster, &build->roster, sizeof(build->roster)) != 0) {
        return fail(self, "a transaction waits for one set of nodes, told the same way, in every response");
    }
    return true;
}

static void toRoster(const CanHubTxnResponse *response, TxnRosterMessage *roster)
{
    memset(roster, 0, sizeof(*roster));
    if (response->node_from == CANHUB_TXN_NODE_IN_BYTE) {
        roster->origin.source = kTXN_ORIGIN_SOURCE_PAYLOAD;
        roster->origin.payload_offset = response->node_byte;
        roster->origin.mask = BYTE_VALUE_MAX;
    } else {
        roster->origin.source = kTXN_ORIGIN_SOURCE_CAN_ID;
        roster->origin.mask = response->success.id_ignored_bits;
        roster->origin.shift = lowestSetBit(response->success.id_ignored_bits);
    }
    roster->roster_count = (uint8_t)response->node_count;
    memcpy(roster->roster, response->nodes, response->node_count * sizeof(response->nodes[0]));
}

static uint8_t lowestSetBit(uint32_t value)
{
    uint8_t bit = 0;

    while (bit < ID_BITS_MAX && (value & ((uint32_t)1 << bit)) == 0) {
        bit++;
    }
    return bit == ID_BITS_MAX ? 0 : bit;
}

static bool placeTemplate(TransactionBuilder *self, TxnTemplateMessage *template_message, TransactionBuild *build)
{
    uint8_t template_id;

    if (findTemplate(self, template_message, &template_id)) {
        self->last_template = template_id;
        return true;
    }
    if (self->template_count == TXN_TEMPLATES_MAX) {
        return fail(self, "the agent holds 16 ways of waiting per transaction: each distinct response, where shape, gap and ISO-TP link is one");
    }

    template_message->template_id = self->template_count;
    self->templates[self->template_count] = *template_message;
    self->last_template = self->template_count;
    self->template_count++;
    build->defines_template = true;
    build->template_message = *template_message;
    return true;
}

static bool findTemplate(const TransactionBuilder *self, const TxnTemplateMessage *template_message, uint8_t *template_id)
{
    TxnTemplateMessage candidate;
    uint8_t i;

    if (self->last_template != NO_TEMPLATE) {
        candidate = self->templates[self->last_template];
        candidate.template_id = 0;
        if (memcmp(&candidate, template_message, sizeof(candidate)) == 0) {
            *template_id = self->last_template;
            return true;
        }
    }
    for(i=0; i<self->template_count; i++) {
        candidate = self->templates[i];
        candidate.template_id = 0;
        if (memcmp(&candidate, template_message, sizeof(candidate)) == 0) {
            *template_id = i;
            return true;
        }
    }
    return false;
}

static void buildRecords(const TransactionBuilder *self, const CanHubTxnStep *step, uint8_t template_id, TransactionBuild *build)
{
    uint8_t i;

    if (step->isotp != NULL && step->data != NULL) {
        buildMessageRecords(step, build);
    } else {
        buildFrameRecord(self, step, &build->records[0]);
        build->record_count = 1;
    }
    for(i=0; i<build->record_count; i++) {
        build->records[i].template_id = template_id;
    }
}

static void buildFrameRecord(const TransactionBuilder *self, const CanHubTxnStep *step, TxnStep *record)
{
    memset(record, 0, sizeof(*record));
    if (step->where != NULL) {
        record->flags |= TXN_STEP_FLAG_EXPECT_VALUE;
        record->expect_value = expectValue(step);
    }
    if (step->data == NULL) {
        record->flags |= TXN_STEP_FLAG_EXPECT_ONLY;
        record->can_id = step->isotp != NULL ? CanId_ToWire(step->isotp->tx_id) : 0;
        return;
    }

    record->can_id = CanId_ToWire(step->id);
    record->payload_length = (uint8_t)step->length;
    if (step->length > FRAME_PAYLOAD_MAX_CLASSIC) {
        record->frame_flags = (uint8_t)(FRAME_FLAG_FD | (self->bitrate_switch ? FRAME_FLAG_BRS : 0));
    }
    memcpy(record->payload, step->data, step->length);
}

static void buildMessageRecords(const CanHubTxnStep *step, TransactionBuild *build)
{
    TxnStep *record;
    uint16_t offset = 0;
    uint16_t piece;

    while (offset < step->length) {
        piece = (uint16_t)(step->length - offset);
        if (piece > TRANSACTION_BUILDER_MESSAGE_RECORD_BYTES) {
            piece = TRANSACTION_BUILDER_MESSAGE_RECORD_BYTES;
        }
        record = &build->records[build->record_count++];
        memset(record, 0, sizeof(*record));
        record->can_id = CanId_ToWire(step->isotp->tx_id);
        record->flags = TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED;
        record->payload_length = (uint8_t)piece;
        memcpy(record->payload, step->data + offset, piece);
        offset = (uint16_t)(offset + piece);
    }

    record = &build->records[build->record_count - 1];
    record->flags = TXN_STEP_FLAG_MESSAGE;
    if (step->where != NULL) {
        record->flags |= TXN_STEP_FLAG_EXPECT_VALUE;
        record->expect_value = expectValue(step);
    }
}

static uint64_t expectValue(const CanHubTxnStep *step)
{
    uint64_t success_mask;
    uint64_t success_value;
    uint64_t where_mask;
    uint64_t where_value;

    bytesToWord(step->response->success.bytes, &success_mask, &success_value);
    bytesToWord(step->where->bytes, &where_mask, &where_value);
    return (success_value & ~where_mask) | where_value;
}
