*** Settings ***
Documentation     The security model end to end: what each transport negotiates,
...               that an identity survives a restart, and that pinning and the
...               agent allowlist reject what they are supposed to reject.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}

*** Variables ***
${TLS}            tls://127.0.0.1:7227
${QUIC}           quic://127.0.0.1:7227
${AGENT_STATE}    /tmp/security-agent
${CLIENT_STATE}   /tmp/security-client

*** Test Cases ***
TLS Negotiates A Suite The Build Offers
    ${hub}=    Start Hub Listening On Both Encrypted Transports
    ${client}=    Dump Over ${TLS} As over-tls Against ${hub}

    Log Of ${hub} Should Contain on tls://
    Log Of ${hub} Should Contain negotiated TLS_
    Log Of ${client} Should Contain over tls://, negotiated TLS_

QUIC Negotiates A Suite The Build Offers
    ${hub}=    Start Hub Listening On Both Encrypted Transports
    ${client}=    Dump Over ${QUIC} As over-quic Against ${hub}

    Log Of ${hub} Should Contain on quic://
    Log Of ${hub} Should Contain negotiated TLS_
    Log Of ${client} Should Contain over quic://, negotiated TLS_

Both Transports Negotiate The Same Suite
    ${hub}=    Start Hub Listening On Both Encrypted Transports
    Dump Over ${TLS} As same-tls Against ${hub}
    Dump Over ${QUIC} As same-quic Against ${hub}

    ${over_tls}=    Negotiated Cipher Suite On ${hub} Over tls
    ${over_quic}=    Negotiated Cipher Suite On ${hub} Over quic
    Should Be Equal    ${over_tls}    ${over_quic}

Agent Identity Survives A Restart
    ${before}=    Identity Of Agent On ${LOCAL_SERVER} In ${AGENT_STATE}
    ${after}=    Identity Of Agent On ${LOCAL_SERVER} In ${AGENT_STATE}

    Should Match Regexp    ${before}    ^[0-9a-f]{64}$
    Should Be Equal    ${before}    ${after}

Client Identity Survives A Restart
    ${before}=    Identity Of Client On ${LOCAL_SERVER} In ${CLIENT_STATE}
    ${after}=    Identity Of Client On ${LOCAL_SERVER} In ${CLIENT_STATE}

    Should Match Regexp    ${before}    ^[0-9a-f]{64}$
    Should Be Equal    ${before}    ${after}

Hub Pins The Agent Fingerprint On First Register
    ${hub}=    Start Hub Listening On Both Encrypted Transports
    ${agent}=    Register Agent pinned On ${hub} From ${AGENT_STATE}
    ${fingerprint}=    Identity Of Agent On ${LOCAL_SERVER} In ${AGENT_STATE}

    ${pins}=    Run CLI On Hub ${hub}    pins
    Should Contain    ${pins.stdout}    ${fingerprint}

An Agent Reusing A Name With A New Identity Is Rejected
    ${hub}=    Start Hub Listening On Both Encrypted Transports
    Register Agent rekeyed On ${hub} From ${AGENT_STATE}

    ${impostor}=    Start Agent rekeyed On ${hub} From /tmp/security-impostor
    Log Of ${impostor} Should Contain rejected    timeout=10
    Hub ${hub} Should Export 1 Interfaces

An Unknown Agent Is Rejected When The Allowlist Is On
    ${hub}=    Start Hub With Allowlist
    ${agent}=    Start Agent unlisted On ${hub} From ${AGENT_STATE}

    Log Of ${agent} Should Contain rejected    timeout=10
    Hub ${hub} Should Export 0 Interfaces

An Allowlisted Agent Is Accepted
    ${hub}=    Start Hub With Allowlist
    ${fingerprint}=    Identity Of Agent On ${LOCAL_SERVER} In ${AGENT_STATE}
    Run CLI On Hub ${hub}    pins    add    allowed    ${fingerprint}

    Register Agent allowed On ${hub} From ${AGENT_STATE}
    Hub ${hub} Should Export 1 Interfaces

*** Keywords ***
Start Hub Listening On Both Encrypted Transports
    ${hub}=    Start Hub With Allowlist ${False}
    RETURN    ${hub}

Start Hub With Allowlist
    ${hub}=    Start Hub With Allowlist ${True}
    RETURN    ${hub}

Start Hub With Allowlist ${required}
    ${config}=    Hub Configuration    listen=${{ ["${TLS}", "${QUIC}"] }}
    ...    require_known_agents=${required}
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${config}
    RETURN    ${hub}

Start Agent ${name} On ${hub} From ${state_dir}
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${config}=    Agent Configuration    ${TLS}    ${name}    vcan0    state_dir=${state_dir}
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${config}
    RETURN    ${agent}

Register Agent ${name} On ${hub} From ${state_dir}
    ${agent}=    Start Agent ${name} On ${hub} From ${state_dir}
    Wait Until Agent ${agent} Registered On ${hub}
    RETURN    ${agent}

Dump Over ${connect} As ${name} Against ${hub}
    Create VCAN On ${LOCAL_SERVER}    vcan0
    ${agent_state}=    Fresh State Directory For agent
    ${client_state}=    Fresh State Directory For client
    ${agent_config}=    Agent Configuration    ${connect}    ${name}    vcan0    state_dir=${agent_state}
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_config}
    Wait Until Agent ${agent} Registered On ${hub}

    ${client_config}=    Client Configuration    dump    ${name}/vcan0    connect=${connect}    state_dir=${client_state}
    ${client}=    Start CAN Client On ${LOCAL_SERVER} With ${client_config}
    Wait Until Client ${client} Has Open Channel    ${hub}
    RETURN    ${client}

Hub ${hub} Should Export ${count} Interfaces
    ${rows}=    Interfaces On ${hub}
    Length Should Be    ${rows}    ${count}
