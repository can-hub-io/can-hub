# can-hub documentation

## Using

- [Quick start](quick-start.md) — first frame in ten minutes, sandbox and real deployment.
- [Installation](installation.md) — Debian packages, static binaries, python wheel, building from source.
- [Security](security.md) — identities, TOFU pinning, agent allowlist, client ACLs.

## Tool reference

- [can-hub](hub.md) — the hub: listeners, identity, state, counters, limits.
- [can-hub-agent](agent.md) — the device daemon: exporting buses, service deployment, capabilities.
- [can-hub-client](client.md) — list/dump/send, `attach` vcan mirroring, the socketcand bridge.
- [can-hub-cli](cli.md) — administration command reference.
- [can-hub-web](web.md) — the web admin panel: install, users and permissions, REST API.

## Embedding

- [libcanhub](libcanhub.md) — the C client library (Linux and Windows).
- [python-can-hub](../python/README.md) — native python-can backend.

## Internals

- [design.md](design.md) — architecture and the reasoning behind every decision.
- [protocol.md](protocol.md) — the wire protocol, version 0 (CC-BY-4.0; independent implementations welcome).
- [wire-change-runbook.md](wire-change-runbook.md) — changing or adding a wire message: checklist and the index of every codec site.
- [CONTRIBUTING.md](../CONTRIBUTING.md) and [CODE_STYLE.md](../CODE_STYLE.md).
