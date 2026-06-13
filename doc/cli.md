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
| `interface set <agent>/<iface> bitrate <bps>` | reconfigure a bus bitrate |
| `interface up\|down <agent>/<iface>` | bring a bus up or down |
| `pins` | pinned agent identities |
| `pins add <name> <fingerprint>` | authorize an agent ([enrollment](security.md#locking-down-agents)) |
| `pins delete <name>` | drop a pin so a re-keyed agent can pin again |
| `acl` | client read/write grants |
| `acl add <fp\|*> <agent\|*>/<iface\|*> none\|ro\|rw` | grant a permission level ([semantics](security.md#client-acls)) |
| `acl delete <fp\|*> <agent\|*>/<iface\|*>` | drop a grant |

## Notes

- All counters are cumulative and monotonic — sample twice for rates.
- `interface set`/`up`/`down` is forwarded to the owning agent and applied
  on the device via rtnetlink: the agent process needs `CAP_NET_ADMIN`
  ([agent — capabilities](agent.md#capabilities)). A bitrate change cycles
  the link down/set/up and disrupts every consumer; `agent unreachable`
  means the agent dropped mid-request.
- Kicked agents normally reconnect on their backoff. To keep one out,
  `pins delete` it first and run the hub with `--require-known-agents`.
- Failures print `hub error <code>: <detail>` from the hub's ERROR message.
- Command output goes to stdout, diagnostics to stderr. Tune the stderr
  verbosity with `--log-level error|warn|info|debug` (default `info`) or the
  `CAN_HUB_LOG` environment variable.
