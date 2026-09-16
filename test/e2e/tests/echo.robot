*** Settings ***
Documentation     Own-echo suppression. A socketcand bridge opens with
...               SUPPRESS_OWN_ECHO and both reads and writes on one connection,
...               so it is the only component that can exercise the path: what it
...               writes must reach the bus and every other subscriber, and must
...               not come back to the bridge as an echo.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${WRITES}    ${20}

*** Test Cases ***
What A Bridge Writes Reaches The Bus And Not Itself
    Create VCAN On ${LOCAL_SERVER}    vcan0

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    edge    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${bridge_cfg}=    Client Configuration    socketcand    connect=quic://local:7227    extra=--no-beacon
    ${bridge}=    Start CAN Client On ${LOCAL_SERVER} With ${bridge_cfg}
    Sleep    3s    reason=let the bridge connect and cache the interface list

    ${connections}=    Create List    edge/vcan0
    ${consumer}=    Start Draining ${connections} On ${LOCAL_SERVER}    seconds=12
    Wait Until 1 Channels Open On ${hub}    12

    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    ${sent}=    Burst ${WRITES} Frames Through Socketcand On ${LOCAL_SERVER} edge/vcan0    hold=5
    Should Be Equal As Integers    ${sent}[sent]    ${WRITES}

    Wait Until Frames Captured By ${capture} Reaches ${WRITES}    timeout=15
    ${on_bus}=    Frames Captured By ${capture}
    ${counts}=    Drain Result Of ${consumer}
    Log    on bus: ${on_bus}/${WRITES}, echoed back to the writer: ${counts}[edge/vcan0]    console=True

    Should Be Equal As Integers    ${on_bus}    ${WRITES}
    Should Be Equal As Integers    ${counts}[edge/vcan0]    ${0}
