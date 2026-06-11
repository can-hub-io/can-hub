#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canhub.h"

#define LIST_INTERFACES_MAX 256
#define RECV_TIMEOUT_MS 1000

static int32_t runList(CanHubSession *session);
static int32_t runDump(CanHubSession *session, const char *interface);
static int32_t runSend(CanHubSession *session, const char *interface, const char *id_text, const char *payload_text);
static bool parsePayload(const char *text, CanHubFrame *frame);
static void printFrame(const CanHubFrame *frame);

int main(int argc, char **argv)
{
    CanHubSession *session;
    CanHubConnectConfig config = { 0 };
    int32_t result;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <url> list | dump <interface> | send <interface> <can-id-hex> <payload-hex>\n", argv[0]);
        return 1;
    }

    config.struct_size = sizeof(config);
    config.url = argv[1];
    config.connect_timeout_ms = -1;

    session = canhub_connect(&config);
    if (session == NULL) {
        fprintf(stderr, "could not connect to %s\n", argv[1]);
        return 1;
    }

    if (strcmp(argv[2], "list") == 0) {
        result = runList(session);
    } else if (strcmp(argv[2], "dump") == 0 && argc >= 4) {
        result = runDump(session, argv[3]);
    } else if (strcmp(argv[2], "send") == 0 && argc >= 6) {
        result = runSend(session, argv[3], argv[4], argv[5]);
    } else {
        fprintf(stderr, "unknown command\n");
        result = 1;
    }

    canhub_close(session);

    return result;
}

static int32_t runList(CanHubSession *session)
{
    static CanHubInterfaceInfo interfaces[LIST_INTERFACES_MAX];
    int32_t count;
    int32_t i;

    count = canhub_list(session, interfaces, LIST_INTERFACES_MAX, -1);
    if (count < 0) {
        fprintf(stderr, "list failed: %s\n", canhub_last_error(session));
        return 1;
    }

    for(i=0; i<count; i++) {
        printf("%u %s/%s\n", interfaces[i].interface_id, interfaces[i].agent, interfaces[i].interface_name);
    }

    return 0;
}

static int32_t runDump(CanHubSession *session, const char *interface)
{
    CanHubFrame frame;
    int32_t result;

    result = canhub_open(session, interface, 0, -1);
    if (result != CANHUB_OK) {
        fprintf(stderr, "open failed: %s\n", canhub_last_error(session));
        return 1;
    }

    fprintf(stderr, "dumping %s\n", interface);
    for (;;) {
        result = canhub_recv(session, &frame, RECV_TIMEOUT_MS);
        if (result == CANHUB_RECEIVED) {
            printFrame(&frame);
        } else if (result != CANHUB_ERR_TIMEOUT) {
            fprintf(stderr, "recv failed: %s\n", canhub_last_error(session));
            return 1;
        }
    }
}

static int32_t runSend(CanHubSession *session, const char *interface, const char *id_text, const char *payload_text)
{
    CanHubFrame frame;
    int32_t result;

    memset(&frame, 0, sizeof(frame));
    frame.can_id = (uint32_t)strtoul(id_text, NULL, 16);
    if (!parsePayload(payload_text, &frame)) {
        fprintf(stderr, "bad payload\n");
        return 1;
    }

    result = canhub_open(session, interface, CANHUB_OPEN_FLAG_WRITE, -1);
    if (result != CANHUB_OK) {
        fprintf(stderr, "open failed: %s\n", canhub_last_error(session));
        return 1;
    }
    result = canhub_send(session, &frame);
    if (result != CANHUB_OK) {
        fprintf(stderr, "send failed: %s\n", canhub_last_error(session));
        return 1;
    }

    return 0;
}

static bool parsePayload(const char *text, CanHubFrame *frame)
{
    char byte_text[3] = { 0, 0, 0 };
    char *byte_end = NULL;
    size_t digits = strlen(text);
    size_t i;

    if (digits % 2 != 0 || digits / 2 > CANHUB_FRAME_PAYLOAD_MAX) {
        return false;
    }

    frame->length = (uint8_t)(digits / 2);
    for(i=0; i<frame->length; i++) {
        byte_text[0] = text[i * 2];
        byte_text[1] = text[i * 2 + 1];
        frame->payload[i] = (uint8_t)strtoul(byte_text, &byte_end, 16);
        if (byte_end != byte_text + 2) {
            return false;
        }
    }

    return true;
}

static void printFrame(const CanHubFrame *frame)
{
    uint8_t i;

    printf(
        "(%llu.%06llu) %03X#",
        (unsigned long long)(frame->timestamp_us / 1000000),
        (unsigned long long)(frame->timestamp_us % 1000000),
        frame->can_id & CANHUB_CAN_ID_MASK
    );
    for(i=0; i<frame->length; i++) {
        printf("%02X", frame->payload[i]);
    }
    printf("\n");
    fflush(stdout);
}
