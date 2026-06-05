#include "apps/agent/agent_app.h"

/* ---------- public ---------- */

void AgentApp_Init(AgentApp *self, TransportPort *transport, CanPort *can, const RegisterMessage *registration)
{
    Agent_Init(&self->agent, transport, can, registration);
}

TransportEvents AgentApp_TransportEvents(AgentApp *self)
{
    return Agent_TransportEvents(&self->agent);
}

CanEvents AgentApp_CanEvents(AgentApp *self)
{
    return Agent_CanEvents(&self->agent);
}

void AgentApp_Tick(AgentApp *self, uint64_t now_us)
{
    Agent_Tick(&self->agent, now_us);
}
