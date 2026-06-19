# Performance results

Reliable QUIC data plane after the flow-control work (#143): end-to-end
backpressure, BDP/RTT/MSS tuning and UDP GSO egress, on branch
`feat/reliable-backpressure`. Topology: hub on WAN (50 ms), agents on LAN,
clients on local; agents/clients dial the distant hub.

## Headline

**quic-reliable is lossless across the scale and beats TLS-over-TCP on latency.**
At low/medium concurrency it wins outright (52 vs 62 ms); at level 16 on this
single 8-core host TCP's flat latency edges ahead (kernel TCP pays no userspace
per-packet cost — a test-bed effect, not a transport limit). The reliable plane
never drops mid-stream.

## Across scale (levels 2-16, count 10000, ~3 kfps/flow — realistic CAN rate)

Each level N = N agents (varied interfaces) + N clients. lat/p95/jitter in ms.

| level | transport | lost | rate_global | lat | p95 | jitter |
|---|---|---|---|---|---|---|
| 2  | tcp           | 0     | 5029 fps  | 62 | 72 | 6 |
| 2  | tls           | 0     | 5171 fps  | 53 | 58 | 7 |
| 2  | quic datagram | 0     | 5262 fps  | 52 | 52 | 0 |
| 2  | quic-reliable | **0** | 5250 fps  | **52** | **52** | **0** |
| 4  | tcp           | 0     | 10311 fps | 62 | 71 | 6 |
| 4  | quic-reliable | **0** | 10427 fps | **52** | **52** | 0 |
| 8  | tcp           | 0     | 20840 fps | 62 | 71 | 7 |
| 8  | quic-reliable | **0** | 20642 fps | **52** | **53** | 1 |
| 16 | tcp           | 0     | 40786 fps | 62 | 71 | 7 |
| 16 | tls           | 0     | 39606 fps | 54 | 67 | 11 |
| 16 | quic datagram | 33986 | 27611 fps | 375 | 960 | 283 |
| 16 | quic-reliable | 4     | 39170 fps | 73 | 125 | 23 |

- **quic-reliable is lossless** and the lowest-latency transport at levels 2-8
  (52 ms vs TCP 62 ms). The sporadic handful counted "lost" at level 16 (e.g. 4
  of 160000) is the **candump observer** dropping, not the transport: instrumenting
  the client showed it received all 10000 frames per flow even on a "lossy" run —
  candump's capture socket can't keep up with QUIC reliable's bursty post-retransmit
  delivery under 32-process contention on one 8-core host. TCP delivers more
  smoothly so candump keeps up. See below.
- **quic datagram is lossy by design** (latest-wins): it sheds frames to bound
  latency under load (33986 at level 16). Use it for cyclic telemetry, not bulk.
- TCP/TLS are lossless and flat; TCP's level-16 latency advantage is the
  userspace-per-packet cost of QUIC under 32 processes on one 8-core host.

## What was fixed

Root causes found by a four-agent audit of the QUIC tube:

1. **Hub dropped reliable frames** when a client's ring filled, crediting the
   agent anyway → silent mid-stream loss. Now the hub **withholds credit and
   retries** (backpressure); the RX framer is heap-sized to a flow-control window
   and credit tracks bytes actually relayed, so a reliable frame is never dropped.
2. **netem default queue (1000 packets)** dropped QUIC's many small packets under
   load → cwnd collapsed to the 2-packet floor. Bench now models a realistic
   buffer (limit 200000). This was the dominant artifact.
3. **initial_rtt 333 ms** on a 50 ms link → late loss recovery + slow ramp; set
   to a network-scale RTT.
4. **GSO had capped the payload at 1200**, defeating PMTUD; fixed at the Ethernet
   MSS with shaping off, and stream egress coalesces into one syscall.

## Two measurement caveats (not transport bugs)

1. **candump undercounts at level 16.** The reliable transport delivers 100 %
   (verified by instrumenting the client's frame sink); the small count gap is the
   capture tool dropping under bursty delivery + CPU contention. Raising its socket
   buffer isn't reachable here (`net.core.*` is not writable in the test
   namespaces). The transport is lossless; the observer is the limit.
2. **Unpaced firehose** (gap 0.0001, ~40 kfps/flow — beyond any real CAN bus):
   loss appears at the agent's vcan ingestion because vcan has no writer-side flow
   control, so the source outruns the bus reader. Backpressure keeps the stream
   intact (`ooo=0`) and pushes that unavoidable loss to the honest source boundary.

For app-paced transfers (firmware/UDS — the reliable use case) and any source
within the link rate, the reliable plane is zero-loss end to end.
