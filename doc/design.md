# can-hub design

Status: draft
Date: 2026-06-05

## Motivation

socketcand and cannelloni assume the machine that owns the CAN interfaces is reachable: socketcand is a TCP server on the device, cannelloni a point-to-point tunnel. Both break behind NAT/firewalls (vehicles on cellular, machines on factory LANs) and neither has built-in authentication or encryption.

can-hub inverts the model: a device agent dials out to a central hub over QUIC with mutual TLS. Clients reach the buses through the hub, or directly P2P when the network allows it. Own binary protocol, clean-room, dual-licensed AGPL-3.0 + commercial (protocol spec CC-BY-4.0).

```
[can-hub-agent] --quic/tls/tcp--> [can-hub] <--quic/tls/tcp-- [clients]
                                      |            (+unix for local clients)
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
- TCP (plaintext on 7228, TLS-over-TCP on 7227): fallback for networks that block UDP. Both planes share the single stream; frames lose latest-wins semantics (head-of-line blocking under WAN loss) — documented trade-off of the fallback.
- Plain UDP: discarded (2026-06-06). If the network passes UDP it passes QUIC, so plain UDP only removes encryption; a reliable-control-over-UDP lite (stop-and-wait in the adapter) and a TCP-control + UDP-data hybrid (needs a session token so the hub can correlate the flows) were evaluated and parked — revisit only if a microcontroller target cannot carry ngtcp2.
- SCTP: rejected. Middleboxes kill it, QUIC already provides multistreaming.
- UDP beacon discovery: discarded (2026-06-06). Clients are configured with the hub address and query the catalogue with LIST.

The transport is a port; adapters implement it. Adding a transport must not touch the domain.

### Injection echo (decision 2026-06-07)

Client-injected frames become visible to the other subscribers of an interface through their **bus echo**, never by hub-side fan-out: the agent enables `CAN_RAW_RECV_OWN_MSGS`, the kernel returns the TX on completion (`MSG_CONFIRM`), and that echo travels back to the hub and fans out like any bus frame. Truth from the wire — if the TX never made it, nobody sees it — and real bus ordering. The hub tags each injection with an origin token (FRAME route_flags); the agent's EchoCorrelator pairs TX↔echo locally, where every loss mode is synchronously observable (failed writes drop their entry), and returns the token so the hub can suppress the echo towards its own originator when the channel was opened with the suppress-own-echo flag. Hub-side fan-out was rejected: it lies under TX failure and reorders against genuine bus traffic.

### P2P (phased)

The hub is a rendezvous broker, WebRTC-pattern but not the WebRTC stack:

1. Phase 1 — relay: agent and client both dial out to the hub; the hub routes frames between them. Always works.
2. Phase 2 — hole punch: the hub exchanges observed UDP endpoints (STUN-like, free since QUIC is UDP) and the peers attempt a direct QUIC path; fall back to relay when punching fails (symmetric NAT / CGNAT). QUIC connection migration keeps the session across the path switch.

The path (relay vs direct) is abstracted from day one and transparent to the domain.

### Identity and naming

- Agent identity: TLS certificate fingerprint (SHA-256 of the DER certificate). Zero-config default: self-signed ED25519 keypair generated on first start (state dir `/var/lib/can-hub`, per-user fallback `~/.local/state/can-hub`, `--state-dir` overrides), trust-on-first-use pinning on both sides — the agent pins the hub per host:port (known_hubs), the hub requires a client certificate on the encrypted transports (mTLS on QUIC and TLS-over-TCP, same hub identity for both) and pins agent_name to fingerprint at REGISTER (IdentityStorePort, SQLite). Changed fingerprints are rejected. The same host:port pin covers quic:// and tls:// because both listeners present the same certificate. can-hub-client generates its own identity the first time it dials tls://. Provisioned `--cert`/`--key` also supported. Plaintext transports (tcp, unix socket) carry no fingerprint and bypass pinning.
- Client authorization (ACLs, decision 2026-06-07): grants live in SQLite (client_acls, behind AuthorizationPort) keyed by the client TLS fingerprint (subject) and the namespaced interface `agent_name/interface_name` (object). Any of the three may be the literal `*` wildcard, giving object scopes exact (`agent/iface`), agent-wide (`agent/*`) and global (`*/*`), and subject scopes specific fingerprint or any (`*`). Each grant carries a permission level: `none` (deny read and write), `ro` (read only), `rw` (read and write). Resolution is most-specific-wins, subject dominating: a rule naming the fingerprint always beats a `*`-subject rule, then object specificity breaks within a subject rank (exact > agent-wide > global) — no ties possible. With no matching rule the baseline is read-open, no-write. Enforcement is in the broker at OPEN: read denied → `OPEN_STATUS_READ_DENIED`; the OPEN `want write` flag on a non-writable channel → `OPEN_STATUS_WRITE_DENIED`. A client FRAME on a non-writable channel is dropped regardless (the security boundary behind the honest-client OPEN check). Clients on fingerprint-less transports (unix, plain tcp) are network-trusted and may always read and write. Admin: `can-hub-cli acl [add|delete] <fingerprint|*> <agent|*>/<iface|*> [none|ro|rw]` and `acl list`; the client reports its fingerprint with `can-hub-client --show-identity`.
- Agent allowlist (authorized_keys model, decision 2026-06-07): default is TOFU (first contact auto-pins, zero-config). `--require-known-agents` (locked mode) only accepts fingerprints already pinned and rejects unknown ones with no server state created — flood-proof, unlike an approval queue. Enrollment is out-of-band like SSH `authorized_keys`: `can-hub-agent --show-identity` prints the agent's fingerprint (public, safe to share — the private key never leaves the device), the admin adds it with `can-hub-cli pins add <name> <fingerprint>`, and the agent's existing reconnect-backoff lands the next attempt. Locked mode applies only to the encrypted transports (quic/tls) that carry a fingerprint; plain tcp carries none and defaults to binding 127.0.0.1 (a deliberate `--listen tcp://0.0.0.0:...` exposes it on a trusted network/VPN, where it bypasses the allowlist — the hub warns). Client authorization (read/write ACLs) is the next layer.
- Each agent has a unique friendly name (explicit, or derived from origin as fallback).
- Interfaces are namespaced `agent_name/interface`, e.g. `truck42/can0`. Each interface knows its owning agent. The hub answers queries by interface, by agent name and by fingerprint.
- Interface configuration (decision 2026-06-08): remote bitrate and link up/down, admin-only. Reconfiguring a bus disrupts every consumer and needs CAP_NET_ADMIN on the device, so it stays on the admin plane (cli over the unix socket) — no client-facing config ACL yet. `cli interface set <agent>/<iface> bitrate <bps> | up | down`. The admin names the interface by its namespaced pair; the hub resolves it and forwards an IFCONFIG to the owning agent (a hub→agent control message, new direction), then relays the agent's reply back synchronously (pending-request correlation in the broker; agent-unreachable if it drops mid-flight). The agent applies through CanPort.configure — SocketCAN via rtnetlink, set-bitrate sequences down/set/up; an MCU agent maps the same port to bxCAN registers. Bit timings and FD data parameters are deferred (additive message extension later). socketcand parity was the trigger but its surface is just bittiming + 3 ctrlmode flags; we ship the high-value subset first.

