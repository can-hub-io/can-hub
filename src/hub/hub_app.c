#include "hub/hub_app.h"

/* ---------- public ---------- */

void HubApp_Init(HubApp *self, HubTransportPort *transport, IdentityStorePort *identity_store)
{
    Broker_Init(&self->broker, transport, identity_store);
}

HubTransportEvents HubApp_Events(HubApp *self)
{
    return Broker_Events(&self->broker);
}

void HubApp_Tick(HubApp *self, uint64_t now_us)
{
    Broker_Tick(&self->broker, now_us);
}
