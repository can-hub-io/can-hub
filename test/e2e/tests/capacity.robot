*** Settings ***
Documentation     Peer capacity. The hub used to hold 64 peers in all; past
...               that ceiling every client must still connect, open its
...               channel and get the bus traffic.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Test Cases ***
A Hundred Clients Each Get The Bus Traffic
    ${hub}=    Start Hub With Agent crowd
    ${clients}=    Dump crowd/vcan0 From 100 Clients On ${hub}

    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    123#C0FFEE

    Every Client Of ${clients} Should Receive 123#C0FFEE

*** Keywords ***
Start Hub With Agent ${name}
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_config}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_config}
    ${agent_config}=    Agent Configuration    quic://local:7227    ${name}    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_config}
    Wait Until Agent ${agent} Registered On ${hub}    8
    RETURN    ${hub}

Dump ${interface} From ${count} Clients On ${hub}
    ${config}=    Client Configuration    dump    ${interface}    connect=unix://${hub.unix_socket}
    ${clients}=    Start ${count} CAN Clients On ${LOCAL_SERVER} With ${config}
    Wait Until ${count} Channels Open On ${hub}    30
    RETURN    ${clients}
