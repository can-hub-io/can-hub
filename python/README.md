# python-can-hub

Native [python-can](https://python-can.readthedocs.io) backend for
[can-hub](https://github.com/can-hub-io/can-hub): consume remote CAN interfaces
exported by can-hub agents, directly over the binary protocol — unix socket,
plain TCP, TLS or QUIC, with mTLS identity and TOFU pinning on the encrypted
transports. No bridge process in between.

```python
import can

bus = can.Bus(
    interface="canhub",
    channel="truck42/can0",
    url="quic://hub.example.com:7227",
)
bus.send(can.Message(arbitration_id=0x123, data=b"\xDE\xAD\xBE\xEF"))
for message in bus:
    print(message)
```

- `channel`: namespaced interface `agent/iface` (or the numeric id from
  `can-hub-client list`).
- `url`: omit to connect to the local hub unix socket.
- `state_dir`: client TLS identity + pin store location (tls/quic).
- `receive_own_messages`: standard python-can echo semantics.

Write access follows the hub client ACLs: if the ACL grants read-only, the
bus opens read-only and `send()` raises.

## Listing interfaces

`CanHubBus.list_interfaces()` asks a hub for the interfaces it exports. Each
entry is a python-can config dict you can splat straight into `can.Bus`:

```python
from canhub import CanHubBus
import can

for config in CanHubBus.list_interfaces(url="quic://hub.example.com:7227"):
    print(config["channel"])          # e.g. "can-agent/can0"
bus = can.Bus(**CanHubBus.list_interfaces()[0])   # first interface, local hub
```

It accepts the same connection arguments as the bus (`url`, `identity_cert`,
`identity_key`, `hub_fingerprint`, `state_dir`); omit `url` to query the local
hub unix socket.

This also wires python-can's discovery: `can.detect_available_configs("canhub")`
returns the same dicts. With no explicit target it reads the connection from the
environment — `CANHUB_URL`, `CANHUB_STATE_DIR`, `CANHUB_IDENTITY_CERT`,
`CANHUB_IDENTITY_KEY`, `CANHUB_HUB_FINGERPRINT` — falling back to the local hub
unix socket. So `CANHUB_URL=quic://hub.example.com:7227` points discovery at a
remote host without any code.

The wheel bundles `libcanhub.so` with the TLS/QUIC stack linked in
statically; the only runtime dependency is glibc.

## Testing

The unit tests stub the native library, so they run without a build:

```sh
pip install -e python/.[test]
pytest python/tests
```

The backend is also exercised end-to-end against real binaries (recv, send,
`list_interfaces`, and `can.detect_available_configs` discovery) by
`test/e2e/tests/python_can.robot`; run the whole bench with `make e2e`.

## Building from source

```sh
./scripts/build-python-wheel.sh        # host arch, glibc-tagged (local dev)
pip install python/dist/*.whl
```

Distributable manylinux wheels are built per architecture in a manylinux
container and repaired by auditwheel (needs docker, plus QEMU binfmt for the
cross arches):

```sh
./scripts/build-python-wheel.sh x86_64   # python/dist/x86_64/*.whl
./scripts/build-python-wheel.sh aarch64
./scripts/build-python-wheel.sh armv7l
```

The release workflow builds these three on native runners (aarch64/armv7l on
the arm64 hosted runner, no QEMU) plus a cross-compiled win_amd64 wheel, and
publishes all four to PyPI on a `v*` tag.
