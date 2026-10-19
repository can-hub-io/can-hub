*** Settings ***
Documentation     CAN FD across a bridge (#8). The agent reports which of its
...               interfaces carry CAN FD; the hub never injects an FD frame
...               into a classic bus, it drops and counts it instead, and the
...               interface list says which bus is which.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Test Cases ***
The Interface List Tells CAN FD From Classic
    Create VCAN On ${LAN_SERVER}    vcan0
    Enable CAN FD On ${LAN_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Make CAN Classic On ${LOCAL_SERVER}    vcan0
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}

    ${listed}=    Run CLI On Hub ${hub}    interfaces

    Should Match Regexp    ${listed.stdout}    truck\\s+vcan0\\s+.*\\sfd\\s*\\n
    Should Match Regexp    ${listed.stdout}    lab\\s+vcan0\\s+.*\\sclassic\\s*\\n

An FD Frame Is Not Bridged Into A Classic Bus
    Create VCAN On ${LAN_SERVER}    vcan0
    Enable CAN FD On ${LAN_SERVER}    vcan0
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Make CAN Classic On ${LOCAL_SERVER}    vcan0
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    Inject CAN Frame On ${LAN_SERVER}    vcan0    123##1AABBCCDDEEFF0011
    Inject CAN Frame On ${LAN_SERVER}    vcan0    456#BEEF
    Candump ${lab_capture} Should Capture 456#BEEF
    ${listed}=    Run CLI On Hub ${hub}    bridge
    ${fd_on_lab}=    Frames With Id 123 Captured By ${lab_capture}

    Should Be Equal As Integers    ${fd_on_lab}    0
    Should Match Regexp    ${listed.stdout}    truck/vcan0\\s+lab/vcan0\\s+active\\s+1\\s+1\\s
