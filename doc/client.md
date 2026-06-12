# can-hub-client — the consumer

Reference consumer: browse the catalogue, dump and inject frames, and bridge
remote buses into the tools you already use (socketcand clients, the whole
SocketCAN ecosystem via `attach`).

```
can-hub-client [--connect <url>] [--state-dir <path>] <command>
can-hub-client --show-identity [--state-dir <path>]
```

`--connect` takes `quic://host:port`, `tls://host:port`, `tcp://host:port`
or `unix://path`; omitted, it uses the hub's local unix socket
(`/run/can-hub/hub.sock`). On tls/quic the client has its own TLS identity
(generated on first use, `--show-identity` prints its fingerprint for
[ACLs](security.md#client-acls)) and pins the hub on first contact.

Interfaces are addressed by the numeric id from `list` or by namespaced name
`agent/iface`. Hub-side denials print as `hub error <code>: <detail>`.

## list

```sh
$ can-hub-client --connect tls://hub.example.com:7227 list
id         agent                            interface
1          truck42                          can0
2          truck42                          can1
```

## dump

```sh
can-hub-client dump truck42/can0
can-hub-client dump --no-echo truck42/can0
can-hub-client dump truck42/can0 123 700:7F0
```

- Optional trailing filters `<id>[:<mask>]` (hex) are applied **hub-side**
  (SocketCAN mask semantics: deliver when `id & mask == filter & mask`); a
  bare id matches exactly. Up to 16 filters; only matching frames cross the
  network.
- `--no-echo` suppresses the bus echo of this client's own injections;
  by default you see your own frames come back once they hit the wire.

## send

```sh
can-hub-client send truck42/can0 123#DEADBEEF
```

cansend syntax: hex id `#` hex payload. More than 3 id digits or an id above
0x7FF selects an extended (EFF) frame; a payload longer than 8 bytes selects
CAN FD. (`#R` for RTR and `##<flags>` for FD/BRS are not parsed yet.)
Requires a write grant when connected on tls/quic — see
[security](security.md#client-acls).

## attach — mirror into a local vcan

```sh
sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0   # once
can-hub-client --connect tls://hub.example.com:7227 attach truck42/can0 vcan0
```

Bidirectional mirror between a remote bus and a pre-existing local `vcan`
(no `CAP_NET_ADMIN` needed): `candump`, `cansend`, SavvyCAN, Wireshark and
python-can now work against the remote bus with zero changes. Without a
write grant it downgrades to read-only — frames still flow remote→local,
local writes are refused at the hub.

## socketcand — bridge server

```sh
can-hub-client --connect tls://hub.example.com:7227 socketcand
socketcand server on 127.0.0.1:29536, hub hub.example.com, beacon on
```

Runs a local [socketcand](https://github.com/linux-can/socketcand) TCP
server bridging every interface the client may read, plus the UDP discovery
beacon (port 42000). For socketcand-speaking tools, e.g. python-can:

```python
can.Bus(interface="socketcand", host="127.0.0.1", port=29536,
        channel="truck42/can0")
```

- `--listen [<bind-ip>:]<port>` moves it off loopback (socketcand clients
  are unauthenticated — expose deliberately); `--no-beacon` silences
  discovery.
- Rawmode only: BCM, ISO-TP and control modes are parsed and rejected.
- A bus without a write grant opens read-only; `< send >` on it is refused.
- The client Debian package ships this as `can-hub-socketcand.service`
  ([installation](installation.md#socketcand-bridge-optional)).

## Access control

What a client may read or inject is decided by the hub's ACLs, keyed by the
client's TLS fingerprint. Defaults: read everything, write nothing. Clients
on plaintext transports (unix, plain tcp) carry no fingerprint and are
network-trusted with full access. Full model in
[security](security.md#client-acls).

## Programmatic access

- Python: `pip install python-can-hub`, a native python-can backend —
  [python/README.md](../python/README.md).
- C: [libcanhub](libcanhub.md), the embeddable client library (Linux and
  Windows).
