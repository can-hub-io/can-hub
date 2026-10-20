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

## Transactions

A transaction hands a request/response sequence to the agent, which runs it at
its bus: each exchange costs a bus round trip instead of a network one (UDS
flashing, broadcast upgrades; see the protocol's Transactions section). Open
the bus with `reliable=True`:

```python
from canhub import Advance, Pattern, Step, Template, TransactionAborted

response = Template(
    advance_on=Pattern(0x7E8, data=b"\x00\x76", data_mask=b"\x00\xff"),
    abort_on=Pattern(0x7E8, data=b"\x00\x7f", data_mask=b"\x00\xff"),
    extend_on=Pattern(0x7E8, data=b"\x00\x7f\x00\x78", data_mask=b"\x00\xff\x00\xff"),
    mode=Advance.COUNT,
    timeout=0.05,
)
try:
    with bus.transaction(exclusive=True) as transaction:
        transaction.template(0, response)
        transaction.send(Step(arbitration_id=0x7E0, data=block) for block in blocks)
        result = transaction.wait()
except TransactionAborted as aborted:
    print(aborted.result.reason, aborted.result.failing_step, aborted.result.frame)
```

`send()` takes any iterable, a generator included, and hands it over as the
agent's window allows. `wait()` returns a `TransactionResult` or raises
`TransactionAborted` with it. Leaving the `with` block on an exception
(Ctrl-C included) cancels the transaction at the agent. A roster step waits for
a set of nodes: `bus.transaction(roster=range(32), origin=Origin(mask=0x1F))`
tells the nodes apart by `(id & mask) >> shift` (or by payload bytes), and the
result names who answered (`answered`) and who did not (`missing`).
`exclusive=True` keeps other clients' frames off the bus meanwhile; their
`send()` raises `CanOperationError`.

A [transaction script](../doc/transaction-scripts.md) describes the same in a
file, and runs as it does from `can-hub-client txn`:

```python
from canhub import ScriptError, TransactionAborted

try:
    result = bus.run_script("flash.txn", progress=lambda progress, total: print(progress.completed_steps, total))
except ScriptError as error:
    print(error.line, error)                      # flash.txn:5: no template has this name
except TransactionAborted as aborted:
    print(aborted.result.reason, sorted(aborted.result.missing))
```

`progress(progress, total_steps)` runs a few times a second; returning `False`
cancels the transaction, and an exception raised in it (Ctrl-C included)
cancels it and propagates. It must not use the bus.

### ISO-TP steps

A template with `isotp=Isotp(...)` makes each of its steps one ISO-TP message
(up to 4095 bytes in `data`) that the agent segments, honouring the ECU's flow
control, and whose reassembled answer the patterns match. Begin with
`isotp=True`; `extend_timeout` is the wait after a response pending (UDS P2*):

```python
from canhub import Advance, Isotp, Pattern, Step, Template

transfer = Template(
    advance_on=Pattern(0x7E8, data=b"\x76\x00"),
    abort_on=Pattern(0x7E8, data=b"\x7f"),
    extend_on=Pattern(0x7E8, data=b"\x7f\x00\x78", data_mask=b"\xff\x00\xff"),
    mode=Advance.COUNT,
    timeout=0.05,
    extend_timeout=5.0,
    abort_on_mismatch=True,
    isotp=Isotp(0x7E8),                 # padding 0xCC, normal addressing, classic CAN
)
with bus.transaction(exclusive=True, isotp=True) as transaction:
    transaction.template(1, transfer)
    transaction.send(
        Step(template=1, arbitration_id=0x7E0, data=bytes([0x36, counter]) + block, expect=bytes([0x76, counter]))
        for counter, block in blocks
    )
    transaction.wait()
```

`Isotp(response_id, tx_address=0x10, rx_address=0xF1)` is extended addressing
(one address for mixed), `fd=True` sends CAN FD frames of `data_length` bytes,
and `block_size`/`st_min` are the flow control the agent answers with. Each
message is handed over whole, as the agent's window takes it.

### UDS

`bus.uds()` runs UDS requests (ISO 14229) at the agent as one transaction, the
answers checked by the UDS rules: a positive answer starts with the service id
+ 0x40, a negative one (`7F <service> <code>`) raises `UdsNegativeResponse`,
`7F <service> 78` makes the request wait `p2_extended`, and a request whose
sub-function carries the suppress-positive-response bit (`3E 80`) does not
wait:

```python
from canhub import Isotp, UdsNegativeResponse

try:
    with bus.uds(0x7E0, Isotp(0x7E8), p2=0.15, p2_extended=5.0, exclusive=True) as ecu:
        ecu.request(b"\x10\x02")                                   # expects 50
        ecu.request(b"\x31\x01\xff\x00", expect=b"\x71\x01\xff\x00")  # checks the start of the answer
        ecu.request(b"\x34\x00\x44" + address + size)
        ecu.transfer(firmware, block_size=4093)                    # 36 <counter> <block>, answered 76 <counter>
        ecu.request(b"\x37")
except UdsNegativeResponse as refused:
    print(hex(refused.service), hex(refused.code))
```

SecurityAccess needs the manufacturer's seed/key algorithm, which runs on the
client between two exchanges; do it beforehand. python-can-isotp and udsoncan
also work over `CanHubBus` as over any python-can bus, but then the client
does the segmentation and every frame crosses the network; `bus.uds()` keeps
it at the agent.

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
