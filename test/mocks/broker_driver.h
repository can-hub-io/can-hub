#pragma once

#include <stdint.h>

#include "hub_transport_port_mock.h"
#include "hub/ports/hub_transport_events.h"
#include "protocol/register_message.h"

/*
 * Test driver: speaks the wire protocol to a Broker through its events so
 * the tests read in domain language. Every helper clears the control
 * messages it produced, leaving the mock log clean for the assertions.
 */
void BrokerDriver_ConnectAgent(
    const HubTransportEvents *events,
    HubTransportPortMock *transport,
    uint32_t peer_id,
    const RegisterMessage *registration
);
void BrokerDriver_ConnectClient(const HubTransportEvents *events, uint32_t peer_id);
void BrokerDriver_ConnectAdmin(const HubTransportEvents *events, uint32_t peer_id);
uint32_t BrokerDriver_InterfaceIdAt(const HubTransportEvents *events, HubTransportPortMock *transport, uint8_t index);
uint8_t BrokerDriver_OpenInterface(
    const HubTransportEvents *events,
    HubTransportPortMock *transport,
    uint32_t peer_id,
    uint32_t interface_id
);
