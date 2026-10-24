#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canhub.h"

#define CONNECT_TIMEOUT_MS 5000
#define OPEN_TIMEOUT_MS 5000
#define IDLE_TIMEOUT_MS 5000
#define EXIT_ABORTED 2
#define REQUEST_ID 0x7E0
#define RESPONSE_ID 0x7E8
#define TRANSFER_DATA 0x36
#define POSITIVE_TRANSFER 0x76
#define NEGATIVE_RESPONSE 0x7F
#define RESPONSE_PENDING 0x78
#define BATCH_ID 0x123
#define ACK_ID_BASE 0x700
#define NODE_BITS 0x1F
#define ACK_MARKER 0x01
#define BATCH_FRAMES 16
#define FRAME_BYTES 8
#define NODES_MAX 32
#define BYTE_MASK 0xFF
#define BITS_PER_BYTE 8

static const CanHubTxnMatch NEGATIVE = { { RESPONSE_ID, false }, 0, { [1] = CANHUB_TXN_EQUALS(NEGATIVE_RESPONSE) } };
static const CanHubTxnMatch BUSY = {
    { RESPONSE_ID, false }, 0, { [1] = CANHUB_TXN_EQUALS(NEGATIVE_RESPONSE), [3] = CANHUB_TXN_EQUALS(RESPONSE_PENDING) }
};

static const CanHubTxnResponse TRANSFER_ACCEPTED = {
    .success = { .id = { RESPONSE_ID, false }, .bytes = { [1] = CANHUB_TXN_EQUALS(POSITIVE_TRANSFER) } },
    .failure = &NEGATIVE,
    .pending = &BUSY,
    .timeout_ms = 200,
    .timeout_after_pending_ms = 200,
    .on_mismatch_abort = true,
};

static uint32_t nodes[NODES_MAX];

static int32_t runRequests(CanHubSession *session, uint32_t count);
static int32_t runBatches(CanHubSession *session, uint32_t batches, uint32_t node_count);
static int32_t sendBatch(CanHubTxn *transaction, uint32_t batch, const CanHubTxnResponse *acknowledgement);
static void printRoster(void *context, const CanHubTxnRosterResult *result);
static int32_t report(int32_t result, const CanHubTxnReport *outcome, CanHubSession *session);
static void printMissing(const CanHubTxnRosterResult *result);

int main(int argc, char **argv)
{
    CanHubSession *session;
    CanHubConnectConfig config = { 0 };
    int32_t result;

    if (argc < 5) {
        fprintf(stderr, "usage: %s <url> <interface> uds <count> | batch <batches> <nodes>\n", argv[0]);
        return 1;
    }

    config.struct_size = sizeof(config);
    config.url = argv[1];
    config.connect_timeout_ms = CONNECT_TIMEOUT_MS;
    session = canhub_connect(&config);
    if (session == NULL) {
        fprintf(stderr, "could not connect to %s: %s\n", argv[1], canhub_last_error(NULL));
        return 1;
    }
    if (canhub_open(session, argv[2], CANHUB_OPEN_FLAG_WRITE | CANHUB_OPEN_FLAG_RELIABLE, OPEN_TIMEOUT_MS) != CANHUB_OK) {
        fprintf(stderr, "open failed: %s\n", canhub_last_error(session));
        canhub_close(session);
        return 1;
    }

    if (strcmp(argv[3], "uds") == 0) {
        result = runRequests(session, (uint32_t)strtoul(argv[4], NULL, 10));
    } else if (strcmp(argv[3], "batch") == 0 && argc >= 6) {
        result = runBatches(session, (uint32_t)strtoul(argv[4], NULL, 10), (uint32_t)strtoul(argv[5], NULL, 10));
    } else {
        fprintf(stderr, "unknown command\n");
        result = 1;
    }

    canhub_close(session);
    return result;
}

