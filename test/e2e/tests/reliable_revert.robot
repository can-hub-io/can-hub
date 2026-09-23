*** Settings ***
Documentation     A reliable channel is not forever. Each time the last reliable
...               client of an interface goes away, the hub closes the agent's
...               reliable stream and the uplink falls back to the lossy datagram
...               plane. Cycling more times than a peer has reliable stream slots
...               proves the slots are released, and a lossy subscriber still
...               receives the bus afterwards.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${CYCLES}         ${10}
${OPENED}         reliable channel 0 opened
${CLOSED}         reliable channel 0 closed

*** Test Cases ***
The Agent Uplink Leaves Reliable Mode When Its Last Reliable Client Leaves
    Create VCAN On ${LOCAL_SERVER}    vcan0

    ${hub_cfg}=    Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://local:7227    edge    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    8

    FOR    ${cycle}    IN RANGE    1    ${CYCLES + 1}
        ${reliable_cfg}=    Client Configuration    dump    edge/vcan0    connect=quic://local:7227    reliable=${True}
        ${reliable}=    Start CAN Client On ${LOCAL_SERVER} With ${reliable_cfg}
        Wait Until Hub ${hub} Logged ${OPENED} ${cycle} Times
        Stop CAN Client ${reliable}
        Wait Until Hub ${hub} Logged ${CLOSED} ${cycle} Times
    END

    ${lossy_cfg}=    Client Configuration    dump    edge/vcan0    connect=quic://local:7227
    ${lossy}=    Start CAN Client On ${LOCAL_SERVER} With ${lossy_cfg}
    Wait Until 1 Channels Open On ${hub}    12
    Inject CAN Frame On ${LOCAL_SERVER}    vcan0    123#CAFE
    Client ${lossy} Should Receive 123#CAFE
