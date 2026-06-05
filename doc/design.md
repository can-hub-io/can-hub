# can-hub design

Status: draft
Date: 2026-06-05

## Motivation

socketcand and cannelloni assume the machine that owns the CAN interfaces is reachable: socketcand is a TCP server on the device, cannelloni a point-to-point tunnel. Both break behind NAT/firewalls (vehicles on cellular, machines on factory LANs) and neither has built-in authentication or encryption.

can-hub inverts the model: a device agent dials out to a central hub over QUIC with mutual TLS. Clients reach the buses through the hub, or directly P2P when the network allows it. Own binary protocol, clean-room, MIT licensed.

```
[can-hub-agent] --QUIC (mTLS)--> [can-hub] <--QUIC/TCP-- [clients]
                                     |
                                 [can-hub-cli] (unix socket)
```

## Decisions

### Language and build

C (C11), cmake + gcc, static or dynamic linking. Make wraps cmake: `make release|debug|install|test`, `ARCH=x86_64|armhf|arm64` selects a toolchain file. Unit tests with CEST on the host. SOLID and light DDD: domain entities, use cases, ports, adapters — no ceremonial hexagonal layering.

### Transports

- QUIC (ngtcp2): primary. TLS 1.3 built in, reliable streams for control, unreliable datagrams (RFC 9221) for CAN frames — latest-wins, no head-of-line blocking.
- Reliable data plane (later): flows that need guaranteed delivery (ISOTP, UDS/firmware upgrades) can ride dedicated QUIC streams, reliable and ordered per flow, without blocking cyclic traffic. See protocol.md open questions.
- TCP, with optional TLS: fallback for networks that block UDP.
- Plain UDP (LAN, unencrypted, cannelloni-style): possible later, low priority.
- SCTP: rejected. Middleboxes kill it, QUIC already provides multistreaming.

The transport is a port; adapters implement it. Adding a transport must not touch the domain.

### P2P (phased)

The hub is a rendezvous broker, WebRTC-pattern but not the WebRTC stack:

1. Phase 1 — relay: agent and client both dial out to the hub; the hub routes frames between them. Always works.
2. Phase 2 — hole punch: the hub exchanges observed UDP endpoints (STUN-like, free since QUIC is UDP) and the peers attempt a direct QUIC path; fall back to relay when punching fails (symmetric NAT / CGNAT). QUIC connection migration keeps the session across the path switch.

The path (relay vs direct) is abstracted from day one and transparent to the domain.

### Identity and naming

- Agent identity: TLS certificate fingerprint. Zero-config default: self-signed keypair generated on first start, trust-on-first-use pinning on both sides; provisioned CA certificates also supported.
- Each agent has a unique friendly name (explicit, or derived from origin as fallback).
- Interfaces are namespaced `agent_name/interface`, e.g. `truck42/can0`. Each interface knows its owning agent. The hub answers queries by interface, by agent name and by fingerprint.

### State and persistence

- Live registry (sessions, interfaces, subscriptions): in memory; it dies with the connection.
- Persistent data (agent identities, TOFU fingerprints, names, ACLs, config): SQLite, embedded, no external services. Redis rejected: deployment dependency, overkill.

### Concurrency

Single-threaded epoll event loop; ngtcp2 is callback-driven and fits. Threads only if benchmarks demand them.

### Agent portability (microcontroller target)

The agent core (domain + application) is freestanding: no POSIX, no file descriptors, no heap, no syscalls. Ports are structs of function pointers with a context pointer; events are pushed in (`Agent_OnCanFrame`, `Agent_OnControlMessage`, ...) and time is injected (`Agent_Tick(now_us)`). On Linux the epoll loop in `apps/agent` drives it; on a microcontroller an ISR/systick does, and the transport adapter can be QUIC, TCP or UDP (lwIP or bare) without touching the core.

### Administration

`can-hub-cli` talks to the hub over a local unix domain socket (filesystem permissions as access control) using the same binary protocol with admin message types.

### Compatibility adapters (future)

Protocol compatibility needs no license: optional adapters may let socketcand or cannelloni clients talk to the hub, implemented from their public specs. Out of MVP scope.

## Stories

- E0 — spikes: ngtcp2 datagram proof of concept (vcan over QUIC), latency/CPU baseline against socketcand and cannelloni.
- E1 — protocol: binary wire format spec (doc/protocol.md) + encode/decode module with unit tests.
- E2 — agent: SocketCAN capture/injection, QUIC client, registration, reconnect with backoff.
- E3 — hub: QUIC server, registry, relay routing, SQLite persistence, unix socket admin, CLI.
- E4 — P2P phase 2: endpoint exchange, hole punch, path migration, relay fallback.
- E5 — hardening: backpressure, session limits, metrics, packaging.
