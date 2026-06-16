*** Settings ***
Documentation     Write burst through a socketcand bridge (the firmware-upgrade
...               write pattern the user reported as "solo recibe 1"). A producer
...               bursts a block of frames into the socketcand server; they travel
...               bridge -> hub -> agent -> bus. The socketcand TCP read chunk
...               (2 KiB) used to be pushed whole into the 512 B AsciiFramer, which
...               overflowed and dropped the buffer, so a burst larger than the
...               framer lost almost everything. The framer now ingests
...               incrementally; the whole burst must reach the bus.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${BURST}    ${400}

*** Test Cases ***
Socketcand Write Burst Reaches The Bus In Full
    Create VCAN On ${LOCAL_SERVER}    vcan0

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    edge    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    # The QUIC bridge carries a fingerprint, so writes need an explicit grant.
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${bridge_cfg}=    Client Configuration    socketcand    connect=quic://local:7227    extra=--no-beacon
    ${bridge}=    Start CAN Client On ${LOCAL_SERVER} With ${bridge_cfg}
    Sleep    3s    reason=let the bridge connect and cache the interface list

    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    ${sent}=    Burst ${BURST} Frames Through Socketcand On ${LOCAL_SERVER} edge/vcan0    hold=5
    Should Be Equal As Integers    ${sent}[sent]    ${BURST}

    Wait Until Frames Captured By ${capture} Reaches ${BURST}    timeout=15
    ${delivered}=    Frames Captured By ${capture}
    Log    delivered to bus: ${delivered}/${BURST}    console=True
    Should Be Equal As Integers    ${delivered}    ${BURST}
