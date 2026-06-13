#include "socketcand/socketcand_app.h"

/* ---------- public ---------- */

void SocketcandApp_Init(SocketcandApp *self, TransportPort *hub, SocketcandServerPort *server, const char *device_name, const char *beacon_url, bool beacon_enabled)
{
    SocketcandBridge_Init(&self->bridge, hub, server, device_name, beacon_url, beacon_enabled);
}

void SocketcandApp_SetName(SocketcandApp *self, const char *name)
{
    SocketcandBridge_SetName(&self->bridge, name);
}

TransportEvents SocketcandApp_TransportEvents(SocketcandApp *self)
{
    return SocketcandBridge_TransportEvents(&self->bridge);
}

SocketcandServerEvents SocketcandApp_ServerEvents(SocketcandApp *self)
{
    return SocketcandBridge_ServerEvents(&self->bridge);
}

void SocketcandApp_Tick(SocketcandApp *self, uint64_t now_us)
{
    SocketcandBridge_Tick(&self->bridge, now_us);
}
