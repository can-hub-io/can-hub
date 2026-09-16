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

## What the handshake negotiates

TLS 1.3 only, ALPN `canhub/0`, ED25519 signatures, X25519 key exchange with
secp256r1 as the fallback.

AES is offered only where the CPU implements it in hardware:

| CPU | Offered, most preferred first |
|---|---|
| x86-64 with AES-NI | `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256` |
| ARM with the ARMv8 crypto extensions, 64- or 32-bit | the same three |
| everything else | `TLS_CHACHA20_POLY1305_SHA256` |

Detection is at runtime, so one binary per architecture covers both cases: a
Raspberry Pi 5 offers AES, a Pi 4 offers ChaCha20.

The software AES fallback is a constant-time implementation around 600x slower
than the ChaCha20 path. A TLS 1.3 server picks from what the client offered, so
a build that never offers AES cannot be pushed onto it. There is no wire
signalling and no knob; mixed fleets work because ChaCha20 is always offered.

Interop is unaffected: an OpenSSL peer negotiates AES-GCM against a build that
offers it and ChaCha20-Poly1305 against one that does not, both RFC 8446 suites.

**QUIC Initial packets are the exception.** RFC 9001 fixes AES-128-GCM for them
whatever the connection later negotiates, so a hub with no hardware AES pays the
software cost on every connection attempt, including from peers it has never
heard of — one core saturates at roughly 250 attempts per second. On such a
machine, prefer `tls://` for an internet-facing hub, or put address validation
in front of it.

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
