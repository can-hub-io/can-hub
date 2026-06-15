*** Settings ***
Documentation     python-can backend (python-can-hub) over libcanhub: install
...               the package, recv from the real bus, send onto it.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Start Bus Hub Agent And Package
Suite Teardown    Teardown Bench    ${BENCH}

*** Variables ***
${CONNECT}        tcp://127.0.0.1:7228
${TLS_CONNECT}    tls://127.0.0.1:7227
${PROBE}          /work/test/e2e/scripts/pycan_probe.py
${WRONG_FINGERPRINT}    deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef

*** Test Cases ***
Python Bus Receives Frames From The Bus
    ${dump}=    Start Python ${PROBE} On ${LOCAL_SERVER}    ${CONNECT}    truck42/vcan0    dump
    Log Of ${dump} Should Contain dumping truck42/vcan0
    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    123#DEADBEEF
    Log Of ${dump} Should Contain 123#DEADBEEF

Python Bus Sends Onto The Bus
    ${candump}=    Start Candump On ${LOCAL_SERVER}
    ${result}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${CONNECT}    truck42/vcan0    send    1A0    CAFE
    Should Be True    ${result.ok}
    Candump ${candump} Should Capture 1A0#CAFE

Python Bus Lists The Interfaces The Hub Exports
    ${result}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${CONNECT}    truck42/vcan0    list
    Should Be True    ${result.ok}
    Should Contain    ${result.stdout}    canhub truck42/vcan0

Python Can Discovery Finds The Hub Interfaces From The Environment
    ${result}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${CONNECT}    truck42/vcan0    detect
    Should Be True    ${result.ok}
    Should Contain    ${result.stdout}    canhub truck42/vcan0

Python Bus Refuses Unknown Interfaces
    ${result}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${CONNECT}    truck42/nope    dump
    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    not found

Python Bus Connects Over TLS With The Expected Hub Fingerprint
    ${fingerprint}=    Fingerprint Of ${HUB.config.state_dir}/hub.crt On ${LOCAL_SERVER}
    ${candump}=    Start Candump On ${LOCAL_SERVER}
    ${result}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${TLS_CONNECT}    truck42/vcan0    send    1B0    BEBE
    ...    --state-dir    /tmp/pycan-identity    --hub-fingerprint    ${fingerprint}
    Should Be True    ${result.ok}
    Candump ${candump} Should Capture 1B0#BEBE

Python Bus Rejects A Hub With The Wrong Fingerprint
    ${result}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${TLS_CONNECT}    truck42/vcan0    send    1B0    BEBE
    ...    --state-dir    /tmp/pycan-identity    --hub-fingerprint    ${WRONG_FINGERPRINT}
    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    could not connect

Python Bus Uses An Explicitly Injected Identity
    ${candump}=    Start Candump On ${LOCAL_SERVER}
    ${result}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${TLS_CONNECT}    truck42/vcan0    send    1C0    CACA
    ...    --cert    /tmp/pycan-identity/client.crt    --key    /tmp/pycan-identity/client.key
    Should Be True    ${result.ok}
    Candump ${candump} Should Capture 1C0#CACA

*** Keywords ***
Start Bus Hub Agent And Package
    Setup Bench    ${BENCH}
    Install Python Package On ${LOCAL_SERVER}    /work/python
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    Set Suite Variable    ${HUB}    ${hub}
    ${agent_cfg}=    Agent Configuration    ${CONNECT}    truck42    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}
    Run CLI On Hub ${hub}    acl    add    *    */*    rw
