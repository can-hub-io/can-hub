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

A Firmware-Sized Transfer Streams Through The Window And Around The Agent's Ring
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    1
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    batch    1200    1    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=20400
    ${status}=    Status Of ${hub}
    Should Be Equal As Integers    ${status.transactions_completed}    1
    Should Be Equal As Integers    ${status.transaction_steps}    20400

A Transfer Over A Lossy WAN Link Arrives Whole And In Order
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${WAN_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${WAN_SERVER}    vcan0    nodes    1
    Log Of ${ecu} Should Contain ready
    Set Link Loss On ${WAN_SERVER} To 5 Keeping Its Latency

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    batch    400    1    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=6800

A Client Lost Mid-Transaction Frees The Agent For The Next One
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready
    ${transaction}=    Start Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    300    --exclusive    --state-dir    ${client_state}
    Log Of ${transaction} Should Contain granted
    Kill ${transaction}
    Wait Until ${hub} Relays No Transaction

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    10    --state-dir    ${client_state}

    Should Be True    ${result.ok}    ${result.stdout}${result.stderr}
    Should Contain    ${result.stdout}    done steps=10

A Lost Agent Ends The Client's Transaction
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    ${agent}=    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready
    ${transaction}=    Start Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    300    --state-dir    ${client_state}
    Log Of ${transaction} Should Contain granted

    Stop CAN Agent ${agent}

    Log Of ${transaction} Should Contain aborted reason=agent_disconnected

A Second Transaction On A Busy Agent Is Refused
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready
    ${first}=    Start Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    300    --state-dir    ${client_state}
    Log Of ${first} Should Contain granted

    ${second}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    5    --state-dir    ${client_state}

    Should Not Be True    ${second.ok}
    Should Contain    ${second.stderr}    busy
    Log Of ${first} Should Contain done steps=300    timeout=20

A Cancelled Transaction Reports It And Frees The Agent
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    300    --abort-after-ms    500    --state-dir    ${client_state}

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=cancelled
    Wait Until ${hub} Relays No Transaction

A Transaction Needs A Reliable Channel
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    5    --lossy    --state-dir    ${client_state}

    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    open the interface reliable

A Client Without A Write Grant Cannot Start A Transaction
    ${hub_state}=    Fresh State Directory For hub
    ${hub_cfg}=    Hub Configuration    state_dir=${hub_state}
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    5    --state-dir    ${client_state}

    Should Not Be True    ${result.ok}
    Should Contain    ${result.stderr}    write denied

The Hub Lists A Transaction In Flight
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready
    ${transaction}=    Start Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    300    --exclusive    --state-dir    ${client_state}
    Log Of ${transaction} Should Contain granted

    ${rows}=    Transactions Relayed By ${hub}

    Length Should Be    ${rows}    1
    Should Be Equal    ${rows[0].interface}    truck42/vcan0
    Should Be Equal    ${rows[0].state}    active
    Should Be Equal    ${rows[0].lease}    yes


A Node Acknowledging Another CRC Is Named With The Values
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    32    --wrong-crc    21
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    batch    4    32    --state-dir    ${client_state}

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=value_mismatch step=16
    Should Contain    ${result.stdout}    origin=21

A Client That Stops Handing Steps Over Starves Its Transaction
    [Documentation]    The client keeps reading but holds its steps back past the
    ...                agent's 5 s starvation deadline.
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    nodes    1
    Log Of ${ecu} Should Contain ready

    ${result}=    Run Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    batch    50    1    --hold-ms    6500    --state-dir    ${client_state}

    Should Be Equal As Integers    ${result.returncode}    2
    Should Contain    ${result.stdout}    aborted reason=starved

A Released Lease Lets The Other Clients Write Again
    ${hub}=    Start CAN HUB Open To Writers On ${LOCAL_SERVER}
    ${client_state}=    Fresh State Directory For client
    Start CAN Agent truck42 On ${LOCAL_SERVER} Exporting vcan0 Into ${hub}
    ${ecu}=    Start Python ${ECU} On ${LOCAL_SERVER}    vcan0    uds    --delay-ms    10
    Log Of ${ecu} Should Contain ready
    ${transaction}=    Start Python ${TXN} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    uds    100    --exclusive    --state-dir    ${client_state}
    Log Of ${transaction} Should Contain granted
    ${refused}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    send    1A0    CAFE    --state-dir    ${client_state}
    Log Of ${transaction} Should Contain done steps=100    timeout=20
    ${bus}=    Start Candump On ${LOCAL_SERVER}    vcan0

    ${accepted}=    Run Python ${PROBE} On ${LOCAL_SERVER}    ${QUIC}    truck42/vcan0    send    1A0    CAFE    --state-dir    ${client_state}

    Should Not Be True    ${refused.ok}
    Should Be True    ${accepted.ok}    ${accepted.stdout}${accepted.stderr}
    Candump ${bus} Should Capture 1A0#CAFE
