*** Settings ***
Documentation     Per-channel TX fairness (#59). One agent exports a quiet and a
...               noisy bus; one client (socketcand bridge) holds both on a single
...               hub peer over a rate-limited link. The noisy bus floods and
...               saturates the shared pipe; the quiet bus must still get through.
...               Without the fix the quiet bus loses ~70%% of its frames; with it,
...               the drops land on the noisy channel and the quiet bus stays 100%%.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}

*** Variables ***
${QUIET_FRAMES}    ${60}

*** Test Cases ***
Quiet Channel Survives A Noisy Neighbour On The Same Peer
    Create VCAN On ${LOCAL_SERVER}    quiet
    Create VCAN On ${LOCAL_SERVER}    noisy

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    truck42    quiet    noisy
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    Limit Egress On ${LOCAL_SERVER} To 3mbit

    ${bridge_cfg}=    Client Configuration    socketcand    connect=quic://local:7227    extra=--no-beacon
    ${bridge}=    Start CAN Client On ${LAN_SERVER} With ${bridge_cfg}
    Sleep    2s    reason=let the bridge connect to the hub and cache the interface list

    ${channels}=    Create List    truck42/quiet    truck42/noisy
    ${consumer}=    Start Draining ${channels} On ${LAN_SERVER}    seconds=12
    Wait Until 2 Channels Open On ${hub}    12

    Flood noisy On ${LOCAL_SERVER}
    Send ${QUIET_FRAMES} Frames On ${LOCAL_SERVER} quiet

    ${drops}=    Channel Drops On ${hub}
    ${counts}=    Drain Result Of ${consumer}
    Log    quiet delivered: ${counts}[truck42/quiet]/${QUIET_FRAMES}, channel drops: ${drops}    console=True

    # The quiet bus is delivered in full and never charged a drop...
    Should Be Equal As Integers    ${counts}[truck42/quiet]    ${QUIET_FRAMES}
    Should Be Equal As Integers    ${drops}[quiet]    0
    # ...while the noisy bus absorbs the saturation it caused.
    Should Be True    ${drops}[noisy] > 0