static int32_t runRequests(CanHubSession *session, uint32_t count)
{
    uint8_t request[] = { 0x02, TRANSFER_DATA, 0x00 };
    CanHubTxnReport outcome;
    CanHubTxnMatch where;
    CanHubTxn *transaction;
    int32_t result = CANHUB_OK;
    uint32_t i;

    transaction = canhub_txn_create(session, NULL);
    if (transaction == NULL) {
        fprintf(stderr, "%s\n", canhub_last_error(session));
        return 1;
    }

    for(i=0; i<count && result == CANHUB_OK; i++) {
        request[2] = (uint8_t)(i & BYTE_MASK);
        memset(&where, 0, sizeof(where));
        where.id = CANHUB_STANDARD_ID(RESPONSE_ID);
        where.bytes[2] = (CanHubTxnByte)CANHUB_TXN_EQUALS(request[2]);
        result = canhub_txn_step(transaction, &(CanHubTxnStep){
            .id = CANHUB_STANDARD_ID(REQUEST_ID),
            .data = request,
            .length = sizeof(request),
            .response = &TRANSFER_ACCEPTED,
            .where = &where,
        });
    }

    result = canhub_txn_finish(transaction, &outcome);
    return report(result, &outcome, session);
}

static int32_t runBatches(CanHubSession *session, uint32_t batches, uint32_t node_count)
{
    CanHubTxnOptions options = { .idle_timeout_ms = IDLE_TIMEOUT_MS, .on_roster = printRoster };
    CanHubTxnResponse acknowledgement = {
        .success = { .id = { ACK_ID_BASE, false }, .id_ignored_bits = NODE_BITS, .bytes = { [0] = CANHUB_TXN_EQUALS(ACK_MARKER) } },
        .nodes = nodes,
        .node_count = node_count,
        .timeout_ms = 300,
        .continue_on_timeout = true,
    };
    CanHubTxnReport outcome;
    CanHubTxn *transaction;
    int32_t result = CANHUB_OK;
    uint32_t i;

    for(i=0; i<NODES_MAX; i++) {
        nodes[i] = i;
    }
    transaction = canhub_txn_create(session, &options);
    if (transaction == NULL) {
        fprintf(stderr, "%s\n", canhub_last_error(session));
        return 1;
    }

    for(i=0; i<batches && result == CANHUB_OK; i++) {
        result = sendBatch(transaction, i, &acknowledgement);
    }

    result = canhub_txn_finish(transaction, &outcome);
    return report(result, &outcome, session);
}

static int32_t sendBatch(CanHubTxn *transaction, uint32_t batch, const CanHubTxnResponse *acknowledgement)
{
    uint8_t payload[FRAME_BYTES];
    CanHubTxnMatch where = acknowledgement->success;
    uint16_t sum = 0;
    int32_t result;
    uint32_t frame;
    uint32_t offset;

    for(frame=0; frame<BATCH_FRAMES; frame++) {
        for(offset=0; offset<FRAME_BYTES; offset++) {
            payload[offset] = (uint8_t)(batch + frame + offset);
            sum = (uint16_t)(sum + payload[offset]);
        }
        result = canhub_txn_send(transaction, CANHUB_STANDARD_ID(BATCH_ID), payload, sizeof(payload), NULL);
        if (result != CANHUB_OK) {
            return result;
        }
    }

    where.bytes[1] = (CanHubTxnByte)CANHUB_TXN_EQUALS(sum & BYTE_MASK);
    where.bytes[2] = (CanHubTxnByte)CANHUB_TXN_EQUALS(sum >> BITS_PER_BYTE);
    return canhub_txn_step(transaction, &(CanHubTxnStep){ .response = acknowledgement, .where = &where });
}

static void printRoster(void *context, const CanHubTxnRosterResult *result)
{
    (void)context;

    printf("roster step=%" PRIu32, result->step);
    printMissing(result);
    printf("\n");
    fflush(stdout);
}

static int32_t report(int32_t result, const CanHubTxnReport *outcome, CanHubSession *session)
{
    if (result == CANHUB_OK) {
        printf("done steps=%" PRIu32 "\n", outcome->completed_steps);
        return 0;
    }
    if (result != CANHUB_ERR_TXN_ABORTED) {
        fprintf(stderr, "%s\n", canhub_last_error(session));
        return 1;
    }

    printf("aborted reason=%s step=%" PRIu32, canhub_txn_reason_name(outcome->reason), outcome->failing_step);
    printMissing(&outcome->failing_roster);
    printf("\n");
    return EXIT_ABORTED;
}

static void printMissing(const CanHubTxnRosterResult *result)
{
    uint32_t i;

    if (result->missing_count == 0) {
        return;
    }
    printf(" missing=[");
    for(i=0; i<result->missing_count; i++) {
        printf(i == 0 ? "%" PRIu32 : ", %" PRIu32, result->missing[i]);
    }
    printf("]");
}
