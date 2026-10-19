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
    ${hub}=    Start CAN HUB With Agent crowd On ${LOCAL_SERVER}
    ${clients}=    Dump crowd/vcan0 From 100 Clients On ${hub}

    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    123#C0FFEE

    Every Client Of ${clients} Should Receive 123#C0FFEE
