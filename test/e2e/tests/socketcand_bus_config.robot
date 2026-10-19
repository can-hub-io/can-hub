*** Settings ***
Documentation     Bus configuration through the socketcand bridge. The socketcand
...               bit timing (B) and control mode (C) commands become a hub
...               CONFIGURE, so they need an rwa grant and answer with the
...               reason the agent gives. A vcan has neither bit timing nor
...               control modes, so the bench checks the refusals.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${TLS}            tls://127.0.0.1:7227

*** Test Cases ***
A Socketcand Bit Timing Says Why The Bus Refused It
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}
    ${state}=    Fresh State Directory For client
    ${fingerprint}=    Identity Of Client On ${LOCAL_SERVER} In ${state}
    Run CLI On Hub ${hub}    acl    add    ${fingerprint}    bench/vcan0    rwa
    ${bridge_cfg}=    Client Configuration    socketcand    connect=${TLS}    state_dir=${state}    extra=--no-beacon
    ${bridge}=    Start CAN Client On ${LOCAL_SERVER} With ${bridge_cfg}

    ${replies}=    Socketcand Replies To < bench/vcan0 B 500000 875 0 0 0 0 0 0 > On ${LOCAL_SERVER}

    Should Start With    ${replies}[0]    < error apply failed:

A Socketcand Client Without An Admin Grant Cannot Set Control Modes
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}
    ${state}=    Fresh State Directory For client
    ${fingerprint}=    Identity Of Client On ${LOCAL_SERVER} In ${state}
    Run CLI On Hub ${hub}    acl    add    ${fingerprint}    bench/vcan0    rw
    ${bridge_cfg}=    Client Configuration    socketcand    connect=${TLS}    state_dir=${state}    extra=--no-beacon
    ${bridge}=    Start CAN Client On ${LOCAL_SERVER} With ${bridge_cfg}

    ${replies}=    Socketcand Replies To < bench/vcan0 C 1 0 0 > On ${LOCAL_SERVER}

    Should Be Equal    ${replies}[0]    < error not allowed to configure this interface >

