#include <cest>

#include <cstring>

extern "C" {
#include "hub/domain/hub_peer.h"
#include "protocol/hello_message.h"
}

#define FINGERPRINT "aa11bb22cc33dd44ee55ff66aa77bb88cc99dd00ee11ff22aa33bb44cc55dd66"

static HubPeer peer;

describe("hub_peer", []() {
    beforeEach([]() {
        memset(&peer, 0, sizeof(peer));
    });

    it("adopts the agent and client roles regardless of locality", []() {
        expect(HubPeer_AdoptRole(&peer, kPEER_ROLE_AGENT)).toBe(true);
        expect(peer.role).toBe(kHUB_PEER_ROLE_AGENT);

        peer.role = kHUB_PEER_ROLE_UNKNOWN;

        expect(HubPeer_AdoptRole(&peer, kPEER_ROLE_CLIENT)).toBe(true);
        expect(peer.role).toBe(kHUB_PEER_ROLE_CLIENT);
    });

    it("adopts the admin role only for local peers", []() {
        expect(HubPeer_AdoptRole(&peer, kPEER_ROLE_ADMIN)).toBe(false);
        expect(peer.role).toBe(kHUB_PEER_ROLE_UNKNOWN);

        peer.local = true;

        expect(HubPeer_AdoptRole(&peer, kPEER_ROLE_ADMIN)).toBe(true);
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
        HubPeer_SetAgentName(&peer, "truck42");
        strncpy(peer.fingerprint_hex, FINGERPRINT, IDENTITY_FINGERPRINT_HEX_SIZE - 1);

        HubPeer_FillAdminEntry(&peer, &entry);

        expect(entry.peer_id).toBe((uint32_t)0x80000001);
        expect(entry.role).toBe(kHUB_PEER_ROLE_AGENT);
        expect((const char *)entry.agent_name).toBe("truck42");
        expect((const char *)entry.fingerprint_hex).toBe(FINGERPRINT);
    });
});
