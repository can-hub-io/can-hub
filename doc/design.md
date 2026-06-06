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

C (C11), cmake + gcc, static or dynamic linking. Make wraps cmake: `make release|debug|install|test`, `ARCH=x86_64|armhf|arm64` selects a toolchain file. Unit tests with CEST on the host. SOLID and light DDD, layered as:

- `protocol/` — shared wire format. `agent/` and `hub/` — one directory per bounded context, each with its own `domain/`, `ports/` and application wiring (`agent.c` + `agent_app.c`, `broker.c` + `hub_app.c`). All freestanding (no POSIX, no heap), portable to microcontrollers as-is.
- `<context>/ports/` — contracts in both directions, owned by the core: outbound Port structs the core calls (`TransportPort`, `CanPort`) and inbound Events structs the core implements (`TransportEvents`, `CanEvents`, obtained from `Agent_*Events`).
- `platform/<name>/` — adapters plus the concrete entry point per platform (`platform/linux/agent_main.c`: epoll, QUIC over ngtcp2, SocketCAN). Platform mains are deliberately concrete; nothing outside `platform/` includes anything from it.

### Transports

- QUIC (ngtcp2): primary. TLS 1.3 built in, reliable streams for control, unreliable datagrams (RFC 9221) for CAN frames — latest-wins, no head-of-line blocking.
- Reliable data plane (later): flows that need guaranteed delivery (ISOTP, UDS/firmware upgrades) can ride dedicated QUIC streams, reliable and ordered per flow, without blocking cyclic traffic. See protocol.md open questions.
- TCP (plaintext today, optional TLS later): fallback for networks that block UDP. Both planes share the single stream; frames lose latest-wins semantics (head-of-line blocking under WAN loss) — documented trade-off of the fallback.
- Plain UDP: discarded (2026-06-06). If the network passes UDP it passes QUIC, so plain UDP only removes encryption; a reliable-control-over-UDP lite (stop-and-wait in the adapter) and a TCP-control + UDP-data hybrid (needs a session token so the hub can correlate the flows) were evaluated and parked — revisit only if a microcontroller target cannot carry ngtcp2.
- SCTP: rejected. Middleboxes kill it, QUIC already provides multistreaming.
- UDP beacon discovery: discarded (2026-06-06). Clients are configured with the hub address and query the catalogue with LIST.

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

### Listeners and defaults

The hub always listens on a single unix domain socket (`/run/can-hub/hub.sock` by default, `--listen unix://<path>` overrides). It speaks the wire protocol and carries every local consumer: can-hub-client today, can-hub-cli admin traffic when the admin message family lands — demuxed by the HELLO role field, not by socket. Filesystem permissions as access control; it splits into a separate admin socket only if admin ever needs stricter permissions (decision 2026-06-06).

Without `--listen` flags the hub also serves tcp://7227 and, when `--cert`/`--key` are present, quic://7227 (same number, different protocols). Explicit `--listen tcp://`/`quic://` replaces the network defaults; default listeners that cannot start warn and are skipped, explicitly requested ones are fatal.

### Administration

`can-hub-cli` talks to the hub over the unix domain socket above using the same binary protocol with admin message types.

### Compatibility adapters (future)

Protocol compatibility needs no license: optional adapters may let socketcand or cannelloni clients talk to the hub, implemented from their public specs. Out of MVP scope.

Shims run as additional listener transports on the hub itself: the hub listens on TCP and QUIC for its own protocol by default, and each enabled shim (e.g. socketcand ASCII) adds another listener that translates to the broker's transport contract — legacy clients connect to the hub directly, no separate proxy process.

### Administration

Two admin surfaces over the same admin plane: `can-hub-cli` (unix socket) and a web panel (peers, interfaces, metrics, kick, ACLs). How the web panel is served (embedded HTTP server vs separate process consuming the admin socket) is decided when it lands.

## Stories

- E0 — spikes: ngtcp2 datagram proof of concept (vcan over QUIC), latency/CPU baseline against socketcand and cannelloni.
- E1 — protocol: binary wire format spec (doc/protocol.md) + encode/decode module with unit tests.
- E2 — agent: SocketCAN capture/injection, QUIC client, registration, reconnect with backoff.
- E3 — hub: QUIC server, registry, relay routing, SQLite persistence, unix socket admin, CLI.
- E4 — P2P phase 2: endpoint exchange, hole punch, path migration, relay fallback.
- E5 — hardening: backpressure, session limits, metrics, packaging.
