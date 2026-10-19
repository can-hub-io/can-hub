*** Settings ***
Documentation     Bus-to-bus bridge routes (#8). Two agents on different hosts,
...               truck and lab, each export a vcan. A route set on the hub
...               injects what the source bus carries into the destination bus,
...               all of it or only the ids it names. Two routes in opposite
...               directions join both buses without a loop, because a bridged
...               frame is never bridged again. Routes live in hub.db and apply
...               again after a hub restart.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${FRAMES}         ${10}

*** Test Cases ***
A Route Carries The Source Bus Into The Destination Bus
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0
    Wait Until Frames Captured By ${lab_capture} Reaches ${FRAMES}
    ${delivered}=    Frames Captured By ${lab_capture}

    Should Be Equal As Integers    ${delivered}    ${FRAMES}

A Route Passes Only The Ids It Names
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0    100
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0    can_id=200
    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0    can_id=100
    ${passed}=    Wait Until Capture ${lab_capture} Settles    can_id=100    quiet=1
    ${filtered}=    Frames With Id 200 Captured By ${lab_capture}

    Should Be Equal As Integers    ${passed}    ${FRAMES}
    Should Be Equal As Integers    ${filtered}    0

Routes Both Ways Join Two Buses Without A Loop
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    Run CLI On Hub ${hub}    bridge    add    lab/vcan0    truck/vcan0
    ${truck_capture}=    Start Candump On ${LAN_SERVER}    vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0    can_id=123
    Send ${FRAMES} Frames On ${LOCAL_SERVER} vcan0    can_id=456
    ${on_lab}=    Wait Until Capture ${lab_capture} Settles    can_id=123    quiet=1
    ${on_truck}=    Wait Until Capture ${truck_capture} Settles    can_id=456    quiet=1
    ${truck_echoes}=    Frames With Id 123 Captured By ${truck_capture}
    ${lab_echoes}=    Frames With Id 456 Captured By ${lab_capture}

    Should Be Equal As Integers    ${on_lab}    ${FRAMES}
    Should Be Equal As Integers    ${on_truck}    ${FRAMES}
    Should Be Equal As Integers    ${truck_echoes}    ${FRAMES}
    Should Be Equal As Integers    ${lab_echoes}    ${FRAMES}

The Route List Counts What A Route Forwarded
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0
    Wait Until Frames Captured By ${lab_capture} Reaches ${FRAMES}

    ${listed}=    Run CLI On Hub ${hub}    bridge

    Should Match Regexp    ${listed.stdout}    truck/vcan0\\s+lab/vcan0\\s+active\\s+${FRAMES}\\s+0\\s+all

A Route Applies Again After A Hub Restart
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    ${restarted}=    Restart ${hub}
    Wait Until Hub ${restarted} Exports 2 Interfaces
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0
    Wait Until Frames Captured By ${lab_capture} Reaches ${FRAMES}
    ${delivered}=    Frames Captured By ${lab_capture}

    Should Be Equal As Integers    ${delivered}    ${FRAMES}

Deleting A Route Stops The Bridge
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With Agents truck On ${LAN_SERVER} And lab On ${LOCAL_SERVER}
    Run CLI On Hub ${hub}    bridge    add    truck/vcan0    lab/vcan0
    Run CLI On Hub ${hub}    bridge    delete    truck/vcan0    lab/vcan0
    ${lab_capture}=    Start Candump On ${LOCAL_SERVER}    vcan0
    ${truck_capture}=    Start Candump On ${LAN_SERVER}    vcan0

    Send ${FRAMES} Frames On ${LAN_SERVER} vcan0
    Wait Until Frames Captured By ${truck_capture} Reaches ${FRAMES}
    ${delivered}=    Wait Until Capture ${lab_capture} Settles    quiet=1

    Should Be Equal As Integers    ${delivered}    0
