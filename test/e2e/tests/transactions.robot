*** Settings ***
Documentation     Transactions end to end: a python-can-hub client hands a
...               request/response sequence to the hub, the agent runs it at its
...               bus against simulated nodes (ecu_sim.py), and the client gets
...               the outcome. Over the WAN link (50 ms each way) a remote
...               client would pay a round trip per exchange; the transaction
...               pays it once.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench ${BENCH} With The Python Package On ${LOCAL_SERVER}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${TXN}            /work/test/e2e/scripts/txn_run.py
${ECU}            /work/test/e2e/scripts/ecu_sim.py
${PROBE}          /work/test/e2e/scripts/pycan_probe.py
${QUIC}           quic://127.0.0.1:7227
${TLS}            tls://127.0.0.1:7227

*** Test Cases ***
A Remote Request Sequence Runs At Bus Speed Instead Of Link Round Trips
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${WAN_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${WAN_SERVER}    vcan0    uds
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    200    --within    5    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=200

A Negative Response Aborts The Transaction At Its Step
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --negative-at    50
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    100    --state-dir    ${client_state}

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=abort_match step=49

A Response Pending Extends The Wait Beyond The Step Timeout
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --pending    --delay-ms    70
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    5    --timeout-ms    100    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=5

Every Node Acknowledges Each Broadcast Batch
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    8
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    batch    4    8    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=68

A Silent Node Is Named When Its Acknowledgement Never Comes
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    8    --silent    5
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    batch    4    8    --state-dir    ${client_state}

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=timeout step=16 missing=[5]

An Exclusive Transaction Keeps Other Clients Off The Bus
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready
    ${transaction}=    Start Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    300    --exclusive    --state-dir    ${client_state}
    Log Of ${transaction} Should Contain granted

    ${intruder}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    send    1A0    CAFE    --state-dir    ${client_state}

    Should Not Be True    ${intruder.ok}
    Should Contain    ${intruder.stderr}    holds this interface
    Log Of ${transaction} Should Contain done steps=300    timeout=20

A Transaction Runs Over TLS
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}    connect=tls://local:7227
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${TLS}    truck42/vcan0    uds    200    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=200
