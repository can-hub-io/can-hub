#include "hub/hub_app.h"

/* ---------- public ---------- */

void HubApp_Init(
    HubApp *self,
    HubTransportPort *transport,
    IdentityStorePort *identity_store,
    AuthorizationPort *authorization,
    BridgeStorePort *bridge_store,
    bool require_known_peers
)
{
    Broker_Init(&self->broker, transport, identity_store, authorization, bridge_store, require_known_peers);
}

HubTransportEvents HubApp_Events(HubApp *self)
{
    return Broker_Events(&self->broker);
}

void HubApp_Tick(HubApp *self, uint64_t now_us)
{
    Broker_Tick(&self->broker, now_us);
}

int32_t HubApp_NextTimeoutMs(HubApp *self, int32_t cap_ms)
{
    return Broker_NextTimeoutMs(&self->broker, cap_ms);
}
