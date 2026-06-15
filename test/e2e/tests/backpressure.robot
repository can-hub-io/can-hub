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

*** Variables ***
${BURST}    ${256}

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
    Sleep    2s    reason=let the QUIC path drain the queued burst onto the bus

    ${delivered}=    Frames Captured By ${capture}
    Log    delivered: ${delivered}/${BURST}    console=True
    Should Be Equal As Integers    ${delivered}    ${BURST}
