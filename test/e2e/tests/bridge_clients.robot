*** Settings ***
Documentation     What clients see of a bridge (#8). A frame the hub injected
...               along a route reaches the clients of the destination bus
...               marked as bridged: can-hub-client dump prints it with
...               "(bridged)" and python-can-hub sets message.bridged. A frame a
...               node on that bus sent is not marked.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench ${BENCH} With The Python Package On ${LOCAL_SERVER}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${FRAMES}         ${5}
${PLAIN_TCP}      tcp://127.0.0.1:7228
${PROBE}          /work/test/e2e/scripts/pycan_probe.py

*** Test Cases ***
The Dump Of The Destination Bus Marks What Came Over The Route
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    ${dump_cfg}=    Client Configuration    dump    lab/vcan0    connect=${PLAIN_TCP}
    ${dump}=    Start CAN Client On ${LOCAL_SERVER} With ${dump_cfg}
    Wait Until Client ${dump} Has Open Channel    ${hub}

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0    can_id=123
    Send ${FRAMES} Frames On ${LOCAL_SERVER} vcan0    can_id=456
    Log Of ${dump} Should Contain 456
    ${bridged}=    Frames With Id 123 Dumped By ${dump}    bridged=True
    ${local}=    Frames With Id 456 Dumped By ${dump}    bridged=False

    Should Be Equal As Integers    ${bridged}    ${FRAMES}
    Should Be Equal As Integers    ${local}    ${FRAMES}

Python Clients Of The Destination Bus See Bridged Frames As Bridged
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    ${probe}=    Start Python ${PROBE} On ${LOCAL_SERVER}    ${PLAIN_TCP}    lab/vcan0    dump
    Log Of ${probe} Should Contain dumping lab/vcan0

    Inject CAN Frame On ${LAN_SERVER}    vcan0    123#C0FFEE
    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    456#BEEF
    Log Of ${probe} Should Contain 456#BEEF
    ${bridged}=    Lines Of ${probe} Containing 123#C0FFEE bridged
    ${local}=    Lines Of ${probe} Containing 456#BEEF bridged

    Should Be Equal As Integers    ${bridged}    1
    Should Be Equal As Integers    ${local}    0
