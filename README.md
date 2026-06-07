# can-hub

CAN bus over the network, done right. Devices behind NAT export their
SocketCAN interfaces to a central hub over QUIC with mutual TLS; clients
anywhere consume them with the tools they already use. A modern alternative
to socketcand and cannelloni.

```
[can-hub-agent] --quic/tls/tcp--> [can-hub] <--quic/tls/tcp/unix-- [can-hub-client]
   truck42                          the hub                          anywhere
   can0, can1                                                        list / dump / send
                                    [can-hub-cli]
                                    admin on the hub host
```

## Why

- **Works through NAT and firewalls.** Agents and clients both dial out;
  the hub rendezvous in the middle. No port forwarding on the device side.
- **Zero-config security.** Self-signed ED25519 identities generated on
  first start, trust-on-first-use pinning on both sides, client
  certificates required on the encrypted transports. The TLS fingerprint is
  the device identity — impostors are rejected, not just logged.
- **The right transport for CAN.** Control rides reliable QUIC streams;
  frames ride QUIC datagrams — latest-wins, no head-of-line blocking. A
  lost cyclic frame is repaired by the next cycle, not retransmitted late.
- **Physical-bus semantics.** Injected frames become visible to the other
  subscribers through their real bus echo: if the TX never reached the
  wire, nobody is lied to, and ordering is the bus ordering.
- **Fleet-shaped, not link-shaped.** One hub, many agents, a queryable
  catalogue (`truck42/can0`), per-peer and per-interface traffic counters,
  and an admin plane to inspect and kick.
- **Freestanding core.** Everything outside `src/platform/` is C11 without
  POSIX, heap or syscalls — the agent core compiles for microcontrollers
  as-is.

## Binaries

| Binary | Role |
|---|---|
| `can-hub` | the hub: registry, frame relay, admin plane |
| `can-hub-agent` | device daemon: exports local SocketCAN interfaces |
| `can-hub-client` | consumer: `list`, `dump`, `send` |
| `can-hub-cli` | hub administration over the local unix socket |

## Build

```sh
make release            # -O2 into build/x86_64/release
make test               # unit tests (CEST)
```

Requirements: cmake >= 3.16, ninja, gcc, GnuTLS development headers
(`libgnutls28-dev` on Debian/Ubuntu). The first configure needs network:
ngtcp2 and the SQLite amalgamation are fetched and built statically.

## Quickstart

On the hub host (defaults: quic://7227 UDP, tls://7227 TCP, plain
tcp://7228, and a local unix socket):

```sh
can-hub
```

On the device (TLS identity auto-generated, hub fingerprint pinned on first
contact):

```sh
can-hub-agent --connect quic://hub.example.com:7227 --name truck42 can0 can1
```

From anywhere — `list` shows the catalogue, the `id` column feeds `dump`
and `send`:

```sh
$ can-hub-client --connect tls://hub.example.com:7227 list
id         agent                            interface
1          truck42                          can0
2          truck42                          can1

$ can-hub-client --connect tls://hub.example.com:7227 dump 1
(1780847295.078524) 123 [4] DE AD BE EF

$ can-hub-client --connect tls://hub.example.com:7227 send 1 123#DEADBEEF
```

Administration, on the hub host:

```sh
can-hub-cli status
can-hub-cli agents
can-hub-cli agents show truck42
can-hub-cli clients
can-hub-cli peers kick 0x40000003
can-hub-cli pins forget truck42      # allow a re-keyed agent to pin again
```

No CAN hardware around? `sudo ip link add dev vcan0 type vcan && sudo ip
link set up vcan0`, run the agent against `vcan0` and talk to it with
`cansend`/`candump` on one side and `can-hub-client` on the other.

## Transports

| Listener | Default | Notes |
|---|---|---|
| quic:// | UDP 7227 | primary: streams + datagrams, mTLS |
| tls:// | TCP 7227 | TCP twin of quic, same identity and pinning, mTLS |
| tcp:// | TCP 7228 | plaintext, for intranets and constrained devices |
| unix:// | /run/can-hub/hub.sock | local consumers and all administration |

Plaintext transports carry no identity and skip pinning; the admin role is
accepted only on the unix socket.

Listeners take an optional bind address (`--listen tcp://10.0.0.5:7228`,
default 0.0.0.0), and explicit `--listen` flags replace the network
defaults — so disabling a transport is listing the ones you want:
`can-hub --listen tls://7227 --listen quic://7227` runs without plain TCP.

## Status

Pre-1.0. The wire protocol is version 0 and may change until v1 is frozen
(see [doc/protocol.md](doc/protocol.md)). Designed for telemetry,
diagnostics and bus replication — explicitly **not** for control loops over
the WAN.

## Documentation

- [doc/design.md](doc/design.md) — architecture and decisions
- [doc/protocol.md](doc/protocol.md) — wire protocol (CC-BY-4.0,
  independent implementations welcome)
- [CONTRIBUTING.md](CONTRIBUTING.md) — style, tests, CLA

## License

Dual-licensed: [AGPL-3.0](LICENSE) for everyone, with a
[commercial license](LICENSE.commercial) available for proprietary use.