### State and persistence

- Live registry (sessions, interfaces, subscriptions): in memory; it dies with the connection.
- Persistent data (agent identities, TOFU fingerprints, names, ACLs, config): SQLite, embedded, no external services. Redis rejected: deployment dependency, overkill. Implemented: `hub.db` in the state dir holds agent_identities (name → fingerprint, behind IdentityStorePort); a legacy known_agents pin file is imported once on start and renamed.

### Concurrency

Single-threaded epoll event loop; ngtcp2 is callback-driven and fits. Threads only if benchmarks demand them.

### Agent portability (microcontroller target)

The agent core (domain + application) is freestanding: no POSIX, no file descriptors, no heap, no syscalls. Ports are structs of function pointers with a context pointer; events are pushed in (`Agent_OnCanFrame`, `Agent_OnControlMessage`, ...) and time is injected (`Agent_Tick(now_us)`). On Linux the epoll loop in `platform/linux/agent_main.c` drives it; on a microcontroller an ISR/systick does, and the transport adapter can be QUIC, TCP or UDP (lwIP or bare) without touching the core.

### Listeners and defaults

The hub always listens on a single unix domain socket (`/run/can-hub/hub.sock` by default, `--listen unix://<path>` overrides). It speaks the wire protocol and carries every local consumer: can-hub-client and can-hub-cli admin traffic — demuxed by the HELLO role field, not by socket. Filesystem permissions as access control; it splits into a separate admin socket only if admin ever needs stricter permissions (decision 2026-06-06).

Without `--listen` flags the hub also serves quic://7227 (UDP), tls://7227 (TCP, same number HTTP/3-style) and plain tcp://7228 (decision 2026-06-07: plaintext stays a default for intranet and microcontroller agents, on its own port so 7227 is secure on both stacks); the TLS identity is auto-generated when `--cert`/`--key` are absent and shared by the quic and tls listeners. Explicit `--listen tcp://`/`quic://`/`tls://` replaces the network defaults; default listeners that cannot start warn and are skipped, explicitly requested ones are fatal. Network listeners accept an optional bind address, `--listen <scheme>://[<bind-ip>:]<port>` (default 0.0.0.0) — e.g. plain tcp pinned to an intranet interface: `--listen tls://7227 --listen quic://7227 --listen tcp://10.0.0.5:7228`. Disabling a default transport = listing the others explicitly.

### Administration

`can-hub-cli` talks to the hub over the unix domain socket above using the same binary protocol with admin message types. The grammar is noun-first (`<noun> [verb] [args]`, omitted verb = list) so future admin surfaces (ACLs, bridges) join without reshaping it:

