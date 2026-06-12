# Security model

What identifies a peer, who trusts whom, and how to lock both sides down.
Defaults are zero-config and TOFU; everything here tightens them.

## Identity is the TLS fingerprint

On the encrypted transports (quic, tls) every party — hub, agent, client —
has a self-signed ED25519 certificate, auto-generated under its state dir on
first start (or provisioned via `--cert`/`--key` on the hub). The SHA-256
fingerprint of that certificate **is** the identity: it is never sent inside
messages, the connection itself is authenticated. Both encrypted listeners
require client certificates (mTLS).

| Who verifies whom | Mechanism |
|---|---|
| agent → hub | pins the hub fingerprint per host:port on first contact (`known_hubs`); a changed hub fingerprint refuses the handshake |
| client → hub | same TOFU pin store, written on first `tls://`/`quic://` dial |
| hub → agent | requires a client certificate; pins `agent_name → fingerprint` at first REGISTER (`hub.db`); a known name with a new fingerprint is rejected |
| hub → client | requires a client certificate; the fingerprint is the ACL subject |

One pin covers both `quic://` and `tls://` on the same host:port — the
listeners share the certificate.

## Plaintext transports are network-trusted

Plain `tcp://` and the unix socket carry no identity: no pinning, no
allowlist, no ACLs — peers there are trusted with full read and write.

- Plain tcp binds 127.0.0.1 by default. Expose it
  (`--listen tcp://0.0.0.0:7228`) only on a trusted network or VPN; the hub
  warns when you do.
- The unix socket's access control is its filesystem permissions; it is also
  the only place the admin role is accepted (an admin HELLO over TCP or QUIC
  is disconnected).

## Locking down agents

Default is TOFU: the first agent to claim a name pins it. To accept only
pre-authorized devices, run the hub with `--require-known-agents` and enroll
each one `authorized_keys`-style:

```sh
# on the device — public, safe to share; the private key never leaves it
can-hub-agent --show-identity
9abfc913fddfe9bad4cab50ba024210d81dba02140103d5019923b29adf818e1

# on the hub host
can-hub-cli pins add truck42 9abfc913fddfe9bad4cab50ba024210d81dba02140103d5019923b29adf818e1
```

An unknown fingerprint is rejected with **no server state created**
(flood-proof, unlike an approval queue). The agent keeps retrying on its
backoff, so authorizing it while it retries is enough. A re-keyed device
needs `can-hub-cli pins delete <name>` before it can pin again.

The allowlist applies to quic/tls only — plain tcp carries no fingerprint
and bypasses it (see above).

## Client ACLs

Defaults: any client may read every interface, none may inject. Grants
override that per client and per interface:

```sh
can-hub-client --show-identity                # the subject fingerprint

can-hub-cli acl add * */* rw                  # everyone read+write everywhere
can-hub-cli acl add <fp> truck42/* ro         # this client: truck42 read-only
can-hub-cli acl add <fp> truck42/can0 none    # ...except can0, fully denied
can-hub-cli acl delete <fp> truck42/can0
can-hub-cli acl
```

- Subject: a client fingerprint or `*`. Object: `agent/iface` with `*` on
  either side. Level: `none` (no read, no write), `ro`, `rw`.
- Resolution is most-specific-wins with the subject dominating: a rule
  naming the fingerprint always beats a `*` rule; within the same subject
  rank the narrower object wins (`agent/can0` > `agent/*` > `*/*`). No
  matching rule → read yes, write no.
- Enforcement is at OPEN (read/write denied up front) **and** at the frame
  boundary: an injected frame on a non-writable channel is dropped even if
  the client lied at OPEN.
- Clients on plaintext transports carry no fingerprint and always get full
  access — ACLs only bind on tls/quic.

## Scope

Pre-1.0: the wire protocol is version 0 and unfrozen
([protocol](protocol.md)). can-hub is designed for telemetry, diagnostics
and bus replication — explicitly **not** for control loops over the WAN.
