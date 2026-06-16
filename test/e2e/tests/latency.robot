*** Settings ***
Documentation     End-to-end latency of the live RX path under load. A bus on the
...               agent is streamed with timestamped frames; a remote consumer
...               mirrors that bus and candump records when each frame arrives, so
...               age = receive_time - send_time. At a light rate the age is just
...               the link latency. When the hub->consumer link is the bottleneck
...               and the bus floods past it, the deep egress buffers (the datagram
...               backlog + the per-channel egress queue) fill and stand full:
...               every frame waits behind the queue, so a latest-wins live
...               consumer (the inverter) keeps getting seconds-old frames.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${CAN_ID}    7AA

*** Test Cases ***
Light Live Traffic Arrives Fresh
    [Documentation]    ~200 frames/s, unconstrained link. Age should be ~link
    ...                latency (single-digit to low-tens of ms).
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${LAN_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    10
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://local:7227
    ${client}=    Start CAN Client On ${LAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    ${capture}=    Start Candump On ${LAN_SERVER}    vcan1
    Stream Timestamped Frames On ${LOCAL_SERVER} vcan0    can_id=${CAN_ID}    gap=0.005    duration=${4}
    Sleep    3s    reason=let any standing queue drain so max age is captured

    ${stats}=    Frame Age Stats Of ${capture} For ${CAN_ID}
    Log    light: ${stats}[count] frames, avg=${stats}[avg_ms]ms p95=${stats}[p95_ms]ms max=${stats}[max_ms]ms    console=True
    Should Be True    ${stats}[count] > 0
    Should Be True    ${stats}[p95_ms] < 100    fresh traffic should not be buffered

Flooded Live Traffic Stands In The Egress Queue
    [Documentation]    Bus floods past a 1 mbit hub->consumer link. The deep
    ...                buffers fill and the consumer receives stale frames.
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${LAN_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    10
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://local:7227
    ${client}=    Start CAN Client On ${LAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    Throttle Egress On ${LOCAL_SERVER} To 1mbit
    ${capture}=    Start Candump On ${LAN_SERVER}    vcan1
    Stream Timestamped Frames On ${LOCAL_SERVER} vcan0    can_id=${CAN_ID}    gap=0.0002    duration=${8}
    Sleep    2s    reason=let the standing queue drain so max age is captured

    ${drops}=    Channel Drops On ${hub}
    ${stats}=    Frame Age Stats Of ${capture} For ${CAN_ID}
    Log    flooded: ${stats}[count] frames, avg=${stats}[avg_ms]ms p95=${stats}[p95_ms]ms max=${stats}[max_ms]ms, hub drops=${drops}    console=True
    Should Be True    ${stats}[count] > 0
