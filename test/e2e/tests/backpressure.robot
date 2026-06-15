*** Settings ***
Documentation     Data-plane egress backpressure. A client bursts a block of
...               frames to a remote bus over QUIC (the firmware-upgrade write
...               pattern). Without the transport datagram backlog the QUIC
...               congestion window refuses part of the burst and the client
...               drops it silently; with the backlog the whole block is queued
...               and delivered. The agent's vcan must receive every frame.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${BURST}    ${256}
${SINK_BURST}    ${2000}
${SINK_RATE}    20kbit
${SINK_RATE_BPS}    20000
${SINK_RATE_BPS_HIGH}    40000

*** Test Cases ***
QUIC Client Write Burst Is Delivered In Full
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    # The QUIC client carries a fingerprint, so writes need an explicit grant.
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://127.0.0.1:7227
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    12

    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    Burst ${BURST} Frames On ${LOCAL_SERVER} vcan1
    Wait Until Frames Captured By ${capture} Reaches ${BURST}

    ${delivered}=    Frames Captured By ${capture}
    Log    delivered: ${delivered}/${BURST}    console=True
    Should Be Equal As Integers    ${delivered}    ${BURST}

A Rate-Limited Bus Counts The Silent CAN-TX Drops
    [Documentation]    The bus is a fixed-rate sink: a burst far above the bus
    ...                rate overflows the agent's CAN-tx queue (ENOBUFS). Those
    ...                drops were silent; the agent now counts them per interface
    ...                and reports them, so the hub's interface view shows them.
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://127.0.0.1:7227
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    12

    Shape CAN Sink On ${LOCAL_SERVER} vcan0 To ${SINK_RATE}
    Burst ${SINK_BURST} Frames On ${LOCAL_SERVER} vcan1
    Wait Until TX Dropped On ${hub} For truck42/vcan0 Exceeds 0

Pacing Keeps The Burst Off The Silent CAN-TX Queue
    [Documentation]    Same rate-limited bus, but the agent advertises its rate
    ...                (--pace-rate, since vcan has no bit timing) and the hub
    ...                paces egress to it. The CAN-tx queue stops overflowing, so
    ...                leg-3 drops stay ~0; any shed load is now bounded and
    ...                counted at the hub instead of silently lost at the bus.
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${pace}=    Create List    --pace-rate    ${SINK_RATE_BPS}
    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0    extra=${pace}
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://127.0.0.1:7227
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    12

    Shape CAN Sink On ${LOCAL_SERVER} vcan0 To ${SINK_RATE}
    Burst ${SINK_BURST} Frames On ${LOCAL_SERVER} vcan1
    ${drops}=    Wait Until TX Dropped On ${hub} For truck42/vcan0 Settles
    Log    tx-dropped under pacing: ${drops}    console=True
    Should Be True    ${drops} < 20    pacing did not keep the CAN-tx queue from overflowing

Credit Feedback Reins In A Too-High Pace Rate
    [Documentation]    The agent advertises twice the real bus rate, so pacing
    ...                alone would overrun the CAN-tx queue forever. The agent
    ...                measures its own drops and backs its advertised credit off
    ...                until the hub paces to the real rate, so the drops stop
    ...                climbing. Settling (vs an unbounded climb) is the proof.
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${pace}=    Create List    --pace-rate    ${SINK_RATE_BPS_HIGH}
    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0    extra=${pace}
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://127.0.0.1:7227
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    12

    Shape CAN Sink On ${LOCAL_SERVER} vcan0 To ${SINK_RATE}
    Flood vcan1 On ${LOCAL_SERVER}
    ${settled}=    Wait Until TX Dropped On ${hub} For truck42/vcan0 Stops Climbing
    Log    tx-dropped stopped climbing at ${settled}    console=True
