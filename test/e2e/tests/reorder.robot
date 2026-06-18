*** Settings ***
Documentation     Ordering of a sequential write across the two data planes under
...               link jitter. A client writes frames 0..N-1 in order to a remote
...               bus; the bus must see them in order. The QUIC data plane is
...               unordered datagrams, so jitter delivers them out of order -> the
...               bus is scrambled (a firmware CRC mismatch even with no loss). The
...               TLS data plane is an ordered stream, so TCP reassembles in order.
...               Client on the WAN host (50 ms) with added jitter; the hub keeps
...               per-channel FIFO order, so any reorder is the transport's.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${COUNT}    ${200}

*** Test Cases ***
QUIC Data Plane Scrambles A Sequential Write Under Jitter
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${WAN_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    12
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://local:7227
    ${client}=    Start CAN Client On ${WAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    Set Link Jitter On ${WAN_SERVER} 20ms 15ms
    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    Send ${COUNT} Frames On ${WAN_SERVER} vcan1    gap=0.003
    Wait Until Frames Captured By ${capture} Reaches ${COUNT}    timeout=20

    ${result}=    Sequence Integrity Of ${capture} Over ${COUNT} Frames
    Log    quic: received ${result}[received]/${COUNT}, in_order=${result}[in_order]    console=True
    Should Be True    ${result}[in_order] == False    the unordered datagram plane delivered in order

TLS Data Plane Preserves Order Under Jitter
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${WAN_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    tls://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    12
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=tls://local:7227
    ${client}=    Start CAN Client On ${WAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    Set Link Jitter On ${WAN_SERVER} 20ms 15ms
    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    Send ${COUNT} Frames On ${WAN_SERVER} vcan1    gap=0.003
    Wait Until Frames Captured By ${capture} Reaches ${COUNT}    timeout=20

    ${result}=    Sequence Integrity Of ${capture} Over ${COUNT} Frames
    Log    tls: received ${result}[received]/${COUNT}, in_order=${result}[in_order]    console=True
    Should Be Equal As Integers    ${result}[received]    ${COUNT}
    Should Be True    ${result}[in_order]

QUIC Reliable Plane Preserves Order Under Jitter
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${WAN_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    12
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://local:7227    reliable=${True}
    ${client}=    Start CAN Client On ${WAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    Set Link Jitter On ${WAN_SERVER} 20ms 15ms
    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    Send ${COUNT} Frames On ${WAN_SERVER} vcan1    gap=0.003
    Wait Until Frames Captured By ${capture} Reaches ${COUNT}    timeout=20

    ${result}=    Sequence Integrity Of ${capture} Over ${COUNT} Frames
    Log    quic-reliable: received ${result}[received]/${COUNT}, in_order=${result}[in_order]    console=True
    Should Be Equal As Integers    ${result}[received]    ${COUNT}
    Should Be True    ${result}[in_order]
