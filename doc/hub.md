# can-hub — the hub

Central rendezvous: agents dial in and register their CAN interfaces,
clients open them, the hub routes frames and serves the admin plane.

```
can-hub [--listen <scheme>://[<bind-ip>:]<port>] ...
        [--cert <pem> --key <pem>] [--state-dir <path>]
        [--require-known-peers]
can-hub --show-identity [--cert <pem>] [--state-dir <path>]
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
- Listeners are dual-stack: the `0.0.0.0` default binds both IPv4 and IPv6, so
  clients reach the hub over either family. Bind a specific address with an IPv4
  literal (`--listen quic://192.0.2.1:7227`) or a bracketed IPv6 literal
  (`--listen quic://[2001:db8::1]:7227`, `[::1]` for IPv6 loopback). On a host
  without IPv6 the listener falls back to IPv4-only. Clients resolve hostnames to
  either family, so an AAAA-only FQDN connects.
- Any explicit network `--listen` **replaces all network defaults**, so
  disabling a transport is listing the ones you want:
  `can-hub --listen tls://7227 --listen quic://7227` runs without plain TCP.
- A default listener that cannot start (e.g. the unix socket path is not
  writable) warns and is skipped; an explicitly requested one is fatal.
- The admin role is accepted only on the unix socket; an admin HELLO over
  TCP or QUIC is disconnected. Filesystem permissions on the socket are the
  admin access control.
- The socket is created mode `0660` (owner + group read/write), group-owned by
  the process group (the `can-hub` group under the Debian package). **Any user
  in that group can use the socket, and the socket carries the full admin plane
  with no per-client identity — so group membership grants unrestricted CAN
  read/write and hub administration (pins, ACLs, kick).** Treat adding a user to
  the `can-hub` group like granting sudo: `usermod -aG can-hub <user>`.

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

`--require-known-peers` rejects agents whose fingerprint is not already
pinned, instead of auto-pinning on first contact, and clients no ACL rule
names. `can-hub-cli pending` lists what was turned away. Enrollment flow in
[security — locking down agents and clients](security.md#locking-down-agents-and-clients).

## State

- Live state (sessions, interface catalogue, open channels) is in memory and
  dies with the connection — an agent that drops disappears from `list`.
- Persistent state lives in `hub.db` (SQLite, in the state dir): agent
  name→fingerprint pins, client ACLs and bridge routes. No external services.
- State dir: `/var/lib/can-hub`, falling back to `~/.local/state/can-hub`
  when not writable; `--state-dir` overrides.
- `--show-identity` prints the hub's fingerprint, what agents and clients
  pass as `--hub-fingerprint`. Use the same `--state-dir` (or `--cert`) the
  hub runs with.

## Bridging buses

The hub can inject what one bus carries into another, across agents and
sites: a fleet's diagnostic ids collected on a lab bus, a production bus
mirrored to a test bench, two buses joined into one.

```sh
can-hub-cli bridge add truck42/can0 lab/can1            # everything, one way
can-hub-cli bridge add truck42/can0 lab/can1 7DF 7E0:7F0  # only these ids
can-hub-cli bridge add lab/can1 truck42/can0            # and back: one bus
can-hub-cli bridge
source                   destination              state    forwarded    dropped      ids
truck42/can0             lab/can1                 active   1520         0            7DF 7E0:7F0
lab/can1                 truck42/can0             active   88           0            all
```

- A route goes one way: what the source bus carries, its bus echo of client
  injections included, is injected into the destination. Several routes may
  share a source (fan-out) or a destination (fan-in).
- Ids are hex, `<id>` for that id only or `<id>:<mask>` for every id that
  matches under the mask, the same syntax as `can-hub-client dump`. No ids
  bridges everything. Error frames are never bridged.
- One hop only: the hub marks what it injects and never bridges that frame
  again, so routes in both directions join two buses without a loop. Clients
  see such a frame with the bridged flag: `(bridged)` in `can-hub-client
  dump`, `CANHUB_FRAME_FLAG_BRIDGED` in libcanhub, `message.bridged` in
  python-can-hub. A second agent on the very same
  physical bus would see the injection as plain traffic; do not export one
  bus through two agents that are both bridged.
- Routes name interfaces, so they wait for agents that are offline (state
  `waiting`; frames for an offline destination count as dropped) and apply
  again when they reconnect. They live in `hub.db` and survive restarts.
  The hub holds up to 64.
- The agent reports which of its interfaces carry CAN FD (MTU 72; a vcan
  is FD unless its MTU is set to 16). The hub never injects an FD frame into
  a classic bus, whether a route, a group or a client sends it: it drops and
  counts it (the route's or group's `dropped`, the hub's frame drops).
  `can-hub-cli interfaces` shows each bus as `fd` or `classic`, `-` for an
  agent too old to report it, and `interface set ... fd on|off` updates it.
- Bridged frames travel like client injections: lossy and paced to the
  destination. Use bridges for telemetry, diagnostics and replication, not
  for control loops over the network.
- Administration only: routes bypass client ACLs. The [web panel](web.md)
  manages them under Bridges, with the `bridges.manage` permission.

### Groups

A group joins several buses into one: whatever arrives on any member is
injected into every other member, never back into the one it came from.

```sh
can-hub-cli group add fleet 7DF 7E0:7F0       # create, or replace the ids
can-hub-cli group join fleet truck42/can0
can-hub-cli group join fleet truck43/can0
can-hub-cli group join fleet lab/can1 receives
can-hub-cli group
fleet  forwarded 2210  dropped 0  ids 7DF 7E0:7F0
  truck42/can0             online   both      ids all
  truck43/can0             online   both      ids all
  lab/can1                 online   receives  ids all
```

- A member may take part one way: `group join fleet lab/can1 receives`
  feeds it the group but keeps its own frames out, `sends` the opposite
  (a production bus mirrored, never written). Ids after the direction
  (up to 8) limit what that member sends, on top of the group ids. Joining
  again changes a member's direction and ids.
- With routes alone that takes a route per ordered pair of buses; a group
  is one line per bus. A frame from truck42 reaches lab directly, not
  through another member: one hop, as for routes.
- A bus reached by several routes and groups is injected once.
- A group is also an interface, `group/<name>`, listed with the others:
  clients open it like any bus. They see what every member captures (not
  what the hub injected), and what they write is injected into every
  member. One attach mirrors a whole group into a local vcan:
  `can-hub-client attach group/fleet fleet0`. Client ACLs apply to
  `group/<name>`; it cannot be opened reliable, configured, or used as a
  route end or a member. The agent name `group` is reserved.
- Up to 16 groups of up to 16 members; names are 1-15 of
  `A-Z a-z 0-9 . _ -`. Members may be offline and apply when they connect.
  Groups live in `hub.db` with the routes.

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

- 512 peers at once (agents, clients and admin sessions together); the
  static tables cost about 100 MB of RAM.
- 63 clients writing through one agent at a time with own-echo suppression;
  a writer past that still writes, but sees its own echo.
- 16 interfaces per agent; interface names 1–15 chars (Linux `IFNAMSIZ`).
- Agent names up to 127 chars.
- 16 CAN id filters per open channel.
- Frame payloads up to 64 bytes (CAN FD).

## Logging

Diagnostics go to stderr at four levels — `error`, `warn`, `info`, `debug`.
Set the verbosity with `--log-level <level>` (default `info`) or the
`CAN_HUB_LOG` environment variable; the flag wins. Under systemd each line
carries a syslog priority prefix so `journalctl -p err` (and friends) filter by
level; on a terminal the lines read plainly as `can-hub: <message>`.
