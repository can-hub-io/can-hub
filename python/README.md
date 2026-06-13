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

The wheel bundles `libcanhub.so` with the TLS/QUIC stack linked in
statically; the only runtime dependency is glibc.

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
