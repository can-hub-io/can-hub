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
- `reliable`: carry the channel on a dedicated reliable QUIC stream (lossless,
  ordered) instead of the lossy datagram plane. Needs a `quic://` url and a hub
  that advertises the capability; otherwise the open fails.
- `ignore_rx_error_frames`: drop the controller error frames
  (`is_error_frame`), received by default as on python-can's socketcan
  interface.

Received messages are `canhub.CanHubMessage`, a `can.Message` with one more
attribute: `bridged` is true when the hub injected the frame into this bus
along a [bridge](../doc/hub.md#bridging-buses) route or group.

Write access follows the hub client ACLs: if the ACL grants read-only, the
bus opens read-only and `send()` raises.

The bus is thread-safe: a `can.Notifier` receiving in its thread while other
threads call `send()` is supported. Calls into the native session are
serialised first come, first served, and a blocking receive gives way to a
waiting send within about 10 ms.

## Drop counters

Two drops happen inside the library and never reach the hub, so the hub's own
counters cannot show them: `send()` raises when the pace relayed by the hub
leaves no budget for the frame, and the receive ring overwrites its oldest
entry when you read slower than the bus produces. `drop_counters()` returns
both as monotonic totals, so a quiet bus is distinguishable from a lossy one:

```python
bus.drop_counters()     # {"rate_limited": 0, "ring_dropped": 12}
```

## Configuring the interface

`configure()` reconfigures the bus's interface on its agent, everything in one
step, with the keywords of `ip link set ... type can`; `interface_settings()`
reads it back. The bus must be opened by its `agent/iface` name, and the hub
allows it only with an `rwa` ACL grant (unix and plain tcp clients are trusted).

```python
bus.configure(bitrate=500000, sample_point=0.875, fd=True, data_bitrate=2000000)
bus.configure(restart_ms=100, txqueuelen=1000, termination=120, listen_only=False, up=True)
from canhub import BitTiming
bus.configure(bit_timing=BitTiming(tq=125, prop_seg=6, phase_seg1=7, phase_seg2=2, sjw=1))
bus.interface_settings()
# InterfaceSettings(up=True, state='error-active', bitrate=500000, sample_point=0.875, ...)
```

Both return an `InterfaceSettings` with `None` where the device reports
nothing (a vcan has no bit timing). A refusal raises `CanOperationError` with
the reason.

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
