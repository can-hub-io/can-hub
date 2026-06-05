# E0 spike results — CAN over QUIC datagrams

Date: 2026-06-05. Environment: Ubuntu 24.04, gcc 13, loopback (vcan0 -> client -> QUIC -> server -> vcan1), 8-core x86_64.

## What was validated

`tunnel.c` bridges SocketCAN and QUIC datagrams (RFC 9221) with ngtcp2 + GnuTLS: one CAN frame per datagram, epoll + timerfd event loop, TLS 1.3 handshake, bidirectional. `run-bench.sh` measures one-way latency (candump correlation, single clock) and CPU at 500/2000/5000 frames/s, 5000 frames per rate.

## Numbers (ngtcp2 1.23.0, typical clean run)

| Rate | Delivered | p50 | p99 | max | CPU client | CPU server |
|---|---|---|---|---|---|---|
| 500 fps | 100% | 127 us | 466 us | 10 ms | 4.3% | 3.2% |
| 2000 fps | 100% | 48 us | 3.3 ms | 33 ms | 5.3% | 3.5% |
| 5000 fps | 100% | 13 us | 91 us | 0.8 ms | 5.6% | 4.5% |

Under induced loss the connection recovers: cwnd shrinks, a few frames are dropped while congestion-blocked (latest-wins semantics, exactly what we want for CAN), then traffic resumes. p99 spikes to tens of ms during recovery windows.

## Comparison vs plain TCP and UDP

`plain_tunnel.c` bridges the same frames over plain TCP (TCP_NODELAY) and plain UDP, no encryption. `run-compare.sh`, 3 runs, values are typical (loopback):

| Transport | p50 @5000fps | p99 @5000fps | CPU client/server @5000fps | Delivery |
|---|---|---|---|---|
| QUIC datagrams (TLS 1.3) | 13-14 us | 104-148 us | ~5.5% / ~4.5% | 100% |
| TCP (NODELAY, plaintext) | 16 us | 130-357 us | ~3.4% / ~3.0% | 100% |
| UDP (plaintext) | 12 us | 88-109 us | ~3.0% / ~1.8% | 100% |

Reading: QUIC costs ~1-2 us p50 over raw UDP and roughly 2x the CPU of plaintext transports — while also doing TLS 1.3 encryption that the others lack. All three are far from saturation at 5000 fps. Loopback hides QUIC's real advantage: on lossy/jittery WAN paths TCP suffers head-of-line blocking and retransmits stale frames, while QUIC datagrams drop stale data (latest-wins). The comparison validates that QUIC's overhead is negligible for the encrypted, NAT-traversing transport can-hub needs.

## Findings

- **Distro ngtcp2 0.12 is unusable for datagrams.** DATAGRAM-only packets do not arm the PTO/loss-detection timer (ngtcp2 issue #697, fixed in 0.14.0). One lost ACK and the connection wedges permanently: bytes_in_flight stays at cwnd forever, every subsequent datagram is rejected, only the 30 s idle timeout remains. Reproduced reliably; gone with 1.23.
- API breaks between 0.12 and 1.x are minor for our usage: `ngtcp2_conn_info` rename and the mandatory `original_dcid_present` flag on the server.
- ngtcp2 ergonomics: BYO everything (UDP sockets, event loop, timers via `ngtcp2_conn_get_expiry`/`handle_expiry`, TLS session glue). ~700 lines for a minimal correct tunnel. Verbose but predictable; the crypto helper callbacks remove most TLS pain.
- GnuTLS backend works; quictls/wolfSSL remain options for static-link footprint later.
- Event-loop pitfall: a blocking timerfd deadlocks if `timerfd_settime` rearms (clearing readability) between `epoll_wait` and the timer read in the same event batch — the read blocks forever and the tunnel freezes silently. Fix: `TFD_NONBLOCK`. Generalize in can-hub: every fd in the loop must be non-blocking.

## Decisions for can-hub

1. Vendor ngtcp2, pinned >= 1.x, built from source in our build (FetchContent or submodule). Never rely on distro packages.
2. QUIC datagrams confirmed as the data plane: latency and CPU have ample margin at 5000 fps on loopback.
3. The transport adapter must own the expiry-timer contract (arm on every egress flush, `handle_expiry` + flush on fire) — getting this wrong produces silent wedges, so it needs unit tests around a mocked clock.
4. Frames are dropped, not queued, when congestion-blocked (latest-wins). A small TX ring for burst absorption can be evaluated in E2.
5. At 5000 fps the kernel CAN socket rx buffer occasionally overflows before the tunnel reads it (run variance: 49-100% delivered, QUIC itself delivered 100% of what was read). E2 needs SO_RCVBUF sizing and read-batching (recvmmsg or drain-loop per epoll event).
