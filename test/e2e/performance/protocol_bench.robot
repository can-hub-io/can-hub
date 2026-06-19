*** Settings ***
Documentation     Performance of the four transports (tcp, tls, quic, quic-reliable)
...               across local/lan/wan latency. Agent + hub run on local; the consumer
...               (attach) runs in each zone, so the measured path is the hub->consumer
...               link at that latency. The agent emits ${COUNT} sequence+timestamp
...               stamped frames at max rate (gap 0) into its bus; per scenario we
...               report received, out-of-order, lost, frame rate, latency and jitter.
...               Datagram QUIC is latest-wins (expect loss/reorder under load); tcp,
...               tls and quic-reliable are ordered and lossless.
...
...               Caveat: per-zone netem delay is applied on each namespace's EGRESS
...               (Bench setup). In this read topology (hub->consumer) the forward data
...               path is the consumer's downlink, which netem does not delay, so the
...               reported latency on lan/wan reads ~0 (only the consumer's return ACKs
...               are delayed). Loss/reorder/rate remain meaningful; to measure forward
...               latency apply delay on the hub egress or the consumer ingress (ifb).
Library           BenchKeywords
Variables         bench_variables.py
Suite Setup       Setup Bench    ${BENCH}
Suite Teardown    Teardown Bench    ${BENCH}
Test Teardown     Reset Bench    ${BENCH}
Test Template     Benchmark The Transport

*** Variables ***
# Pacing: vcan has no bitrate limit, so an unpaced sender (gap 0) pushes ~90 kfps
# and the SOURCE SocketCAN queue overflows before a frame ever reaches the hub,
# masking the transport under ~70 % source-side loss. A small GAP throttles the
# sender just below that overflow so the matrix measures the transport
# (loss/reorder/latency/jitter), not the local queue. Note the reported rate is
# bounded by this send pace, not the transport's peak throughput.
${COUNT}    ${10000}
${GAP}      ${0.0001}
${CAN_ID}    123

*** Test Cases ***            SCHEME    PORT    RELIABLE    CONSUMER
tcp local                     tcp       7228    ${False}    ${LOCAL_SERVER}
tcp lan                       tcp       7228    ${False}    ${LAN_SERVER}
tcp wan                       tcp       7228    ${False}    ${WAN_SERVER}
tls local                     tls       7227    ${False}    ${LOCAL_SERVER}
tls lan                       tls       7227    ${False}    ${LAN_SERVER}
tls wan                       tls       7227    ${False}    ${WAN_SERVER}
quic local                    quic      7227    ${False}    ${LOCAL_SERVER}
quic lan                      quic      7227    ${False}    ${LAN_SERVER}
quic wan                      quic      7227    ${False}    ${WAN_SERVER}
quic-reliable local           quic      7227    ${True}     ${LOCAL_SERVER}
quic-reliable lan             quic      7227    ${True}     ${LAN_SERVER}
quic-reliable wan             quic      7227    ${True}     ${WAN_SERVER}

*** Keywords ***
Benchmark The Transport
    [Arguments]    ${scheme}    ${port}    ${reliable}    ${consumer}
    Create VCAN On ${LOCAL_SERVER}    vcan0
    Create VCAN On ${consumer}    vcan1

    ${hub_cfg}=    Benchmark Hub Configuration
    ${hub}=    Start CAN HUB On ${LOCAL_SERVER} With ${hub_cfg}

    ${agent_cfg}=    Agent Configuration    quic://127.0.0.1:7227    edge    vcan0
    ${agent}=    Start CAN Agent On ${LOCAL_SERVER} With ${agent_cfg}
    Wait Until Agent ${agent} Registered On ${hub}    12
    Run CLI On Hub ${hub}    acl    add    *    */*    rw

    ${client_cfg}=    Client Configuration    attach    edge/vcan0    vcan1    connect=${scheme}://local:${port}    reliable=${reliable}
    ${consumer_proc}=    Start CAN Client On ${consumer} With ${client_cfg}
    Wait Until 1 Channels Open On ${hub}    15

    ${capture}=    Start Candump On ${consumer}    vcan1
    ${sent}=    Stream Benchmark Frames On ${LOCAL_SERVER} vcan0    can_id=${CAN_ID}    count=${COUNT}    gap=${GAP}
    Wait Until Capture ${capture} Settles    can_id=${CAN_ID}

    ${m}=    Benchmark Metrics Of ${capture} Over ${sent} For ${CAN_ID}
    Log    ${scheme} rel=${reliable} ${consumer.name}: rx=${m}[received]/${sent} lost=${m}[lost] ooo=${m}[out_of_order] rate=${m}[rate_fps]fps lat_avg=${m}[latency_avg_ms]ms p95=${m}[latency_p95_ms]ms jitter=${m}[jitter_ms]ms    console=True
    Should Be True    ${m}[received] > 0    no frames arrived for ${scheme} at ${consumer.name}
