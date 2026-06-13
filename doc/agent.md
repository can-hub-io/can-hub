# can-hub-agent — the device daemon

Runs next to the CAN hardware, dials out to the hub (NAT-friendly — no
inbound port on the device) and exports local SocketCAN interfaces.

```
can-hub-agent --connect quic://<host>:<port>|tls://<host>:<port>|tcp://<host>:<port>
              --name <agent-name> [--state-dir <path>] <can-if> [...]
can-hub-agent --show-identity [--state-dir <path>]
```

`--name` is the namespace the interfaces are published under
(`truck42/can0`); names must be unique per hub. Up to 16 interfaces per
agent.

## What it does

- Captures every frame on the exported buses (with capture timestamps) and
  ships it to the hub; injects frames the hub forwards from authorized
  clients.
- Injected frames are confirmed by their **bus echo**: subscribers see an
  injection only once it actually reached the wire, in real bus order. If
  the TX fails, nobody is lied to.
- Reconnects forever on its own backoff. Authorizing or fixing the hub side
  while the agent is retrying is enough — no restart needed.

## Identity and pinning

On the encrypted transports the agent has a stable ED25519 identity
(`agent.crt`/`agent.key` in the state dir), generated on first start. It
pins the hub's fingerprint per host:port (`known_hubs`) on first contact and
refuses a hub whose fingerprint changed.

`--show-identity` prints the agent's fingerprint for the hub allowlist —
public, safe to share; the private key never leaves the device. Use the same
`--state-dir` the agent runs with.

Registration rejections the agent logs:

- *agent name pinned to a different fingerprint* — the hub knows this name
  under another identity. Re-keyed device? `can-hub-cli pins delete <name>`
  on the hub.
- *unknown agent* — the hub runs `--require-known-agents` and this
  fingerprint is not enrolled. See
  [security](security.md#locking-down-agents).
- *name/interface collision or hub registry full*.

## Running as a service

The Debian package installs `can-hub-agent.service`, configured through
`/etc/can-hub/agent.conf` (`HUB_URL`, `INTERFACES`, optional `AGENT_NAME` —
defaults to the machine hostname under systemd; the bare binary always
requires `--name`). State lives in `/var/lib/can-hub-agent`.

```sh
sudo editor /etc/can-hub/agent.conf
sudo systemctl enable --now can-hub-agent
```

### Capabilities

The unit grants `CAP_NET_RAW` only (raw SocketCAN sockets). Remote interface
reconfiguration — `can-hub-cli interface set <agent>/<iface> bitrate <bps>`,
`interface up|down` — uses rtnetlink on the device and needs
`CAP_NET_ADMIN`. Add it with a drop-in if you use that feature:

```ini
# /etc/systemd/system/can-hub-agent.service.d/netadmin.conf
[Service]
CapabilityBoundingSet=CAP_NET_RAW CAP_NET_ADMIN
AmbientCapabilities=CAP_NET_RAW CAP_NET_ADMIN
```

## No hardware? vcan

```sh
sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
can-hub-agent --connect tcp://127.0.0.1:7228 --name demo vcan0
```

Anything `cansend`/`candump` does on `vcan0` flows through the hub.

## Portability

The agent core is freestanding C11 — no POSIX, heap or syscalls — and
compiles for microcontrollers as-is; only the transport and CAN adapters are
platform code. See [design](design.md) for the port architecture.

## Logging

On a healthy start the agent prints `connecting to …`, then `connected to
hub`, then `registered as <name>: <iface>=ch<channel> …` so you can see the
buses went live. Rejections print the reason (unknown agent, identity
mismatch, name/interface collision) and drops print `connection lost,
reconnecting in <n>s`. Diagnostics go to stderr at four levels — `error`,
`warn`, `info`, `debug`; set the verbosity with `--log-level <level>` (default
`info`) or the `CAN_HUB_LOG` environment variable, the flag wins. Under systemd
the lines carry a syslog priority prefix for `journalctl -p`.
