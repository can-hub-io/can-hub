*** Settings ***
Documentation     Reliability of a sequential bulk transfer (the firmware-upgrade
...               write pattern) across the two data planes, over a lossy link.
...               A client writes frames 0..N-1 in order to a remote bus; the bus
...               must receive every one, in order. The QUIC data plane is
...               unreliable datagrams (latest-wins, no retransmit), so link loss
...               drops/reorders frames -> a firmware CRC mismatch. The TLS data
...               plane shares the reliable ordered stream, so TCP retransmits and
...               the transfer arrives intact. Agent and client sit on the WAN host
...               (50 ms, behind NAT) with injected packet loss; setup completes
...               before loss is applied so registration/open are not disturbed.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${COUNT}    ${400}
${LOSS}     ${5}

*** Test Cases ***
QUIC Data Plane Corrupts A Sequential Transfer Under Link Loss
    Create VCAN On ${WAN_SERVER}    vcan0
    Create VCAN On ${WAN_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    edge    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    12
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    edge/vcan0    vcan1    connect=quic://local:7227
    ${client}=    Start CAN Client On ${WAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    Set Link Loss On ${WAN_SERVER} To ${LOSS}
    ${capture}=    Start Candump On ${WAN_SERVER}    vcan0
    Send ${COUNT} Frames On ${WAN_SERVER} vcan1    gap=0.01
    Sleep    3s    reason=let the last datagrams drain over the lossy link

    ${result}=    Sequence Integrity Of ${capture} Over ${COUNT} Frames
    Log    quic: received ${result}[received]/${COUNT}, in_order=${result}[in_order], complete=${result}[complete]    console=True
    Should Be True    ${result}[complete] == False    the lossy datagram plane delivered the transfer intact

TLS Data Plane Delivers The Sequential Transfer Intact Under Link Loss
    Create VCAN On ${WAN_SERVER}    vcan0
    Create VCAN On ${WAN_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    tls://local:7227    edge    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    12
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    edge/vcan0    vcan1    connect=tls://local:7227
    ${client}=    Start CAN Client On ${WAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    Set Link Loss On ${WAN_SERVER} To ${LOSS}
    ${capture}=    Start Candump On ${WAN_SERVER}    vcan0
    Send ${COUNT} Frames On ${WAN_SERVER} vcan1    gap=0.01
    Wait Until Frames Captured By ${capture} Reaches ${COUNT}    timeout=20

    ${result}=    Sequence Integrity Of ${capture} Over ${COUNT} Frames
    Log    tls: received ${result}[received]/${COUNT}, in_order=${result}[in_order], complete=${result}[complete]    console=True
    Should Be Equal As Integers    ${result}[received]    ${COUNT}
    Should Be True    ${result}[in_order]
    Should Be True    ${result}[complete]

QUIC Reliable Plane Delivers The Sequential Transfer Intact Under Link Loss
    Create VCAN On ${WAN_SERVER}    vcan0
    Create VCAN On ${WAN_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    edge    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    12
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    edge/vcan0    vcan1    connect=quic://local:7227    reliable=${True}
    ${client}=    Start CAN Client On ${WAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    Set Link Loss On ${WAN_SERVER} To ${LOSS}
    ${capture}=    Start Candump On ${WAN_SERVER}    vcan0
    Send ${COUNT} Frames On ${WAN_SERVER} vcan1    gap=0.01
    Wait Until Frames Captured By ${capture} Reaches ${COUNT}    timeout=20

    ${result}=    Sequence Integrity Of ${capture} Over ${COUNT} Frames
    Log    quic-reliable: received ${result}[received]/${COUNT}, in_order=${result}[in_order], complete=${result}[complete]    console=True
    Should Be Equal As Integers    ${result}[received]    ${COUNT}
    Should Be True    ${result}[in_order]
    Should Be True    ${result}[complete]
