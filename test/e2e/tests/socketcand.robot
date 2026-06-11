*** Settings ***
Documentation     Duplicate bindings on one bridge peer (#68). One agent exports a
...               bus; one client (socketcand bridge) holds two socketcand
...               connections to that same bus, i.e. two channel bindings for the
...               same interface on a single hub peer. Every frame from the bus
...               must reach both connections. Without the fix only the
...               first-opened connection receives frames; the second starves.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}

*** Variables ***
${FRAMES}    ${20}

*** Test Cases ***
Two Socketcand Connections To The Same Bus Both Receive Every Frame
    Create VCAN On ${LOCAL_SERVER}    bus0

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    bus0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    ${bridge_cfg}=    Client Configuration    socketcand    connect=quic://local:7227    extra=--no-beacon
    ${bridge}=    Start CAN Client On ${LAN_SERVER} With ${bridge_cfg}
    Sleep    2s    reason=let the bridge connect to the hub and cache the interface list

    ${connections}=    Create List    truck42/bus0    truck42/bus0
    ${consumer}=    Start Draining ${connections} On ${LAN_SERVER}    seconds=10
    Wait Until 2 Channels Open On ${hub}    12

    Send ${FRAMES} Frames On ${LOCAL_SERVER} bus0

    ${counts}=    Drain Result Of ${consumer}
    Log    first connection: ${counts}[truck42/bus0]/${FRAMES}, second connection: ${counts}[truck42/bus0@1]/${FRAMES}    console=True

    Should Be Equal As Integers    ${counts}[truck42/bus0]    ${FRAMES}
    Should Be Equal As Integers    ${counts}[truck42/bus0@1]    ${FRAMES}
