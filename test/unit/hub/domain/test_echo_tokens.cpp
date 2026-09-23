#include <cest>

#include <cstring>

extern "C" {
#include "hub/domain/echo_tokens.h"
}

#define CLIENT_SLOT 4
#define CLIENT_PEER 200
#define OTHER_CLIENT_PEER 201
#define AGENT_SLOT 1
#define OTHER_AGENT_SLOT 2

static EchoTokens tokens;
static uint8_t token;
static uint32_t originator;

static void issueToken(uint8_t client_slot, uint32_t client_peer_id);
static void echoFrom(uint8_t agent_slot);

describe("echo_tokens", []() {
    beforeEach([]() {
        EchoTokens_Reset(&tokens);
        token = FRAME_ROUTE_NO_TOKEN;
        originator = 0;
    });

    it("issues the client slot plus one", []() {
        issueToken(CLIENT_SLOT, CLIENT_PEER);

        expect(token).toBe((uint8_t)(CLIENT_SLOT + 1));
    });

    it("issues no token to the slot the route flags cannot address", []() {
        issueToken(FRAME_ROUTE_TOKEN_VALUES_MAX, CLIENT_PEER);

        expect(token).toBe((uint8_t)FRAME_ROUTE_NO_TOKEN);
    });

    it("attributes an echo from the agent the client injected to", []() {
        issueToken(CLIENT_SLOT, CLIENT_PEER);
        EchoTokens_RecordInjection(&tokens, token, AGENT_SLOT);

        echoFrom(AGENT_SLOT);

        expect(originator).toBe((uint32_t)CLIENT_PEER);
    });

    it("attributes nothing to an echo from an agent the client never injected to", []() {
        issueToken(CLIENT_SLOT, CLIENT_PEER);
        EchoTokens_RecordInjection(&tokens, token, AGENT_SLOT);

        echoFrom(OTHER_AGENT_SLOT);

        expect(originator).toBe((uint32_t)0);
    });

    it("attributes nothing to a frame without the echo flag", []() {
        issueToken(CLIENT_SLOT, CLIENT_PEER);
        EchoTokens_RecordInjection(&tokens, token, AGENT_SLOT);

        originator = EchoTokens_Originator(&tokens, (uint8_t)(token << FRAME_ROUTE_TOKEN_SHIFT), AGENT_SLOT);

        expect(originator).toBe((uint32_t)0);
    });

    it("forgets the agents of the previous client when the slot changes hands", []() {
        issueToken(CLIENT_SLOT, CLIENT_PEER);
        EchoTokens_RecordInjection(&tokens, token, AGENT_SLOT);
        issueToken(CLIENT_SLOT, OTHER_CLIENT_PEER);

        echoFrom(AGENT_SLOT);

        expect(originator).toBe((uint32_t)0);
    });

    it("forgets a released agent, so the next agent in its slot cannot claim the echo", []() {
        issueToken(CLIENT_SLOT, CLIENT_PEER);
        EchoTokens_RecordInjection(&tokens, token, AGENT_SLOT);
        EchoTokens_ReleaseAgent(&tokens, AGENT_SLOT);

        echoFrom(AGENT_SLOT);

        expect(originator).toBe((uint32_t)0);
    });
});

static void issueToken(uint8_t client_slot, uint32_t client_peer_id)
{
    token = EchoTokens_Issue(&tokens, client_slot, client_peer_id);
}

static void echoFrom(uint8_t agent_slot)
{
    uint8_t route_flags = (uint8_t)(FRAME_ROUTE_FLAG_ECHO | (token << FRAME_ROUTE_TOKEN_SHIFT));

    originator = EchoTokens_Originator(&tokens, route_flags, agent_slot);
}
