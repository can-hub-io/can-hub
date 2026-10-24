*** Settings ***
Documentation     libcanhub C ABI driven through the canhub-dump example:
...               list, blocking recv from the bus, send onto the bus, and
...               the connect failures reported through canhub_last_error(NULL),
...               a silent resolver included: it never outlasts the connect timeout;
...               and its transaction API through the canhub-txn example, against
...               the simulated nodes of ecu_sim.py.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Start Bus Hub And Agent
Suite Teardown    Teardown Bench    ${BENCH}

*** Variables ***
${CONNECT}        tcp://127.0.0.1:7228
${ECU}            /work/test/e2e/scripts/ecu_sim.py

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

Library Explains A Failed Connection
    ${result}=    Run Binary canhub-dump On ${LOCAL_SERVER}    tcp://127.0.0.1:7    list

    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    the hub refused or closed the connection

Library Explains A Hub Host That Does Not Resolve
    ${result}=    Run Binary canhub-dump On ${LOCAL_SERVER}    tcp://hub.invalid:7228    list

    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    could not resolve the hub host

Library Gives Up On A Silent Resolver Within Its Connect Timeout
    [Setup]    Silence DNS On ${LAN_SERVER}
    [Teardown]    Restore DNS On ${LAN_SERVER}
    ${result}=    Run Binary canhub-dump On ${LAN_SERVER}    tcp://hub.unanswered:7228    list

    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    timed out resolving the hub host

Library Explains A Malformed Url
    ${result}=    Run Binary canhub-dump On ${LOCAL_SERVER}    not-a-url    list

    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    could not parse the hub url

Library Runs A Request Sequence As A Transaction
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Binary canhub-txn On ${LOCAL_SERVER}    ${CONNECT}    truck42/vcan0    uds    50

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=50
    [Teardown]    Kill ${ecu}

Library Reports The Failure That Stopped A Transaction
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --negative-at    10
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Binary canhub-txn On ${LOCAL_SERVER}    ${CONNECT}    truck42/vcan0    uds    50

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=failure_matched step=9
    [Teardown]    Kill ${ecu}

Library Reports Every Batch A Silent Node Missed
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    8    --silent    3
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Binary canhub-txn On ${LOCAL_SERVER}    ${CONNECT}    truck42/vcan0    batch    3    8

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    roster step=16 missing=[3]
    Should Contain    ${result.stdout}    roster step=50 missing=[3]
    Should Contain    ${result.stdout}    done steps=51
    [Teardown]    Kill ${ecu}

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
