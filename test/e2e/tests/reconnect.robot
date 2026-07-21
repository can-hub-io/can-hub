*** Settings ***
Documentation     Agent on a WAN link loses its uplink (100%% loss) while a
...               reliable client is attached, then the link is restored. The
...               agent must detect the dead QUIC connection and reconnect.
Library           BenchKeywords
Library           BuiltIn
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Test Cases ***
Agent Reconnects After WAN Uplink Drop
    Create VCAN On ${WAN_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}

    ${client_cfg}=    Client Configuration    dump    truck42/vcan0    connect=unix://${hub.unix_socket}    reliable=${True}
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until Client ${client} Has Open Channel    ${hub}

    Inject CAN Frame On ${WAN_SERVER}    vcan0    123#DEADBEEF
    Client ${client} Should Receive 123#DEADBEEF

    Set Link Loss On ${WAN_SERVER} To 100
    Sleep    40s
    Set Link Loss On ${WAN_SERVER} To 0

    Log Of ${agent.process} Should Contain reconnected to hub    120

Agent Reconnects After Uplink Drop With Datagrams Queued
    Create VCAN On ${WAN_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan1

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    truck43    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}

    ${client_cfg}=    Client Configuration    dump    truck43/vcan0    connect=unix://${hub.unix_socket}    reliable=${False}
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until Client ${client} Has Open Channel    ${hub}

    Inject CAN Frame On ${WAN_SERVER}    vcan0    123#DEADBEEF
    Client ${client} Should Receive 123#DEADBEEF

    Set Link Loss On ${WAN_SERVER} To 100
    Burst 500 Frames On ${WAN_SERVER} vcan0
    Sleep    40s
    Set Link Loss On ${WAN_SERVER} To 0

    Log Of ${agent.process} Should Contain reconnected to hub    120

Agent Reconnects After WAN Uplink Drop And IP Change
    Create VCAN On ${WAN_SERVER}    vcan0

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    truck42    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}

    ${client_cfg}=    Client Configuration    dump    truck42/vcan0    connect=unix://${hub.unix_socket}    reliable=${True}
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until Client ${client} Has Open Channel    ${hub}

    Inject CAN Frame On ${WAN_SERVER}    vcan0    123#DEADBEEF
    Client ${client} Should Receive 123#DEADBEEF

    Bring Link Down On ${WAN_SERVER}
    Sleep    15s
    Bring Link Up On ${WAN_SERVER} With Octet 40

    Log Of ${agent.process} Should Contain reconnected to hub    150
