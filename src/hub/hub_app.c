#include "hub/hub_app.h"

/* ---------- public ---------- */

void HubApp_Init(HubApp *self, HubTransportPort *transport)
{
    Broker_Init(&self->broker, transport);
}

HubTransportEvents HubApp_Events(HubApp *self)
{
    return Broker_Events(&self->broker);
}
