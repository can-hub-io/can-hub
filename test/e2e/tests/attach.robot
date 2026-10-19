*** Settings ***
Documentation     attach mirrors a remote bus into a local vcan, creating the
...               vcan when it does not exist yet (the bench runs with
...               CAP_NET_ADMIN), so frames flow both ways with no setup.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${CONNECT}        tcp://local:7228

*** Test Cases ***
Attach Creates A Missing Local VCAN And Mirrors Both Ways
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent_cfg}=    Agent Configuration    tcp://127.0.0.1:7228    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    mirror0    connect=${CONNECT}
    ${client}=    Start CAN Client On ${LAN_SERVER} With ${client_cfg}
    Log Of ${client} Should Contain created vcan mirror0
    Wait Until 1 Channels Open On ${hub}

    ${remote}=    Start Candump On ${LAN_SERVER}    mirror0
    ${local}=    Start Candump On ${LOCAL_SERVER}    vcan0
    Sleep    0.3s    reason=let both candumps bind before the one-shot frames
    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    123#0A0B
    Inject CAN Frame On ${LAN_SERVER}    mirror0    321#0C0D
    Candump ${remote} Should Capture 123#0A0B
    Candump ${local} Should Capture 321#0C0D

Attach Mirrors Bus Error Frames Into The Local VCAN
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent_cfg}=    Agent Configuration    tcp://127.0.0.1:7228    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}

    ${client_cfg}=    Client Configuration    attach    truck42/vcan0    mirror0    connect=${CONNECT}
    ${client}=    Start CAN Client On ${LAN_SERVER} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}

    ${remote}=    Start Candump With Error Frames On ${LAN_SERVER}    mirror0
    Sleep    0.3s    reason=let candump bind before the one-shot frame
    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    20000004#0004000000000000
    Candump ${remote} Should Show 20000004#0004000000000000
