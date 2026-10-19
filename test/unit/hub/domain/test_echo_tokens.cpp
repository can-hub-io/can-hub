#include <cest>

#include <cstring>

extern "C" {
#include "hub/domain/echo_tokens.h"
}

#define CLIENT_PEER 200
#define OTHER_CLIENT_PEER 201
#define FIRST_FLOOD_PEER 1000

static EchoTokens tokens;
static uint8_t token;
static uint32_t originator;

static void issueToken(uint32_t client_peer_id);
static void issueTokensToDistinctClients(uint8_t count);
static void echoOf(uint8_t echoed_token);

describe("echo_tokens", []() {
    beforeEach([]() {
        EchoTokens_Reset(&tokens);
        token = FRAME_ROUTE_NO_TOKEN;
        originator = 0;
    });

    it("attributes an echo to the client its token was issued to", []() {
        issueToken(CLIENT_PEER);

        echoOf(token);

        expect(originator).toBe((uint32_t)CLIENT_PEER);
    });

    it("issues the same token to a client on every injection", []() {
        uint8_t first_token;

        issueToken(CLIENT_PEER);
        first_token = token;

        issueToken(CLIENT_PEER);

        expect(token).toBe(first_token);
    });

    it("issues distinct tokens to distinct clients", []() {
        uint8_t first_token;

        issueToken(CLIENT_PEER);
        first_token = token;

        issueToken(OTHER_CLIENT_PEER);

        expect(token == first_token).toBe(false);
    });

    it("attributes nothing to a token it never issued", []() {
        issueToken(CLIENT_PEER);

        echoOf((uint8_t)(token + 1));

        expect(originator).toBe((uint32_t)0);
    });

    it("attributes nothing to a frame without the echo flag", []() {
        issueToken(CLIENT_PEER);

        originator = EchoTokens_Originator(&tokens, (uint8_t)(token << FRAME_ROUTE_TOKEN_SHIFT));

        expect(originator).toBe((uint32_t)0);
    });

    it("forgets a released client", []() {
        issueToken(CLIENT_PEER);
        EchoTokens_ReleaseClient(&tokens, CLIENT_PEER);

        echoOf(token);

        expect(originator).toBe((uint32_t)0);
    });

    it("hands a released token to a new client only after the free ones", []() {
        uint8_t released_token;

        issueToken(CLIENT_PEER);
        released_token = token;
        EchoTokens_ReleaseClient(&tokens, CLIENT_PEER);

        issueToken(OTHER_CLIENT_PEER);

        expect(token == released_token).toBe(false);
    });

    it("issues no token once every value the route flags address is taken", []() {
        issueTokensToDistinctClients(FRAME_ROUTE_TOKEN_VALUES_MAX);

        issueToken(CLIENT_PEER);

        expect(token).toBe((uint8_t)FRAME_ROUTE_NO_TOKEN);
    });

    it("issues a token again once a writer of a full table leaves", []() {
        issueTokensToDistinctClients(FRAME_ROUTE_TOKEN_VALUES_MAX);
        EchoTokens_ReleaseClient(&tokens, FIRST_FLOOD_PEER);

        issueToken(CLIENT_PEER);
        echoOf(token);

        expect(originator).toBe((uint32_t)CLIENT_PEER);
    });
});

static void issueToken(uint32_t client_peer_id)
{
    token = EchoTokens_Issue(&tokens, client_peer_id);
}

static void issueTokensToDistinctClients(uint8_t count)
{
    uint8_t i;

    for(i=0; i<count; i++) {
        EchoTokens_Issue(&tokens, FIRST_FLOOD_PEER + i);
    }
}

static void echoOf(uint8_t echoed_token)
{
    uint8_t route_flags = (uint8_t)(FRAME_ROUTE_FLAG_ECHO | (echoed_token << FRAME_ROUTE_TOKEN_SHIFT));

    originator = EchoTokens_Originator(&tokens, route_flags);
}
