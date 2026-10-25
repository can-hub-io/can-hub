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
flashing, broadcast upgrades; see the protocol's Transactions section). It
speaks the words of [transaction scripts](../doc/transaction-scripts.md) and of
the C API: a step sends a frame and waits for a `Response`. Open the bus with
`reliable=True`:

```python
from canhub import Match, Response, TransactionAborted

transfer = Response(
    Match(0x7E8, data={1: 0x76}),
    timeout=0.05,
    failure=Match(0x7E8, data={1: 0x7F}),
    pending=Match(0x7E8, data={1: 0x7F, 3: 0x78}),
    timeout_after_pending=5.0,
)
try:
    with bus.transaction(exclusive=True) as transaction:
        for counter, block in enumerate(blocks):
            transaction.send(0x7E0, block, transfer, where=Match(0x7E8, data={2: counter}))
    result = transaction.result
except TransactionAborted as aborted:
    print(aborted.result.reason, aborted.result.failing_step, aborted.result.frame)
```

A `Match` compares the id, except the bits in `ignored_bits`, and the data
bytes given in `data` (index to value, or to `Masked(mask, value)`); a
standard and an extended id (`extended=True`) never match each other, and
`Match(None, ...)` meets any id. `where`
adds to the response for one step: its id replaces the response's, its bytes
add to them. Nothing reaches the agent before the first step; each step waits
for the agent's window, a slice at a time, so the bus keeps receiving
meanwhile. Leaving the `with` block finishes the transaction and raises
`TransactionAborted` with the report when it did not complete; leaving it on
an exception (Ctrl-C included) cancels it at the agent, and so does shutting
the bus down while it runs.

A response with `nodes` waits for an answer from each of them, told apart by
the bits `success.ignored_bits` leaves free (or by data byte `node_byte`);
`allowed_missing` may stay silent, and `continue_on_timeout=True` moves on
when the time runs out. `on_roster` gets a `RosterResult` (`step`, `missing`)
for every such step that moved on without all its nodes, and the result names
who was missing at the step that failed:

```python
alive = Response(Match(0x700, ignored_bits=0x0F), timeout=0.5, nodes=range(16), continue_on_timeout=True)

with bus.transaction(on_roster=lambda roster: print(roster.step, sorted(roster.missing))) as transaction:
    transaction.send(0x7FF, b"\x01", alive)
```

`exclusive=True` keeps other clients' frames off the bus meanwhile; their
`send()` raises `CanOperationError`. When the client may lag behind the bus (a
slow link, data produced as it goes), `idle_timeout=60` lets the agent wait
that long for steps, and `keepalive=Keepalive(0x7E0, b"\x02\x3e\x80",
every=2.0)` is the frame it sends meanwhile, for instance to keep a device
session open.

A [transaction script](../doc/transaction-scripts.md) describes the same as
text, and runs as it does from `can-hub-client txn`:

```python
from canhub import ScriptError, TransactionAborted

try:
    result = bus.run_txn_script_file("flash.txn", progress=lambda progress, total: print(progress.completed_steps, total))
except ScriptError as error:
    print(error.line, error)                      # flash.txn:5: no response has this name
except TransactionAborted as aborted:
    print(aborted.result.reason, sorted(aborted.result.missing))
```

`bus.run_txn_script(text, base_directory=...)` runs a script held as text, its
`read(...)` files resolving against `base_directory`. `progress(progress,
total_steps)` runs a few times a second; returning `False` cancels the
transaction, and an exception raised in it (Ctrl-C included) cancels it and
propagates. It and `on_roster` must not use the bus.

### ISO-TP

`send_message(link, data, response)` sends one ISO-TP message (up to 4095
bytes) over an `IsotpLink`: the agent segments it on `tx_id`, honouring the
ECU's flow control, and matches the response against the answer it
reassembles on `rx_id`:

```python
from canhub import IsotpLink, Match, Response

ecu = IsotpLink(0x7E0, 0x7E8, padding=0xCC)
transfer = Response(
    Match(0x7E8, data={0: 0x76}),
    timeout=0.05,
    failure=Match(0x7E8, data={0: 0x7F}),
    pending=Match(0x7E8, data={0: 0x7F, 2: 0x78}),
    timeout_after_pending=5.0,
    on_mismatch_abort=True,
)
with bus.transaction(exclusive=True) as transaction:
    for counter, block in blocks:
        transaction.send_message(ecu, bytes([0x36, counter]) + block, transfer, where=Match(0x7E8, data={1: counter}))
```

`IsotpLink(tx_id, rx_id, tx_address=0x10, rx_address=0xF1)` is extended
addressing (one address for mixed), `fd=True` sends CAN FD frames of
`fd_data_length` bytes, and `block_size`/`st_min` are the flow control the
agent answers with. A response on the link matches `rx_id` exactly;
`transaction.expect(response, link=ecu)` waits for a message without sending.

UDS itself (sessions, security access, negative response codes) is the job of
a diagnostics library. python-can-isotp and udsoncan work over `CanHubBus` as
over any python-can bus, but then the client segments and every frame crosses
the network; a udsoncan connection over agent-side ISO-TP is tracked in #272.

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
