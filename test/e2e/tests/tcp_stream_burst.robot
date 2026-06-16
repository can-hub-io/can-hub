*** Settings ***
Documentation     Stream-framed write burst over TLS. A client bursts a block of
...               frames to a remote bus over the TCP/TLS transport, where the
...               data plane shares the reliable stream and is framed by
...               MessageFramer. The hub used to read the whole socket buffer into
...               the 4 KiB framer before draining, so a burst larger than the
...               framer overflowed MessageFramer_Push and the hub tore the
...               connection down. The framer now drains as it ingests; the whole
...               burst must reach the bus.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${BURST}    ${256}

*** Test Cases ***
TLS Client Write Burst Is Delivered In Full
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    # The TLS client carries a fingerprint, so writes need an explicit grant.
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=tls://127.0.0.1:7227
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    12

    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    Burst ${BURST} Frames On ${LOCAL_SERVER} vcan1
    Wait Until Frames Captured By ${capture} Reaches ${BURST}    timeout=15

    ${delivered}=    Frames Captured By ${capture}
    Log    delivered to bus: ${delivered}/${BURST}    console=True
    Should Be Equal As Integers    ${delivered}    ${BURST}
