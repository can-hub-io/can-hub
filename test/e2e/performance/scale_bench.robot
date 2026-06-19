*** Settings ***
Documentation     Scaling of the four transports under a growing fleet. Fixed topology:
...               the hub runs on the WAN host (50 ms), agents on the LAN host (1.5 ms)
...               and clients on the local host (0 ms) — agents and clients dial the
...               distant hub. Each level N starts N agents and N clients; agent i
...               exports a VARIED number of interfaces (pattern 1,2,4,1,3...) and
...               client i consumes agent i's first interface, so the client count tracks
...               the agent count while the extra interfaces load the registry. Per level
...               we report the aggregate across all clients: received, lost, out-of-order,
...               frame rate, latency and jitter.
...
...               Hub peer ceiling: PEER_DIRECTORY_MAX / QUIC_SERVER_PEERS_MAX = 64, so a
...               level is 2N peers. Level 32 (64 peers) is the edge; levels 64/128 need
...               those caps raised — tracked in issue #143. If level 32 fails on the peer
...               limit, that is the documented ceiling, not a regression.
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}
Test Template     Benchmark Scaled

*** Variables ***
${COUNT}    ${1000}
${GAP}      ${0.0001}

*** Test Cases ***                SCHEME    PORT    RELIABLE    LEVEL
tcp level 2                       tcp       7228    ${False}    ${2}
tcp level 4                       tcp       7228    ${False}    ${4}
tcp level 8                       tcp       7228    ${False}    ${8}
tcp level 16                      tcp       7228    ${False}    ${16}
tcp level 32                      tcp       7228    ${False}    ${32}
tls level 2                       tls       7227    ${False}    ${2}
tls level 4                       tls       7227    ${False}    ${4}
tls level 8                       tls       7227    ${False}    ${8}
tls level 16                      tls       7227    ${False}    ${16}
tls level 32                      tls       7227    ${False}    ${32}
quic level 2                      quic      7227    ${False}    ${2}
quic level 4                      quic      7227    ${False}    ${4}
quic level 8                      quic      7227    ${False}    ${8}
quic level 16                     quic      7227    ${False}    ${16}
quic level 32                     quic      7227    ${False}    ${32}
quic-reliable level 2             quic      7227    ${True}     ${2}
quic-reliable level 4             quic      7227    ${True}     ${4}
quic-reliable level 8             quic      7227    ${True}     ${8}
quic-reliable level 16            quic      7227    ${True}     ${16}
quic-reliable level 32            quic      7227    ${True}     ${32}

*** Keywords ***
Benchmark Scaled
    [Arguments]    ${scheme}    ${port}    ${reliable}    ${level}
    ${m}=    Run Scaled Benchmark ${scheme} Port ${port} Reliable ${reliable} Level ${level}
    ...    hub_server=${WAN_SERVER}
    ...    agent_server=${LAN_SERVER}
    ...    client_server=${LOCAL_SERVER}
    ...    count=${COUNT}
    ...    gap=${GAP}
    Log Scaled Metrics ${level} ${scheme} Reliable ${reliable} ${m}
    Should Be True    ${m}[received] > 0    no frames delivered at level ${level} for ${scheme}
