*** Settings ***
Documentation     A bridge group is also an interface (#8): clients open
...               group/<name> like any bus. They see what every member
...               captures, and what they write reaches every member, so one
...               attach mirrors a whole group into a local vcan.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${FRAMES}         ${5}
${PLAIN_TCP}      tcp://127.0.0.1:7228

*** Test Cases ***
The Hub Lists A Group As An Interface
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    group    add    fleet

    ${listed}=    Interfaces Listed On ${LOCAL_SERVER}    connect=${PLAIN_TCP}

    Should Contain    ${listed}    group/fleet

A Client Of A Group Sees What Every Member Captures
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    ${dump_cfg}=    Client Configuration    dump    group/fleet    connect=${PLAIN_TCP}
    ${dump}=    Start CAN Client On ${LOCAL_SERVER} With ${dump_cfg}
    Wait Until Client ${dump} Has Open Channel    ${hub}

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0    can_id=123
    Send ${FRAMES} Frames On ${LOCAL_SERVER} vcan0    can_id=456
    Log Of ${dump} Should Contain 456
    ${from_truck}=    Frames With Id 123 Dumped By ${dump}    bridged=False
    ${from_lab}=    Frames With Id 456 Dumped By ${dump}    bridged=False

    Should Be Equal As Integers    ${from_truck}    ${FRAMES}
    Should Be Equal As Integers    ${from_lab}    ${FRAMES}

A Frame Sent To A Group Reaches Every Member
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    ${truck_capture}=    Start Candump On ${LAN_SERVER}    vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    ${sent}=    Send Frame On ${LOCAL_SERVER}    group/fleet    321#CAFE    connect=${PLAIN_TCP}

    Should Be True    ${sent.ok}
    Candump ${truck_capture} Should Capture 321#CAFE
    Candump ${lab_capture} Should Capture 321#CAFE

One Attach Mirrors A Whole Group Into A Local vcan
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    ${attach_cfg}=    Client Configuration    attach    group/fleet    fleet0    connect=${PLAIN_TCP}
    ${attach}=    Start CAN Client On ${LOCAL_SERVER} With ${attach_cfg}
    Log Of ${attach} Should Contain created vcan fleet0
    Wait Until 1 Channels Open On ${hub}
    ${mirror_capture}=    Start Candump On ${LOCAL_SERVER}    fleet0
    ${truck_capture}=    Start Candump On ${LAN_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LOCAL_SERVER} vcan0    can_id=456
    Inject CAN Frame On ${LOCAL_SERVER}    fleet0    789#BEEF
    ${mirrored}=    Wait Until Capture ${mirror_capture} Settles    can_id=456    quiet=1

    Should Be Equal As Integers    ${mirrored}    ${FRAMES}
    Candump ${truck_capture} Should Capture 789#BEEF

A Group Interface Comes Back After A Hub Restart
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    ${restarted}=    Restart ${hub}
    Wait Until Hub ${restarted} Exports 3 Interfaces
    ${dump_cfg}=    Client Configuration    dump    group/fleet    connect=${PLAIN_TCP}
    ${dump}=    Start CAN Client On ${LOCAL_SERVER} With ${dump_cfg}
    Wait Until Client ${dump} Has Open Channel    ${restarted}

    Inject CAN Frame On ${LAN_SERVER}    vcan0    123#AA

    Log Of ${dump} Should Contain 123
