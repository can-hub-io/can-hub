*** Settings ***
Documentation     libcanhub C ABI driven through the canhub-dump example:
...               list, blocking recv from the bus, send onto the bus.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Start Bus Hub And Agent
Suite Teardown    Teardown Bench    ${BENCH}

*** Variables ***
${CONNECT}        tcp://127.0.0.1:7228

*** Test Cases ***
Library Lists The Catalogue
    ${result}=    Run Binary canhub-dump On ${LOCAL_SERVER}    ${CONNECT}    list
    Should Be True    ${result.ok}
    Should Contain    ${result.stdout}    truck42/vcan0

Library Receives Frames From The Bus
    ${dump}=    Start Binary canhub-dump On ${LOCAL_SERVER}    ${CONNECT}    dump    truck42/vcan0
    Log Of ${dump} Should Contain dumping truck42/vcan0
    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    123#DEADBEEF
    Log Of ${dump} Should Contain 123#DEADBEEF

Library Sends Onto The Bus
    ${candump}=    Start Candump On ${LOCAL_SERVER}
    ${result}=    Run Binary canhub-dump On ${LOCAL_SERVER}    ${CONNECT}    send    truck42/vcan0    1A0    CAFE
    Should Be True    ${result.ok}
    Candump ${candump} Should Capture 1A0#CAFE

Library Reports Unknown Interfaces
    ${result}=    Run Binary canhub-dump On ${LOCAL_SERVER}    ${CONNECT}    dump    truck42/nope
    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    not found

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
