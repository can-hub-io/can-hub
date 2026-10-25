*** Settings ***
Documentation     Transaction scripts end to end (doc/transaction-scripts.md):
...               can-hub-client txn and python-can-hub's run_txn_script compile
...               a script, stream its steps and report the agent's outcome,
...               against the simulated nodes of ecu_sim.py.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench ${BENCH} With The Python Package On ${LOCAL_SERVER}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${RUN_SCRIPT}     /work/test/e2e/scripts/txn_script_run.py
${ECU}            /work/test/e2e/scripts/ecu_sim.py
${SCRIPTS}        /work/test/e2e/scripts/txn
${QUIC}           quic://127.0.0.1:7227

*** Test Cases ***
A Script Runs A Request Sequence From The Command Line Across The WAN
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${WAN_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${WAN_SERVER}    vcan0    uds
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Binary can-hub-client On ${LOCAL_SERVER}    --connect    ${QUIC}    --state-dir    ${client_state}    txn    truck42/vcan0    ${SCRIPTS}/uds_requests.txn

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=200

A Response That Compares No Identifier Takes The Answer From Any
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Binary can-hub-client On ${LOCAL_SERVER}    --connect    ${QUIC}    --state-dir    ${client_state}    txn    truck42/vcan0    ${SCRIPTS}/uds_any_id.txn

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=50

A Negative Response Stops A Script At Its Step
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --negative-at    50
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Binary can-hub-client On ${LOCAL_SERVER}    --connect    ${QUIC}    --state-dir    ${client_state}    txn    truck42/vcan0    ${SCRIPTS}/uds_requests.txn

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=failure_matched step=49

A Broken Script Is Refused With Its Line And Nothing Reaches The Agent
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}

    ${result}=    Run Binary can-hub-client On ${LOCAL_SERVER}    --connect    ${QUIC}    --state-dir    ${client_state}    txn    truck42/vcan0    ${SCRIPTS}/unknown_response.txn

    Should Be Equal As Integers    ${result.returncode}    1
    Should Contain    ${result.stderr}    unknown_response.txn:5: no response has this name
    ${status}=    Status Of ${hub}
    Should Be Equal As Integers    ${status.transactions_completed}    0
    Should Be Equal As Integers    ${status.transactions_aborted}    0

Every Node Acknowledges A Firmware Image Broadcast From A Python Script Run
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    8
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${RUN_SCRIPT} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    ${SCRIPTS}/broadcast_firmware.txn    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=272

A Silent Node Is Named When The Step Waiting For It Gives Up
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    8    --silent    5
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Binary can-hub-client On ${LOCAL_SERVER}    --connect    ${QUIC}    --state-dir    ${client_state}    txn    truck42/vcan0    ${SCRIPTS}/broadcast_firmware.txn

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=timeout step=16 completed=16 missing=[5]

A Script That Moves On Without A Silent Node Reports It At Every Step
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    8    --silent    5
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Binary can-hub-client On ${LOCAL_SERVER}    --connect    ${QUIC}    --state-dir    ${client_state}    txn    truck42/vcan0    ${SCRIPTS}/broadcast_firmware_soft.txn

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    roster step=16 missing=[5]
    Should Contain    ${result.stdout}    roster step=271 missing=[5]
    Should Contain    ${result.stdout}    done steps=272

A Python Script Run Reports The Nodes A Moving Step Missed
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    8    --silent    2    6
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${RUN_SCRIPT} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    ${SCRIPTS}/broadcast_firmware_soft.txn    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    roster step=33 missing=[2, 6]
    Should Contain    ${result.stdout}    done steps=272

A Script Given As Text Reads Its Files Against The Base Directory
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    8
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${RUN_SCRIPT} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    ${SCRIPTS}/broadcast_firmware.txn    --text    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=272

Ctrl-C Cancels A Running Script At The Agent
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready
    ${script}=    Start Binary can-hub-client On ${LOCAL_SERVER}    --connect    ${QUIC}    --state-dir    ${client_state}    txn    truck42/vcan0    ${SCRIPTS}/uds_requests.txn
    Log Of ${ecu} Should Contain answered 20

    ${exit_code}=    Interrupt ${script}

    Should Be Equal As Integers    ${exit_code}    2
    Log Of ${script} Should Contain aborted reason=cancelled
    Wait Until ${hub} Relays No Transaction

A Progress Callback Cancels A Python Script Run
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${RUN_SCRIPT} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    ${SCRIPTS}/uds_requests.txn    --cancel-after-steps    20    --state-dir    ${client_state}

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=cancelled
