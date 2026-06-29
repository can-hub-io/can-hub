*** Settings ***
Documentation     A client keeps receiving after its agent flaps. The agent
...               loses its uplink long enough for the hub to drop it, then
...               reconnects and re-registers; the hub must re-point the client's
...               channel at the agent's new interface id so traffic resumes —
...               instead of silently stranding the still-connected client.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Test Cases ***
Client Keeps Receiving After Its Agent Reconnects
    Create VCAN On ${WAN_SERVER}    vcan0

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    reattach-agent    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}

    ${client_cfg}=    Client Configuration    dump    reattach-agent/vcan0    connect=unix://${hub.unix_socket}
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until Client ${client} Has Open Channel    ${hub}

    Inject CAN Frame On ${WAN_SERVER}    vcan0    123#DEADBEEF
    Client ${client} Should Receive 123#DEADBEEF

    Set Link Loss On ${WAN_SERVER} To 100
    Sleep    12s
    Set Link Loss On ${WAN_SERVER} To 0

    Wait Until Agent ${agent} Registered On ${hub}    20
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Client ${client} Should Receive 321#CAFEBABE    15
