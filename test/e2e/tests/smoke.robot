*** Settings ***
Documentation     End-to-end smoke: on LOCAL, an agent exports a vcan, a client
...               dumps it, and a frame injected on the bus reaches the client.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}

*** Variables ***
${CONNECT}        tcp://127.0.0.1:7228

*** Test Cases ***
Bench Variables Are Exported
    Should Be Equal    ${LOCAL_SERVER.fqdn}    local
    Should Be Equal As Numbers    ${LOCAL_SERVER.latency_ms}    0
    Should Be Equal As Numbers    ${WAN_SERVER.latency_ms}    50

Agent Frame Reaches Client On Local
    Create VCAN On ${LOCAL_SERVER}    vcan0

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    ${CONNECT}    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}

    ${client_cfg}=    Client Configuration    dump    truck42/vcan0    connect=unix://${hub.unix_socket}
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until Client ${client} Has Open Channel    ${hub}

    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    123#DEADBEEF
    Client ${client} Should Receive 123#DEADBEEF
