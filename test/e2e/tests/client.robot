*** Settings ***
Documentation     Client session over a real bus: LIST catalogue, namespaced
...               send onto the vcan, dump filters, and the failure paths.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Start Bus Hub And Agent
Suite Teardown    Teardown Bench    ${BENCH}

*** Variables ***
${CONNECT}        tcp://127.0.0.1:7228

*** Test Cases ***
Client Lists The Registered Interface
    ${rows}=    List Interfaces On ${LOCAL_SERVER}    connect=${CONNECT}
    Length Should Be    ${rows}    1
    Should Be Equal    ${rows[0].agent}    truck42
    Should Be Equal    ${rows[0].interface}    vcan0

Send By Namespaced Name Reaches The Bus
    ${candump}=    Start Candump On ${LOCAL_SERVER}
    ${result}=    Send Frame On ${LOCAL_SERVER}    truck42/vcan0    123#DEADBEEF    connect=${CONNECT}
    Should Be True    ${result.ok}
    Candump ${candump} Should Capture 123#DEADBEEF

Send To An Unknown Interface Fails
    ${result}=    Send Frame On ${LOCAL_SERVER}    truck42/nope    123#11    connect=${CONNECT}
    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    not found

Dump Honours Subscribe Filters
    ${client_cfg}=    Client Configuration    dump    truck42/vcan0    1A0:7FF    connect=${CONNECT}
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until Client ${client} Has Open Channel    ${HUB}
    Sleep    0.3s
    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    123#0BAD
    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    1A0#CAFE
    Client ${client} Should Receive 1A0#CAFE
    Client ${client} Should Not Receive 123#0BAD

*** Keywords ***
Start Bus Hub And Agent
    Setup Bench    ${BENCH}
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    Set Suite Variable    ${HUB}    ${hub}
    ${agent_cfg}=    Agent Configuration    ${CONNECT}    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}
