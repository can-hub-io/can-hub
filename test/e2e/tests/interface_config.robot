*** Settings ***
Documentation     Interface configuration through the hub. One request reaches
...               the agent, which applies every setting at once and answers with
...               what the bus holds afterwards. A vcan has no bit timing, so the
...               bench drives what it does have, the link and the TX queue, and
...               checks that a refusal carries the kernel's reason and that a
...               client needs an rwa grant to configure.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${TLS}            tls://127.0.0.1:7227

*** Test Cases ***
Show Reports A vcan Up With Its TX Queue
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}

    ${shown}=    Run CLI On Hub ${hub}    interface    show    bench/vcan0

    Should Match Regexp    ${shown.stdout}    link\\s+up
    Should Match Regexp    ${shown.stdout}    txqueuelen\\s+\\d+

One Set Changes The TX Queue And Takes The Link Down
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}

    Run CLI On Hub ${hub}    interface    set    bench/vcan0    txqueuelen    1000    down
    ${queue}=    TX Queue Length Of vcan0 On ${LOCAL_SERVER}

    Should Be Equal As Integers    ${queue}    1000
    Link vcan0 On ${LOCAL_SERVER} Should Be Down

A Bus Without Bit Timing Says Why It Refused
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}

    ${refused}=    Try CLI On Hub ${hub}    interface    set    bench/vcan0    bitrate    500000

    Should Not Be True    ${refused.ok}
    Should Contain    ${refused.stderr}    apply failed:

Raw Timing On A Bus Without Bit Timing Says Why It Refused
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}

    ${refused}=    Try CLI On Hub ${hub}    interface    set    bench/vcan0    tq    125    prop-seg    6    phase-seg1    7    phase-seg2    2

    Should Not Be True    ${refused.ok}
    Should Contain    ${refused.stderr}    apply failed:

Termination On A Bus Without It Says Why It Refused
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}

    ${refused}=    Try CLI On Hub ${hub}    interface    set    bench/vcan0    termination    120

    Should Not Be True    ${refused.ok}
    Should Contain    ${refused.stderr}    apply failed:

A Bitrate And A Time Quantum Together Are Refused Before Anything Changes
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}

    ${refused}=    Try CLI On Hub ${hub}    interface    set    bench/vcan0    bitrate    500000    tq    125    down

    Should Contain    ${refused.stderr}    give a bitrate or a time quantum and segments, not both
    Link vcan0 On ${LOCAL_SERVER} Should Be Up

A Client With An Admin Grant Configures The Bus
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}
    ${state}=    Fresh State Directory For client
    ${fingerprint}=    Identity Of Client On ${LOCAL_SERVER} In ${state}
    Run CLI On Hub ${hub}    acl    add    ${fingerprint}    bench/vcan0    rwa

    ${configured}=    Configure bench/vcan0 From A Client On ${LOCAL_SERVER}    ${TLS}    ${state}    txqueuelen    700
    ${queue}=    TX Queue Length Of vcan0 On ${LOCAL_SERVER}

    Should Be True    ${configured.ok}
    Should Be Equal As Integers    ${queue}    700

A Client That May Only Write Cannot Configure
    ${hub}=    Start CAN HUB With Agent bench On ${LOCAL_SERVER}
    ${state}=    Fresh State Directory For client
    ${fingerprint}=    Identity Of Client On ${LOCAL_SERVER} In ${state}
    Run CLI On Hub ${hub}    acl    add    ${fingerprint}    bench/vcan0    rw

    ${configured}=    Configure bench/vcan0 From A Client On ${LOCAL_SERVER}    ${TLS}    ${state}    txqueuelen    700

    Should Not Be True    ${configured.ok}
    Should Contain    ${configured.stderr}    not allowed to configure
