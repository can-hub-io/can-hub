*** Settings ***
Documentation     Bridge groups (#8). Three buses on two hosts, truck, lab and
...               bench, behave as one: what any member carries is injected into
...               every other member, never back into the one it came from. A
...               bridged frame is never bridged again, and a bus reached by a
...               route and a group is injected once. Groups live in hub.db and
...               apply again after a hub restart.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${FRAMES}         ${10}

*** Test Cases ***
A Group Joins Three Buses Into One
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Start CAN Agent bench On ${LOCAL_SERVER} Exporting vcan1 Into ${hub}
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    bench/vcan1
    ${truck_capture}=    Start Candump On ${LAN_SERVER}    vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    ${bench_capture}=    Start Candump On ${LOCAL_SERVER}    vcan1

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0    can_id=123
    Send ${FRAMES} Frames On ${LOCAL_SERVER} vcan1    can_id=456
    ${truck_on_lab}=    Wait Until Capture ${lab_capture} Settles    can_id=123    quiet=1
    ${truck_on_bench}=    Wait Until Capture ${bench_capture} Settles    can_id=123    quiet=1
    ${bench_on_truck}=    Wait Until Capture ${truck_capture} Settles    can_id=456    quiet=1
    ${bench_on_lab}=    Frames With Id 456 Captured By ${lab_capture}
    ${truck_on_truck}=    Frames With Id 123 Captured By ${truck_capture}
    ${bench_on_bench}=    Frames With Id 456 Captured By ${bench_capture}

    Should Be Equal As Integers    ${truck_on_lab}    ${FRAMES}
    Should Be Equal As Integers    ${truck_on_bench}    ${FRAMES}
    Should Be Equal As Integers    ${bench_on_truck}    ${FRAMES}
    Should Be Equal As Integers    ${bench_on_lab}    ${FRAMES}
    Should Be Equal As Integers    ${truck_on_truck}    ${FRAMES}
    Should Be Equal As Integers    ${bench_on_bench}    ${FRAMES}

A Group Passes Only The Ids It Names
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    group    add    fleet    100
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0    can_id=200
    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0    can_id=100
    ${passed}=    Wait Until Capture ${lab_capture} Settles    can_id=100    quiet=1
    ${filtered}=    Frames With Id 200 Captured By ${lab_capture}

    Should Be Equal As Integers    ${passed}    ${FRAMES}
    Should Be Equal As Integers    ${filtered}    0

A Member That Leaves Stops Receiving
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Start CAN Agent bench On ${LOCAL_SERVER} Exporting vcan1 Into ${hub}
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    bench/vcan1
    Run CLI On Hub ${hub}    group    leave    fleet    bench/vcan1
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    ${bench_capture}=    Start Candump On ${LOCAL_SERVER}    vcan1

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0
    ${on_lab}=    Wait Until Capture ${lab_capture} Settles    quiet=1
    ${on_bench}=    Frames With Id 123 Captured By ${bench_capture}

    Should Be Equal As Integers    ${on_lab}    ${FRAMES}
    Should Be Equal As Integers    ${on_bench}    0

A Route And A Group Into The Same Bus Inject Once
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0
    ${on_lab}=    Wait Until Capture ${lab_capture} Settles    quiet=1

    Should Be Equal As Integers    ${on_lab}    ${FRAMES}

The Group List Shows Its Members And What It Forwarded
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    ghost/can0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0
    Wait Until Frames Captured By ${lab_capture} Reaches ${FRAMES}

    ${listed}=    Run CLI On Hub ${hub}    group

    Should Match Regexp    ${listed.stdout}    fleet\\s+forwarded ${FRAMES}\\s+dropped ${FRAMES}\\s+ids all
    Should Match Regexp    ${listed.stdout}    truck/vcan0\\s+online
    Should Match Regexp    ${listed.stdout}    ghost/can0\\s+offline

A Group Applies Again After A Hub Restart
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    group    add    fleet
    Run CLI On Hub ${hub}    group    join    fleet    truck/vcan0
    Run CLI On Hub ${hub}    group    join    fleet    lab/vcan0
    ${restarted}=    Restart ${hub}
    Wait Until Hub ${restarted} Exports 2 Interfaces
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0
    Wait Until Frames Captured By ${lab_capture} Reaches ${FRAMES}
    ${delivered}=    Frames Captured By ${lab_capture}

    Should Be Equal As Integers    ${delivered}    ${FRAMES}
