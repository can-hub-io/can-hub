*** Settings ***
Documentation     How agents and clients trust the hub (#202). known_hubs holds
...               one line per hub listing every address it was reached under,
...               so a hub dialed under a new address opens no new first
...               contact. The agent serves one hub and refuses a new
...               fingerprint once it trusts one; a client may pin several.
...               --hub-fingerprint replaces first contact with a fixed pin.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${LOOPBACK_TLS}         tls://127.0.0.1:7227
${OTHER_FINGERPRINT}    0000000000000000000000000000000000000000000000000000000000000000

*** Test Cases ***
An Agent Accepts Its Hub Under A New Address
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${state}=    Fresh State Directory For agent
    ${loopback_cfg}=    Agent Configuration    ${LOOPBACK_TLS}    moved    vcan0    state_dir=${state}
    ${loopback_agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${loopback_cfg}
    Wait Until Agent ${loopback_agent} Registered On ${hub}
    Stop CAN Agent ${loopback_agent}
    Wait Until Agent ${loopback_agent} Is Gone From ${hub}
    ${bridge_cfg}=    Agent Configuration    tls://${LOCAL_SERVER.ip}:7227    moved    vcan0    state_dir=${state}

    ${bridge_agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${bridge_cfg}
    Wait Until Agent ${bridge_agent} Registered On ${hub}
    ${pins}=    Pinned Hubs In ${state}

    Length Should Be    ${pins}    1
    Should Contain    ${pins}[0]    127.0.0.1:7227,${LOCAL_SERVER.ip}:7227

An Agent Refuses Another Hub Under A New Address
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${state}=    Fresh State Directory For agent
    Pin Hub old-hub.example:7227 As ${OTHER_FINGERPRINT} In ${state}
    ${agent_cfg}=    Agent Configuration    ${LOOPBACK_TLS}    loyal    vcan0    state_dir=${state}

    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}

    Log Of ${agent} Should Contain not the hub this agent trusts    timeout=10

A Client Pins Another Hub Under A New Address
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}
    ${state}=    Fresh State Directory For client
    Pin Hub old-hub.example:7227 As ${OTHER_FINGERPRINT} In ${state}
    ${client_cfg}=    Client Configuration    dump    bench/vcan0    connect=${LOOPBACK_TLS}    state_dir=${state}

    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_cfg}
    Wait Until Client ${client} Has Open Channel    ${hub}
    ${pins}=    Pinned Hubs In ${state}

    Length Should Be    ${pins}    2

An Agent Given The Hub Fingerprint Writes No Pin
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${fingerprint}=    Identity Of Hub ${hub}
    ${state}=    Fresh State Directory For agent
    ${agent_cfg}=    Agent Configuration    ${LOOPBACK_TLS}    fixed    vcan0    state_dir=${state}    extra=${{ ["--hub-fingerprint", "${fingerprint}"] }}

    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}
    ${pins}=    Pinned Hubs In ${state}

    Should Be Empty    ${pins}

An Agent Given Another Fingerprint Refuses The Hub
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}
    ${state}=    Fresh State Directory For agent
    ${agent_cfg}=    Agent Configuration    ${LOOPBACK_TLS}    misled    vcan0    state_dir=${state}    extra=${{ ["--hub-fingerprint", "${OTHER_FINGERPRINT}"] }}

    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}

    Log Of ${agent} Should Contain does not match the expected one    timeout=10
