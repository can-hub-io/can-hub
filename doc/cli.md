# can-hub-cli — administration

Admin tool for a running hub. It speaks only `unix://` — the hub accepts the
admin role exclusively on its local unix socket, so administration is
local-by-design (use SSH or the [web panel](web.md) for remote access).

```
can-hub-cli [--connect unix://<path>] <command>      # default: /run/can-hub/hub.sock
```

The socket belongs to the `can-hub` user under the packaged hub; run the cli
as root or as a member of the `can-hub` group.

## Commands

Noun-first grammar; an omitted verb lists.

| Command | Effect |
|---|---|
| `status` | hub frame counters and peer/agent/client/interface counts |
| `peers` | every live connection, including pre-HELLO and admin peers |
| `peers kick <peer-id>` | disconnect any peer (id as printed, `0x...` or decimal) |
| `agents` | live agents with interface counts |
| `agents show <name>` | one agent: its interfaces and consuming clients |
| `agents kick <name>` | disconnect an agent by registered name |
| `clients` | open client channels, one row per channel |
| `interfaces` | interface catalogue with subscriber and traffic counters |
| `interface show <agent>/<iface>` | the settings the bus holds now: link, state, bit timing, modes, TX queue |
| `interface set <agent>/<iface> <setting>...` | reconfigure a bus, every setting in one step ([settings](#interface-settings)) |
| `interface up\|down <agent>/<iface>` | bring a bus up or down |
| `pins` | pinned agent identities |
| `pins add <name> <fingerprint>` | authorize an agent ([enrollment](security.md#locking-down-agents-and-clients)) |
| `pins delete <name>` | drop a pin so a re-keyed agent can pin again |
| `acl` | client grants |
| `acl add <fp\|*> <agent\|*>/<iface\|*> none\|r\|rw\|rwa` | grant a permission level ([semantics](security.md#client-acls)) |
| `acl delete <fp\|*> <agent\|*>/<iface\|*>` | drop a grant |
| `pending` | identities the hub turned away, to authorize ([enrolling](security.md#enrolling-what-knocked)) |

## Interface settings

`interface set` takes the words of `ip link set ... type can`, any number of
them, and the agent applies them all at once:

| Setting | Value |
|---|---|
| `bitrate`, `dbitrate` | nominal and CAN FD data bit rate, bit/s |
| `sample-point`, `dsample-point` | a fraction, `0.875` |
| `fd`, `listen-only`, `loopback`, `one-shot`, `triple-sampling`, `berr-reporting` | `on` or `off` |
| `restart-ms` | restart after bus-off, ms (`0` = never) |
| `txqueuelen` | TX queue length |
| `up`, `down` | the link state to leave the bus in |

```sh
can-hub-cli interface set truck42/can0 bitrate 500000 sample-point 0.875 fd on dbitrate 2000000
can-hub-cli interface set truck42/can0 restart-ms 100
can-hub-cli interface show truck42/can0
```

Both print the settings the device holds afterwards, as the kernel reports
them (it may round a sample point). A failure prints the kernel's reason,
e.g. `apply failed: Operation not supported` for bit timing on a vcan.

## Notes

- All counters are cumulative and monotonic — sample twice for rates.
- `interface set`/`up`/`down` is forwarded to the owning agent and applied
  on the device via rtnetlink: the agent process needs `CAP_NET_ADMIN`
  ([agent — capabilities](agent.md#capabilities)). Bit timing, modes and
  `restart-ms` need the link down, so the agent cycles it once for the whole
  request and every consumer sees the bus drop briefly; `agent unreachable`
  means the agent dropped mid-request. Clients with an `rwa` grant can do the
  same with `can-hub-client configure`.
- Kicked agents normally reconnect on their backoff. To keep one out,
  `pins delete` it first and run the hub with `--require-known-peers`.
- Failures print `hub error <code>: <detail>` from the hub's ERROR message.
- Command output goes to stdout, diagnostics to stderr. Tune the stderr
  verbosity with `--log-level error|warn|info|debug` (default `info`) or the
  `CAN_HUB_LOG` environment variable.
