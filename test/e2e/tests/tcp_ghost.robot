*** Settings ***
Documentation     A plaintext agent that loses its link without a FIN leaves a
...               zombie connection on the hub. Its port is blackholed after it
...               registers, so neither its FIN nor any reply to a keepalive probe
...               ever reaches the hub. Reconnecting from the same host must
...               displace the zombie at once; left alone, the hub must reap it.
Library           BenchKeywords
Library           BuiltIn
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Run Keywords    Flush Firewall On ${WAN_SERVER}    AND    Reset Bench    ${BENCH}

*** Test Cases ***
A Plaintext Agent Reconnecting From Its Host Displaces Its Zombie
    Create VCAN On ${WAN_SERVER}    vcan0

    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    tcp://local:7228    zombie1    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}
    ${old_port}=    Source Port Of Agent ${agent} On ${hub}

    Blackhole TCP Source Port ${old_port} On ${WAN_SERVER}
    Stop CAN Agent ${agent}
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}

    Wait Until Agent ${agent} Replaces Port ${old_port} On ${hub}    5

The Hub Reaps A Silent Plaintext Agent
    Create VCAN On ${WAN_SERVER}    vcan0

    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    tcp://local:7228    zombie2    vcan0
    ${agent}=    Start CAN Agent On ${WAN_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}
    ${old_port}=    Source Port Of Agent ${agent} On ${hub}

    Blackhole TCP Source Port ${old_port} On ${WAN_SERVER}
    Stop CAN Agent ${agent}

    Wait Until Agent ${agent} Is Gone From ${hub}    45
