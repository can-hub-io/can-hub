*** Settings ***
Documentation     Reliable subscribers are isolated from each other. A reliable
...               subscriber that stops reading cannot hold back the bus for the
...               rest: the hub keeps relaying to everyone with room and drops the
...               stalled one, so the others keep their latency. And a reliable
...               channel reopened while the agent still closes the old stream
...               waits for it, so frames from the two streams never interleave.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${CAN_ID}          7AA
${COUNT}           ${200}
${AGENT_DELAY}     300ms

*** Test Cases ***
A Stalled Reliable Subscriber Does Not Hold Back The Others
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1
    Create VCAN On ${LOCAL_SERVER}    vcan2
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8
    Run CLI On Hub ${hub}    acl    add    *    */*    rw
    ${stalled_cfg}=    Client Configuration    dump    truck42/vcan0    connect=quic://127.0.0.1:7227    reliable=${True}
    ${stalled}=    Start CAN Client On ${LOCAL_SERVER} With ${stalled_cfg}
    ${reliable_cfg}=    Client Configuration    attach    truck42/vcan0    vcan1    connect=quic://127.0.0.1:7227    reliable=${True}
    ${reliable}=    Start CAN Client On ${LOCAL_SERVER} With ${reliable_cfg}
    ${lossy_cfg}=    Client Configuration    attach    truck42/vcan0    vcan2    connect=quic://127.0.0.1:7227
    ${lossy}=    Start CAN Client On ${LOCAL_SERVER} With ${lossy_cfg}
    Wait Until 3 Channels Open On ${hub}    15
    ${reliable_capture}=    Start Candump On ${LOCAL_SERVER}    vcan1
    ${lossy_capture}=    Start Candump On ${LOCAL_SERVER}    vcan2
    Pause CAN Client ${stalled}

    ${sent}=    Stream Timestamped Frames On ${LOCAL_SERVER} vcan0    can_id=${CAN_ID}    gap=0.001    duration=${4}
    Wait Until Open Channels On ${hub} Drop To 2
    Wait Until Frames Captured By ${reliable_capture} Reaches ${sent}
    ${reliable_stats}=    Frame Age Stats Of ${reliable_capture} For ${CAN_ID}
    ${lossy_stats}=    Frame Age Stats Of ${lossy_capture} For ${CAN_ID}
    Log    reliable: ${reliable_stats}[count]/${sent} avg=${reliable_stats}[avg_ms]ms p95=${reliable_stats}[p95_ms]ms max=${reliable_stats}[max_ms]ms    console=True
    Log    lossy: ${lossy_stats}[count]/${sent} avg=${lossy_stats}[avg_ms]ms p95=${lossy_stats}[p95_ms]ms max=${lossy_stats}[max_ms]ms    console=True

    Should Be Equal As Integers    ${reliable_stats}[count]    ${sent}
    Should Be True    ${reliable_stats}[p95_ms] < 10    the stalled subscriber held back a reliable one
    Should Be True    ${lossy_stats}[p95_ms] < 10    the stalled subscriber held back a lossy one

A Reliable Channel Reopened While The Agent Closes The Old Stream Keeps Order
    Create VCAN On ${WAN_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent_cfg}=    Agent Configuration    quic://local:7227    van7    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    12
    Run CLI On Hub ${hub}    acl    add    *    */*    rw
    ${agent_peer}=    Peer Id Of Agent ${agent} On ${hub}
    ${opened}=    Set Variable    peer ${agent_peer} reliable channel 0 opened
    ${deferred}=    Set Variable    peer ${agent_peer} reliable channel 0 reopens once its previous stream closes
    Set Link Jitter On ${WAN_SERVER} ${AGENT_DELAY} 1ms
    ${first_cfg}=    Client Configuration    dump    van7/vcan0    connect=quic://127.0.0.1:7227    reliable=${True}
    ${first}=    Start CAN Client On ${LOCAL_SERVER} With ${first_cfg}
    Wait Until Hub ${hub} Logged ${opened} 1 Times

    Stop CAN Client ${first}
    ${second_cfg}=    Client Configuration    attach    van7/vcan0    vcan1    connect=quic://127.0.0.1:7227    reliable=${True}
    ${second}=    Start CAN Client On ${LOCAL_SERVER} With ${second_cfg}
    Wait Until Hub ${hub} Logged ${deferred} 1 Times
    Wait Until Hub ${hub} Logged ${opened} 2 Times
    ${capture}=    Start Candump On ${LOCAL_SERVER}    vcan1
    Send ${COUNT} Frames On ${WAN_SERVER} vcan0    gap=0.003
    Wait Until Frames Captured By ${capture} Reaches ${COUNT}    timeout=20
    ${result}=    Sequence Integrity Of ${capture} Over ${COUNT} Frames
    Log    reopened: received ${result}[received]/${COUNT}, in_order=${result}[in_order]    console=True

    Should Be True    ${result}[complete]
    Should Be True    ${result}[in_order]
