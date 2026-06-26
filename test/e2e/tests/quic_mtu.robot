*** Settings ***
Documentation     An agent on a sub-1500 MTU link (1450, common on tunnels and
...               embedded NICs) must still complete the QUIC handshake: the
...               connection starts at the conservative 1200-byte size and PMTUD
...               grows it, instead of blackholing every full-MSS packet.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Test Cases ***
Agent Connects Over QUIC On A 1450 MTU Link
    Create VCAN On ${WAN_SERVER}    vcan0
    Set Link MTU On ${WAN_SERVER} To 1450

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    mtu-agent    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    15

    ${client_cfg}=    Client Configuration    dump    mtu-agent/vcan0    connect=unix://${hub.unix_socket}
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until Client ${client} Has Open Channel    ${hub}

    Inject CAN Frame On ${WAN_SERVER}    vcan0    123#DEADBEEF
    Client ${client} Should Receive 123#DEADBEEF
