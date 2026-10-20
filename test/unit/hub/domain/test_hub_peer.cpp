#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "hub/domain/hub_peer.h"
#include "hub/ports/hub_transport_events.h"
#include "protocol/hello_message.h"
}

#define FINGERPRINT "aa11bb22cc33dd44ee55ff66aa77bb88cc99dd00ee11ff22aa33bb44cc55dd66"
#define OTHER_FINGERPRINT "bb11bb22cc33dd44ee55ff66aa77bb88cc99dd00ee11ff22aa33bb44cc55dd66"
#define AGENT_HOST "203.0.113.7"
#define OTHER_HOST "198.51.100.9"

static HubPeer peer;
static HubPeer other;

static void fingerprintAgent(HubPeer *agent, const char *fingerprint);
static void plaintextAgent(HubPeer *agent, uint8_t transport_kind, const char *host);
static void localAgent(HubPeer *agent);
static void fingerprintClient(HubPeer *client);

describe("hub_peer", []() {
    beforeEach([]() {
        memset(&peer, 0, sizeof(peer));
    });

    it("adopts the agent role on a remote peer", []() {
        bool adopted;

        adopted = HubPeer_AdoptRole(&peer, kPEER_ROLE_AGENT);

        expect(adopted).toBe(true);
        expect(peer.role).toBe(kHUB_PEER_ROLE_AGENT);
    });

    it("adopts the client role on a remote peer", []() {
        bool adopted;

        adopted = HubPeer_AdoptRole(&peer, kPEER_ROLE_CLIENT);

        expect(adopted).toBe(true);
        expect(peer.role).toBe(kHUB_PEER_ROLE_CLIENT);
    });

    it("refuses the admin role to a remote peer", []() {
        bool adopted;

        adopted = HubPeer_AdoptRole(&peer, kPEER_ROLE_ADMIN);

        expect(adopted).toBe(false);
        expect(peer.role).toBe(kHUB_PEER_ROLE_UNKNOWN);
    });

    it("adopts the admin role on a local peer", []() {
        bool adopted;

        peer.local = true;

        adopted = HubPeer_AdoptRole(&peer, kPEER_ROLE_ADMIN);

        expect(adopted).toBe(true);
        expect(peer.role).toBe(kHUB_PEER_ROLE_ADMIN);
    });

    it("keeps the registered agent name", []() {
        HubPeer_SetAgentName(&peer, "truck42");

        expect((const char *)peer.agent_name).toBe("truck42");
    });

    it("fills an admin entry with its attributes", []() {
        AdminPeersReplyEntry entry;

        peer.peer_id = 0x80000001;
        peer.role = kHUB_PEER_ROLE_AGENT;
        peer.transport_kind = kPEER_TRANSPORT_QUIC;
        peer.frames_forwarded = 1500;
        peer.frames_dropped = 7;
        peer.connected_at_us = 1000000;
        HubPeer_SetAgentName(&peer, "truck42");
        snprintf(peer.fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE, "%s", FINGERPRINT);
        snprintf(peer.origin, HUB_PEER_ORIGIN_SIZE, "%s", "203.0.113.7:51000");

        HubPeer_FillAdminEntry(&peer, &entry, 91000000);

        expect(entry.peer_id).toBe((uint32_t)0x80000001);
        expect(entry.role).toBe(kHUB_PEER_ROLE_AGENT);
        expect(entry.transport_kind).toBe((uint8_t)kPEER_TRANSPORT_QUIC);
        expect(entry.frames_forwarded).toBe((uint32_t)1500);
        expect(entry.frames_dropped).toBe((uint32_t)7);
        expect(entry.uptime_seconds).toBe((uint32_t)90);
        expect((const char *)entry.agent_name).toBe("truck42");
        expect((const char *)entry.fingerprint_hex).toBe(FINGERPRINT);
        expect((const char *)entry.origin).toBe("203.0.113.7:51000");
    });

    describe("displacement", []() {
        beforeEach([]() {
            memset(&peer, 0, sizeof(peer));
            memset(&other, 0, sizeof(other));
        });

        it("never displaces a local peer", []() {
            localAgent(&peer);

            expect(HubPeer_Displacement(&peer)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_NEVER);
        });

        it("never displaces a plaintext peer", []() {
            plaintextAgent(&peer, kPEER_TRANSPORT_TCP, AGENT_HOST);
            peer.role = kHUB_PEER_ROLE_CLIENT;

            expect(HubPeer_Displacement(&peer)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_NEVER);
        });

        it("displaces first a peer that has not said hello", []() {
            fingerprintAgent(&peer, FINGERPRINT);

            expect(HubPeer_Displacement(&peer)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_UNIDENTIFIED);
        });

        it("displaces first an agent that has not registered", []() {
            fingerprintAgent(&peer, FINGERPRINT);
            peer.role = kHUB_PEER_ROLE_AGENT;

            expect(HubPeer_Displacement(&peer)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_UNIDENTIFIED);
        });

        it("never displaces an agent the admin enrolled", []() {
            fingerprintAgent(&peer, FINGERPRINT);
            peer.role = kHUB_PEER_ROLE_AGENT;
            HubPeer_SetAgentName(&peer, "truck42");
            peer.vouched = true;

            expect(HubPeer_Displacement(&peer)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_NEVER);
        });

        it("displaces an agent pinned on first contact after every unknown client", []() {
            fingerprintAgent(&peer, FINGERPRINT);
            peer.role = kHUB_PEER_ROLE_AGENT;
            HubPeer_SetAgentName(&peer, "truck42");

            expect(HubPeer_Displacement(&peer)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_UNVOUCHED_AGENT);
        });

        it("never displaces a client an ACL names", []() {
            fingerprintClient(&peer);
            peer.vouched = true;

            expect(HubPeer_Displacement(&peer)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_NEVER);
        });

        it("displaces an idle unknown client before one with open channels", []() {
            fingerprintClient(&peer);
            fingerprintClient(&other);
            other.session.bindings[0].in_use = true;

            expect(HubPeer_Displacement(&peer)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_IDLE_CLIENT);
            expect(HubPeer_Displacement(&other)).toBe((uint8_t)kHUB_PEER_DISPLACEMENT_ACTIVE_CLIENT);
        });
    });

    describe("same agent", []() {
        beforeEach([]() {
            memset(&peer, 0, sizeof(peer));
            memset(&other, 0, sizeof(other));
        });

        it("matches agents carrying the same fingerprint", []() {
            fingerprintAgent(&peer, FINGERPRINT);
            fingerprintAgent(&other, FINGERPRINT);

            expect(HubPeer_IsSameAgent(&peer, &other)).toBe(true);
        });

        it("tells apart agents carrying different fingerprints", []() {
            fingerprintAgent(&peer, FINGERPRINT);
            fingerprintAgent(&other, OTHER_FINGERPRINT);

            expect(HubPeer_IsSameAgent(&peer, &other)).toBe(false);
        });

        it("never matches a fingerprinted agent with a plaintext one from its host", []() {
            fingerprintAgent(&peer, FINGERPRINT);
            plaintextAgent(&other, kPEER_TRANSPORT_TCP, AGENT_HOST);
            snprintf(peer.origin_host, HUB_PEER_ORIGIN_SIZE, "%s", AGENT_HOST);

            expect(HubPeer_IsSameAgent(&peer, &other)).toBe(false);
            expect(HubPeer_IsSameAgent(&other, &peer)).toBe(false);
        });

        it("matches plaintext agents reconnecting from the same host", []() {
            plaintextAgent(&peer, kPEER_TRANSPORT_TCP, AGENT_HOST);
            plaintextAgent(&other, kPEER_TRANSPORT_TCP, AGENT_HOST);

            expect(HubPeer_IsSameAgent(&peer, &other)).toBe(true);
        });

        it("tells apart plaintext agents on different hosts", []() {
            plaintextAgent(&peer, kPEER_TRANSPORT_TCP, AGENT_HOST);
            plaintextAgent(&other, kPEER_TRANSPORT_TCP, OTHER_HOST);

            expect(HubPeer_IsSameAgent(&peer, &other)).toBe(false);
        });

        it("tells apart plaintext agents whose host is unknown", []() {
            plaintextAgent(&peer, kPEER_TRANSPORT_TCP, "");
            plaintextAgent(&other, kPEER_TRANSPORT_TCP, "");

            expect(HubPeer_IsSameAgent(&peer, &other)).toBe(false);
        });

        it("matches agents on the local socket", []() {
            localAgent(&peer);
            localAgent(&other);

            expect(HubPeer_IsSameAgent(&peer, &other)).toBe(true);
        });

        it("tells apart a local agent from a plaintext network one", []() {
            localAgent(&peer);
            plaintextAgent(&other, kPEER_TRANSPORT_TCP, AGENT_HOST);

            expect(HubPeer_IsSameAgent(&peer, &other)).toBe(false);
        });
    });
});

static void fingerprintAgent(HubPeer *agent, const char *fingerprint)
{
    agent->transport_kind = kPEER_TRANSPORT_QUIC;
    snprintf(agent->fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE, "%s", fingerprint);
}

static void plaintextAgent(HubPeer *agent, uint8_t transport_kind, const char *host)
{
    agent->transport_kind = transport_kind;
    snprintf(agent->origin_host, HUB_PEER_ORIGIN_SIZE, "%s", host);
}

static void localAgent(HubPeer *agent)
{
    agent->transport_kind = kPEER_TRANSPORT_UNIX;
    agent->local = true;
}

static void fingerprintClient(HubPeer *client)
{
    fingerprintAgent(client, FINGERPRINT);
    client->role = kHUB_PEER_ROLE_CLIENT;
}
