*** Settings ***
Documentation     Own-echo suppression. A socketcand bridge opens with
...               SUPPRESS_OWN_ECHO and both reads and writes on one connection.
...               What that connection writes must reach the bus and a native
...               subscriber, and must not come back to it; traffic from the
...               bus must still reach it, so a silent connection cannot pass
...               for a suppressed echo.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${WRITES}          ${20}
${LAST_WRITE}      0000000000000013
${BUS_FRAMES}      ${5}

*** Test Cases ***
What A Bridge Writes Reaches The Bus And Not Itself
    Create VCAN On ${LOCAL_SERVER}    vcan0

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    edge    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${subscriber_cfg}=    Client Configuration    dump    edge/vcan0    connect=quic://local:7227
    ${subscriber}=    Start CAN Client On ${LOCAL_SERVER} With ${subscriber_cfg}
    Wait Until 1 Channels Open On ${hub}    12

    ${bridge_cfg}=    Client Configuration    socketcand    connect=quic://local:7227    extra=--no-beacon
    ${bridge}=    Start CAN Client On ${LOCAL_SERVER} With ${bridge_cfg}
    Sleep    3s    reason=let the bridge connect and cache the interface list

    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    ${probe}=    Start Echo Probe Of ${WRITES} Frames Through Socketcand On ${LOCAL_SERVER} edge/vcan0
    Wait Until 2 Channels Open On ${hub}    12
    Wait Until Frames Captured By ${capture} Reaches ${WRITES}    timeout=15
    ${on_bus}=    Frames Captured By ${capture}

    Send ${BUS_FRAMES} Frames On ${LOCAL_SERVER} vcan0    can_id=321
    ${result}=    Drain Result Of ${probe}
    Log    on bus: ${on_bus}/${WRITES}, own echo: ${result}[own], from the bus: ${result}[other]/${BUS_FRAMES}    console=True

    Should Be Equal As Integers    ${result}[sent]    ${WRITES}
    Should Be Equal As Integers    ${on_bus}    ${WRITES}
    Client ${subscriber} Should Receive 123#${LAST_WRITE}
    Should Be Equal As Integers    ${result}[own]    ${0}
    Should Be Equal As Integers    ${result}[other]    ${BUS_FRAMES}
