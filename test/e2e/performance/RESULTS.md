# Performance baseline

Snapshot to compare against the #143 backpressure work (Part 2). Captured after
Part 1 (reliable heap TX ring 256 KiB + BDP flow-control windows: stream 256 KiB,
conn 4 MiB), branch `feat/flow-control-bench`.

Conditions:
- Topology (`scale_bench.robot`): hub on WAN (50 ms egress), agents on LAN (1.5 ms),
  clients on local (0 ms). Agents/clients dial the distant hub.
- Frame payload: 8 bytes (u32 seq + u32 send-time low). One stream per measured iface.
- Numbers are noisy: the bench is bounded by vcan source saturation (gap 0) and host
  CPU contention (level 16 = ~48 processes), not pure transport. `hub_drop` is the
  hub's forwarded-then-dropped counter; agent-side reliable drops are not counted here.

## Scaling matrix, level 16 (16 agents / 34 ifaces / 16 clients), count 1000, gap 0.0001

| transport | rx/16000 | lost | hub_drop | ooo | span | rate_global | lat | p95 | jitter |
|---|---|---|---|---|---|---|---|---|---|
| tcp           | 16000 | 0     | 0 | 0  | 0.328s | 48771 fps | 72ms  | 99ms  | 13ms |
| tls           | 15569 | 431   | 0 | 0  | 0.462s | 33702 fps | 109ms | 251ms | 56ms |
| quic datagram | 5472  | 10528 | 0 | 20 | 0.601s | 9104 fps  | 122ms | 373ms | 133ms |
| quic-reliable | 14115 | 1885  | 0 | 47 | 0.616s | 22915 fps | 220ms | 434ms | 128ms |

- Ordered-transport `lost` (tls 431, reliable 1885) is measurement-tail / contention,
  not transport loss (`hub_drop` = 0). Datagram `lost` 10528 is real (latest-wins).

## Single flow (level 1), count 30000, gap 0 (saturated source, relative comparison)

| transport | rate_global |
|---|---|
| quic-reliable | 23211 fps |
| tcp           | 18899 fps |
| quic datagram | 7053 fps  |
| tls           | 6575 fps  |

## What Part 2 (backpressure) must show

The bigger ring only absorbs bursts up to ~256 KiB (~13k frames). A sustained transfer
exceeding the ring under a **slow sink** still drops at the agent (QueueTx failure, not
visible in `hub_drop`). The current benches saturate the vcan source first, so they
cannot demonstrate it. Part 2 needs a throttled-sink scenario (transfer > ring, dest
slower than source) proving reliable delivers 100% (no agent-side drop) once the agent
stops reading the origin when the ring is full.
