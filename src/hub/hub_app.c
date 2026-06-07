#include "hub/hub_app.h"

/* ---------- public ---------- */

void HubApp_Init(
    HubApp *self,
    HubTransportPort *transport,
    IdentityStorePort *identity_store,
    AuthorizationPort *authorization,
    bool require_known_agents
)
{
    Broker_Init(&self->broker, transport, identity_store, authorization, require_known_agents);
}

HubTransportEvents HubApp_Events(HubApp *self)
{
    return Broker_Events(&self->broker);
}

void HubApp_Tick(HubApp *self, uint64_t now_us)
{
    Broker_Tick(&self->broker, now_us);
}
