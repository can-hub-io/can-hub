#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "hub/domain/rejected_peers.h"
}

#define SECOND_US 1000000ull
#define FINGERPRINT_A "aa00000000000000000000000000000000000000000000000000000000000000"
#define FINGERPRINT_B "bb00000000000000000000000000000000000000000000000000000000000000"

static RejectedPeers rejected;
static AdminRejectedReplyMessage reply;
static HubPeer peer;

static void givenPeer(uint8_t role, const char *fingerprint_hex, const char *origin);
static void reject(const char *name, uint8_t reason, uint64_t now_us);
static void rejectDistinctPeers(uint8_t count, uint64_t first_now_us);
static void list(uint64_t now_us);
static bool listed(const char *fingerprint_hex);

describe("rejected_peers", []() {
    beforeEach([]() {
        RejectedPeers_Reset(&rejected);
        memset(&reply, 0, sizeof(reply));
        givenPeer(kHUB_PEER_ROLE_AGENT, FINGERPRINT_A, "203.0.113.5:40000");
    });

    it("lists a rejected identity with its claimed name and origin", []() {
        reject("truck42", ADMIN_REJECTED_REASON_NOT_ALLOWED, 10 * SECOND_US);

        list(10 * SECOND_US);

        expect(reply.count).toBe((uint8_t)1);
        expect((const char *)reply.entries[0].name).toBe("truck42");
        expect((const char *)reply.entries[0].fingerprint_hex).toBe(FINGERPRINT_A);
        expect((const char *)reply.entries[0].origin).toBe("203.0.113.5:40000");
        expect(reply.entries[0].role).toBe((uint8_t)kHUB_PEER_ROLE_AGENT);
        expect(reply.entries[0].reason).toBe((uint8_t)ADMIN_REJECTED_REASON_NOT_ALLOWED);
    });

    it("counts retries of the same identity in one entry", []() {
        reject("truck42", ADMIN_REJECTED_REASON_NOT_ALLOWED, 10 * SECOND_US);
        reject("truck42", ADMIN_REJECTED_REASON_NOT_ALLOWED, 20 * SECOND_US);

        list(25 * SECOND_US);

        expect(reply.count).toBe((uint8_t)1);
        expect(reply.entries[0].attempts).toBe((uint32_t)2);
        expect(reply.entries[0].last_seen_seconds).toBe((uint32_t)5);
    });

    it("keeps an agent and a client with the same fingerprint apart", []() {
        reject("truck42", ADMIN_REJECTED_REASON_NOT_ALLOWED, 10 * SECOND_US);
        givenPeer(kHUB_PEER_ROLE_CLIENT, FINGERPRINT_A, "203.0.113.5:40001");
        reject("laptop", ADMIN_REJECTED_REASON_NOT_ALLOWED, 10 * SECOND_US);

        list(10 * SECOND_US);

        expect(reply.count).toBe((uint8_t)2);
    });

    it("ignores a peer that carries no fingerprint", []() {
        givenPeer(kHUB_PEER_ROLE_CLIENT, "", "");
        reject("laptop", ADMIN_REJECTED_REASON_NOT_ALLOWED, 10 * SECOND_US);

        list(10 * SECOND_US);

        expect(reply.count).toBe((uint8_t)0);
    });

    it("drops an identity once it is admitted", []() {
        reject("truck42", ADMIN_REJECTED_REASON_NOT_ALLOWED, 10 * SECOND_US);
        RejectedPeers_Forget(&rejected, &peer);

        list(10 * SECOND_US);

        expect(reply.count).toBe((uint8_t)0);
    });

    it("makes room for a new identity by evicting the least recently seen", []() {
        reject("first", ADMIN_REJECTED_REASON_NOT_ALLOWED, 1 * SECOND_US);
        rejectDistinctPeers(REJECTED_PEERS_MAX, 2 * SECOND_US);

        list(100 * SECOND_US);

        expect(listed(FINGERPRINT_A)).toBe(false);
    });

    it("keeps a retrying identity through a flood of one-shot ones", []() {
        reject("truck42", ADMIN_REJECTED_REASON_NOT_ALLOWED, 1 * SECOND_US);
        reject("truck42", ADMIN_REJECTED_REASON_NOT_ALLOWED, 2 * SECOND_US);
        rejectDistinctPeers(REJECTED_PEERS_MAX * 2, 3 * SECOND_US);

        list(100 * SECOND_US);

        expect(listed(FINGERPRINT_A)).toBe(true);
    });

    it("pages a full table across two replies", []() {
        rejectDistinctPeers(REJECTED_PEERS_MAX, 1 * SECOND_US);

        list(10 * SECOND_US);

        expect(reply.count).toBe((uint8_t)ADMIN_REJECTED_REPLY_ENTRIES_MAX);
        expect(reply.flags & ADMIN_REPLY_FLAG_MORE).toBe(ADMIN_REPLY_FLAG_MORE);
    });

    it("skips the entries before the requested offset", []() {
        rejectDistinctPeers(3, 1 * SECOND_US);

        RejectedPeers_List(&rejected, 2, &reply, 10 * SECOND_US);

        expect(reply.count).toBe((uint8_t)1);
    });
});

static void givenPeer(uint8_t role, const char *fingerprint_hex, const char *origin)
{
    memset(&peer, 0, sizeof(peer));
    peer.role = role;
    snprintf(peer.fingerprint_hex, sizeof(peer.fingerprint_hex), "%s", fingerprint_hex);
    snprintf(peer.origin, sizeof(peer.origin), "%s", origin);
}

static void reject(const char *name, uint8_t reason, uint64_t now_us)
{
    Rejection rejection = { &peer, name, reason, now_us };

    RejectedPeers_Record(&rejected, &rejection);
}

static void rejectDistinctPeers(uint8_t count, uint64_t first_now_us)
{
    char fingerprint_hex[IDENTITY_FINGERPRINT_HEX_SIZE];
    uint8_t i;

    for(i=0; i<count; i++) {
        snprintf(fingerprint_hex, sizeof(fingerprint_hex), "%s", FINGERPRINT_B);
        fingerprint_hex[IDENTITY_FINGERPRINT_HEX_SIZE - 2] = (char)('a' + i);
        givenPeer(kHUB_PEER_ROLE_AGENT, fingerprint_hex, "203.0.113.9:40000");
        reject("other", ADMIN_REJECTED_REASON_NOT_ALLOWED, first_now_us + i * SECOND_US);
    }
}

static void list(uint64_t now_us)
{
    RejectedPeers_List(&rejected, 0, &reply, now_us);
}

static bool listed(const char *fingerprint_hex)
{
    uint8_t i;

    for(i=0; i<reply.count; i++) {
        if (strcmp(reply.entries[i].fingerprint_hex, fingerprint_hex) == 0) {
            return true;
        }
    }

    return false;
}