```
status                 hub counters
peers                  every live connection (including pre-HELLO and admin peers)
peers kick <peer-id>   disconnect any peer (id as printed by the tables)
agents                 live agents with their interface count
agents show <name>     agent detail: interfaces and consuming clients
agents kick <name>     disconnect an agent by registered name
clients                open client channels (one row per channel, idle clients included)
interfaces             interface catalogue with subscribers and traffic counters
pins                   pinned TOFU identities
pins add <name> <fp>   authorize an agent fingerprint (authorized_keys style)
pins delete <name>     drop a pin so a re-keyed agent can pin again
acl                    list client ACL grants
acl add <fp|*> <agent|*>/<iface|*> none|ro|rw   grant a client permission level
acl delete <fp|*> <agent|*>/<iface|*>           drop a grant
```

The hub accepts the admin HELLO role only on local transports: a peer claiming it over TCP or QUIC is disconnected. Clients carry no self-declared name — identity stays the TLS fingerprint, so interaction with clients goes through their peer id (`clients`/`agents show` print it, `peers kick` consumes it); if readable handles are ever needed they will be admin-assigned aliases bound to fingerprints, decided together with the ACL design.

### Compatibility adapters

Protocol compatibility needs no license: adapters let socketcand or cannelloni clients reach hub interfaces, implemented from their public specs. socketcand is dual GPL-2.0-only OR BSD-3-Clause; we reimplement it clean-room and would take any borrowed code under the BSD arm (the GPL arm is incompatible with the AGPL/commercial dual-license).

**socketcand (shipped, client-hosted):** `can-hub-client socketcand` runs a local socketcand TCP server (default `127.0.0.1:29536`) plus the UDP discovery beacon (port 42000). It dials the hub as an ordinary client and bridges: a socketcand `< open agent/iface >` is resolved against a cached hub `LIST` to an interface id, then `OPEN`ed; received frames become `< frame … >`, a socketcand `< send … >` becomes a hub `FRAME`. Rawmode only for now (BCM/ISO-TP/control parsed and rejected). The bridge core is freestanding (`src/socketcand/`), the TCP server and beacon are Linux adapters (`src/platform/linux/socketcand/`). The hub is untouched — **authorization is the existing client ACLs** the bridge already carries as a hub client (a write-denied bus opens read-only; `< send >` on it is refused). Each socketcand connection maps to one hub session channel.

**attach (shipped, client-hosted):** `can-hub-client attach <interface-id> <vcan>` mirrors a remote bus into a pre-existing local `vcan`, bidirectionally, so the whole SocketCAN ecosystem (candump, SavvyCAN, Wireshark, python-can) reaches the remote bus unmodified, no `CAP_NET_ADMIN`. Pure composition: a freestanding mirror core (`src/mirror/`, an Agent-like state machine HELLO→OPEN→pump) drives the existing client transport on one side and the `SocketCanAdapter` on the other. Hub→local writes the decoded `FRAME` to the vcan; local→hub reads the vcan and sends a `FRAME` on the open channel. `OPEN` carries `WANT_WRITE`; on `WRITE_DENIED` it reopens read-only (mirror still flows remote→local). Echo loop is broken on two fronts: `SUPPRESS_OWN_ECHO` stops the hub returning our own injections, and the client's vcan socket sets `CAN_RAW_RECV_OWN_MSGS` off so locally-written frames are not re-read and re-sent (the agent keeps it on for TX↔echo correlation — hence the per-open flag on `SocketCanAdapter_Open`).

Both shims are deliberately client-hosted rather than hub listeners: it keeps the hub free of a second, unauthenticated plane and reuses the existing client ACLs instead of inventing a parallel exposure model. A hub-side listener shim (for protocols better terminated centrally) remains an option as an additional listener transport that translates to the broker's transport contract.

### Web admin (future)

A web panel (peers, interfaces, metrics, kick, ACLs) shares the admin plane with `can-hub-cli`. How it is served (embedded HTTP server vs separate process consuming the admin socket) is decided when it lands.

## Stories

Delivered:

- Spikes — ngtcp2 datagram proof of concept (vcan over QUIC), latency/CPU baseline against socketcand and cannelloni.
- Protocol — binary wire format spec (doc/protocol.md) + encode/decode module with unit tests.
- Agent — SocketCAN capture/injection, QUIC client, registration, reconnect with backoff.
- Hub — QUIC server, registry, relay routing, SQLite persistence, unix-socket admin, CLI.
- Hardening & security — TOFU identities, mTLS, TLS-over-TCP, admin plane, backpressure/eviction, metrics.
- Authorization & control — client ACLs, SUBSCRIBE id-mask filters, interface configuration (bitrate/link), socketcand shim.
- Compatibility — `attach` mirrors a remote bus into a local vcan (bidirectional).

Pending work is tracked as GitHub issues, grouped by milestone (priority order — 0 users today, so adoption first, freeze last):

- **Adoption & product** — namespaced interface names, bus-to-bus bridge rules, cansend syntax gaps, error-frame export.
- **Reach** — microcontroller agent (lwIP + bxCAN), R-UDP transport, web admin panel.
- **Protocol v1 freeze** — version negotiation, timestamp encoding decision, reconnect-matrix verification, then freeze (deprioritized while there are no users to break compat with).
- **Backlog** — P2P phase 2 and deferred epics/polish.
