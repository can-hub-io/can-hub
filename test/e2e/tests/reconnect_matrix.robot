*** Settings ***
Documentation     Reconnect matrix (#17): every agent transport recovers from a
...               network drop, a hub restart or crash and its own restart or
...               crash. Recovered means the agent registers again, the hub holds
...               it exactly once (no ghost from the dead connection) and a frame
...               from its bus reaches a client afterwards; a hub that stops
...               cleanly tells its agents at once, well inside the QUIC idle
...               timeout a crash leaves them to. A hub identity change
...               is covered by hub_pinning.robot and security.robot (one pinned
...               verifier for tls and quic), the tcp zombie by tcp_ghost.robot.
...               An attach mirror reconnects too and mirrors both ways again.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Run Keywords    Set Link Loss On ${WAN_SERVER} To 0    AND    Set Link Loss On ${LAN_SERVER} To 0
...               AND    Reset Bench    ${BENCH}

*** Test Cases ***
QUIC Agent Recovers From A Network Drop
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent drop-quic On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=quic://local:7227
    ${clients}=    Dump drop-quic/vcan0 From 1 Clients On ${hub}

    Set Link Loss On ${WAN_SERVER} To 100
    Wait Until Agent ${agent} Lost Its Hub 1 Times
    Set Link Loss On ${WAN_SERVER} To 0

    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE    15

TLS Agent Recovers From A Network Drop
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent drop-tls On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tls://local:7227
    ${clients}=    Dump drop-tls/vcan0 From 1 Clients On ${hub}

    Set Link Loss On ${WAN_SERVER} To 100
    Wait Until Agent ${agent} Lost Its Hub 1 Times
    Set Link Loss On ${WAN_SERVER} To 0

    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE    15

TCP Agent Recovers From A Network Drop
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent drop-tcp On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tcp://local:7228
    ${clients}=    Dump drop-tcp/vcan0 From 1 Clients On ${hub}

    Set Link Loss On ${WAN_SERVER} To 100
    Wait Until Agent ${agent} Lost Its Hub 1 Times
    Set Link Loss On ${WAN_SERVER} To 0

    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE    15

QUIC Agent Survives A Hub Restart
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent stop-hub-quic On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=quic://local:7227

    ${hub}=    Restart ${hub}

    Wait Until Agent ${agent} Lost Its Hub 1 Times    3
    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    ${clients}=    Dump stop-hub-quic/vcan0 From 1 Clients On ${hub}
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

TLS Agent Survives A Hub Restart
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent stop-hub-tls On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tls://local:7227

    ${hub}=    Restart ${hub}

    Wait Until Agent ${agent} Lost Its Hub 1 Times    3
    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    ${clients}=    Dump stop-hub-tls/vcan0 From 1 Clients On ${hub}
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

TCP Agent Survives A Hub Restart
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent stop-hub-tcp On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tcp://local:7228

    ${hub}=    Restart ${hub}

    Wait Until Agent ${agent} Lost Its Hub 1 Times    3
    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    ${clients}=    Dump stop-hub-tcp/vcan0 From 1 Clients On ${hub}
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

QUIC Agent Survives A Hub Crash
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent kill-hub-quic On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=quic://local:7227

    ${hub}=    Crash And Restart ${hub}

    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    ${clients}=    Dump kill-hub-quic/vcan0 From 1 Clients On ${hub}
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

TLS Agent Survives A Hub Crash
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent kill-hub-tls On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tls://local:7227

    ${hub}=    Crash And Restart ${hub}

    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    ${clients}=    Dump kill-hub-tls/vcan0 From 1 Clients On ${hub}
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

TCP Agent Survives A Hub Crash
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent kill-hub-tcp On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tcp://local:7228

    ${hub}=    Crash And Restart ${hub}

    Wait Until Agent ${agent} Has Registered 2 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    ${clients}=    Dump kill-hub-tcp/vcan0 From 1 Clients On ${hub}
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

QUIC Agent Comes Back After A Restart
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent stop-quic On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=quic://local:7227
    ${clients}=    Dump stop-quic/vcan0 From 1 Clients On ${hub}

    ${agent}=    Restart ${agent}

    Wait Until Agent ${agent} Has Registered 1 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

TLS Agent Comes Back After A Restart
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent stop-tls On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tls://local:7227
    ${clients}=    Dump stop-tls/vcan0 From 1 Clients On ${hub}

    ${agent}=    Restart ${agent}

    Wait Until Agent ${agent} Has Registered 1 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

TCP Agent Comes Back After A Restart
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent stop-tcp On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tcp://local:7228
    ${clients}=    Dump stop-tcp/vcan0 From 1 Clients On ${hub}

    ${agent}=    Restart ${agent}

    Wait Until Agent ${agent} Has Registered 1 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

QUIC Agent Comes Back After A Crash
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent kill-quic On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=quic://local:7227
    ${clients}=    Dump kill-quic/vcan0 From 1 Clients On ${hub}

    ${agent}=    Crash And Restart ${agent}

    Wait Until Agent ${agent} Has Registered 1 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

TLS Agent Comes Back After A Crash
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent kill-tls On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tls://local:7227
    ${clients}=    Dump kill-tls/vcan0 From 1 Clients On ${hub}

    ${agent}=    Crash And Restart ${agent}

    Wait Until Agent ${agent} Has Registered 1 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

TCP Agent Comes Back After A Crash
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent kill-tcp On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=tcp://local:7228
    ${clients}=    Dump kill-tcp/vcan0 From 1 Clients On ${hub}

    ${agent}=    Crash And Restart ${agent}

    Wait Until Agent ${agent} Has Registered 1 Times
    Hub ${hub} Should Hold Agent ${agent} Once
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Every Client Of ${clients} Should Receive 321#CAFEBABE

Socketcand Bridge Serves Again After A Hub Restart
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent bridged On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=quic://local:7227
    ${bridge_cfg}=    Client Configuration    socketcand    connect=quic://local:7227    extra=--no-beacon
    ${bridge}=    Start CAN Client On ${LAN_SERVER} With ${bridge_cfg}
    Wait Until Hub ${hub} Holds 1 Client Peers

    ${hub}=    Restart ${hub}

    Wait Until Agent ${agent} Has Registered 2 Times
    Wait Until Hub ${hub} Holds 1 Client Peers
    ${connections}=    Create List    bridged/vcan0
    ${consumer}=    Start Draining ${connections} On ${LAN_SERVER}    seconds=8
    Wait Until 1 Channels Open On ${hub}    8
    Send 20 Frames On ${WAN_SERVER} vcan0
    ${counts}=    Drain Result Of ${consumer}
    Should Be Equal As Integers    ${counts}[bridged/vcan0]    20

Attach Mirrors Both Ways Again After A Hub Restart
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent attach-hub On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=quic://local:7227
    Run CLI On Hub ${hub}    acl    add    *    attach-hub/vcan0    rw
    ${attach_cfg}=    Client Configuration    attach    attach-hub/vcan0    mirror0    connect=quic://local:7227
    ${attach}=    Start CAN Client On ${LAN_SERVER} With ${attach_cfg}
    Wait Until 1 Channels Open On ${hub}
    ${mirror_capture}=    Start Candump On ${LAN_SERVER}    mirror0
    ${bus_capture}=    Start Candump On ${WAN_SERVER}    vcan0

    ${hub}=    Restart ${hub}

    Log Of ${attach} Should Contain mirroring interface attach-hub/vcan0 to mirror0 again    20
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Inject CAN Frame On ${LAN_SERVER}    mirror0    654#BEEF
    Candump ${mirror_capture} Should Capture 321#CAFEBABE
    Candump ${bus_capture} Should Capture 654#BEEF

Attach Mirrors Both Ways Again After A Network Drop
    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${agent}=    Start CAN Agent attach-drop On ${WAN_SERVER} Exporting vcan0 Into ${hub}    connect=quic://local:7227
    Run CLI On Hub ${hub}    acl    add    *    attach-drop/vcan0    rw
    ${attach_cfg}=    Client Configuration    attach    attach-drop/vcan0    mirror0    connect=quic://local:7227
    ${attach}=    Start CAN Client On ${LAN_SERVER} With ${attach_cfg}
    Wait Until 1 Channels Open On ${hub}
    ${mirror_capture}=    Start Candump On ${LAN_SERVER}    mirror0
    ${bus_capture}=    Start Candump On ${WAN_SERVER}    vcan0

    Set Link Loss On ${LAN_SERVER} To 100
    Log Of ${attach} Should Contain connection lost, reconnecting in    30
    Set Link Loss On ${LAN_SERVER} To 0

    Log Of ${attach} Should Contain mirroring interface attach-drop/vcan0 to mirror0 again    30
    Inject CAN Frame On ${WAN_SERVER}    vcan0    321#CAFEBABE
    Inject CAN Frame On ${LAN_SERVER}    mirror0    654#BEEF
    Candump ${mirror_capture} Should Capture 321#CAFEBABE
    Candump ${bus_capture} Should Capture 654#BEEF
