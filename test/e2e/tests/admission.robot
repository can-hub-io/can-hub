*** Settings ***
Documentation     Admission before authentication (#180). A source that opens
...               connections and never finishes the handshake must not take
...               the hub's tables: one host may hold four handshakes in flight,
...               a TLS handshake that stalls is reaped, and once QUIC
...               handshakes pile up the hub answers new ones with a Retry
...               instead of a slot, so a real agent still gets in.
Library           BenchKeywords
Library           BuiltIn
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Run Keywords    Flush Firewall On ${LAN_SERVER}    AND    Flush Firewall On ${WAN_SERVER}    AND    Reset Bench    ${BENCH}

*** Variables ***
${HANDSHAKES_PER_HOST}    ${4}
${HANDSHAKE_LOGGED}       quic: handshake

*** Test Cases ***
A Host Gets Four TLS Handshakes In Flight And Stalled Ones Are Reaped
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${silent}=    Open 5 Silent TCP Connections From ${WAN_SERVER} To ${LOCAL_SERVER.ip} Port 7227
    ${refused}=    Count Of ${silent} Closed Between 0s And 1s
    ${reaped}=    Count Of ${silent} Closed Between 9s And 13s

    Should Be Equal As Integers    ${refused}    1
    Should Be Equal As Integers    ${reaped}    ${HANDSHAKES_PER_HOST}

A Real Agent Gets In Through A Retry While QUIC Handshakes Pile Up
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${debug}=    Create List    --log-level    debug
    ${hub_cfg}=    Hub Configuration    extra=${debug}
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    Drop UDP From Source Port 7227 On ${LAN_SERVER}
    Drop UDP From Source Port 7227 On ${WAN_SERVER}
    ${stalled_cfg}=    Client Configuration    list    connect=quic://local:7227
    FOR    ${server}    IN    ${LAN_SERVER}    ${WAN_SERVER}
        FOR    ${index}    IN RANGE    ${HANDSHAKES_PER_HOST}
            Start CAN Client On ${server} With ${stalled_cfg}
        END
    END
    Wait Until Hub ${hub} Logged ${HANDSHAKE_LOGGED} 8 Times

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    admitted    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}

    Wait Until Agent ${agent} Registered On ${hub}    8
    Log Of ${hub} Should Contain sent a retry
