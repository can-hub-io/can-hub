# can-hub — the hub

Central rendezvous: agents dial in and register their CAN interfaces,
clients open them, the hub routes frames and serves the admin plane.

```
can-hub [--listen <scheme>://[<bind-ip>:]<port>] ...
        [--cert <pem> --key <pem>] [--state-dir <path>]
        [--require-known-agents]
```

## Listeners

| Listener | Default | Notes |
|---|---|---|
| `quic://` | UDP 0.0.0.0:7227 | primary: control on streams, frames on datagrams (latest-wins), mTLS |
| `tls://` | TCP 0.0.0.0:7227 | TCP twin of quic for UDP-hostile networks, same identity and pinning, mTLS |
| `tcp://` | TCP 127.0.0.1:7228 | plaintext, no identity; loopback unless explicitly bound elsewhere |
| `unix://` | `/run/can-hub/hub.sock` | always on; local clients and **all administration** |

Rules:

- `--listen <scheme>://[<bind-ip>:]<port>` — bind-ip defaults to 0.0.0.0
  (plain tcp: 127.0.0.1). `--listen unix://<path>` moves the socket.
- Any explicit network `--listen` **replaces all network defaults**, so
  disabling a transport is listing the ones you want:
  `can-hub --listen tls://7227 --listen quic://7227` runs without plain TCP.
- A default listener that cannot start (e.g. the unix socket path is not
  writable) warns and is skipped; an explicitly requested one is fatal.
- The admin role is accepted only on the unix socket; an admin HELLO over
  TCP or QUIC is disconnected. Filesystem permissions on the socket are the
  admin access control.

Note: the Debian package overrides these defaults via `/etc/can-hub/hub.conf`
(QUIC + unix socket only) — see [installation](installation.md#hub).

## Identity

quic and tls share one TLS identity. When `--cert`/`--key` are absent, a
self-signed ED25519 identity is generated under the state dir on first start
and reused afterwards; agents and clients pin its fingerprint per host:port
on first contact (TOFU). Provisioned PEM certificate and key are accepted
via `--cert`/`--key`. Both encrypted listeners require client certificates
(mTLS): the peer's certificate fingerprint **is** its identity. Details in
[security](security.md).

`--require-known-agents` rejects agents whose fingerprint is not already
pinned, instead of auto-pinning on first contact. Enrollment flow in
[security — locking down agents](security.md#locking-down-agents).

## State

- Live state (sessions, interface catalogue, open channels) is in memory and
  dies with the connection — an agent that drops disappears from `list`.
- Persistent state lives in `hub.db` (SQLite, in the state dir): agent
  name→fingerprint pins and client ACLs. No external services.
- State dir: `/var/lib/can-hub`, falling back to `~/.local/state/can-hub`
  when not writable; `--state-dir` overrides.

## Counters

`can-hub-cli status` exposes hub-wide frame counters; `peers`, `clients` and
`interfaces` carry per-peer, per-channel and per-interface ones. All
counters are cumulative and monotonic — there is no reset; sample twice and
diff for rates (the [web panel](web.md) does this for you).

| Counter | Meaning |
|---|---|
| received | valid data-plane frames accepted by the hub |
| forwarded | frame deliveries that reached a peer |
| dropped | deliveries dropped because the destination's TX budget was full |
| unroutable | valid frames with no route (channel nobody opened) |

Peer ids printed by the tables encode the transport in their high bits
(unix `0x4...`, quic `0x8...`, tls `0xC...`, plain tcp low ids) — useful to
spot how a peer is connected.

## Limits

- 16 interfaces per agent; interface names 1–15 chars (Linux `IFNAMSIZ`).
- Agent names up to 127 chars.
- 16 CAN id filters per open channel.
- Frame payloads up to 64 bytes (CAN FD).
