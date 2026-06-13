# Changing or adding a wire message

A wire change touches several files across two languages. None of them is hard,
but they are easy to miss one at a time — a too-small buffer, an unsynced Rust
offset, or a forgotten sender surfaces as a runtime failure, not a compile
error. This is the checklist and the index of every codec site so the work is a
tick-list instead of sequential discovery.

`doc/protocol.md` is the source of truth for the wire format. Change it first,
then bring the code in line with it. The C and Rust codecs are hand-synced
against that spec — nothing enforces their agreement at build time, so the spec
is what they must both match.

## Checklist

1. **Spec.** Update `doc/protocol.md`: the message type in *Control message
   types*, the byte layout under *Control payload layouts*, and the body-size
   number. Keep fields naturally aligned with explicit zeroed reserved padding.
2. **C codec.** `src/protocol/<message>_message.{c,h}`: the `<MSG>_BODY_SIZE`
   macro, the struct, `Encode`/`Decode`. One module per message type; decode is
   a bounds check plus overlay, no parsing loops.
3. **Control buffer guard.** If you add a new control message, add a
   `static_assert` line for it in `src/protocol/control_buffer.h`. Encode
   buffers are sized from `CONTROL_MESSAGE_MAX_WIRE_SIZE` (= header + the
   largest body, currently REGISTER); if a body grows past it, the asserts turn
   the overflow into a compile error. Bump the max there if needed — never
   hand-pick a buffer size at a call site.
4. **Senders.** Encode the message where it is produced. HELLO has a single
   builder (`HelloMessage_Build`); other messages are encoded at their call
   sites (see the index). Adding a field to HELLO touches one place; for other
   messages, edit every sender listed below.
5. **Rust mirror.** If the message crosses the web daemon boundary (HELLO,
   admin requests, admin replies), update `web/daemon/src/protocol/`. The Rust
   offsets and sizes are duplicated by hand — they must match `protocol.md`
   byte for byte.
6. **Tests.** Unit test in `test/unit/protocol/test_<message>_message.cpp`
   (round-trip encode/decode, invalid/truncated cases). Update the
   `test/mocks/broker_driver.c` mock if the hub-facing contract changed, and the
   Rust tests for anything the daemon encodes or decodes.
7. **Build and verify.** `make test` and `make release` (release catches the
   app-side encode sites the unit build does not), then the e2e suite for
   anything on the live path.

## Index of codec sites

### Spec
- `doc/protocol.md` — *Control message types*, *Control payload layouts*, *Data plane: FRAME*.

### C codec (`src/protocol/`)
- `<message>_message.{c,h}` — `<MSG>_BODY_SIZE`, struct, `Encode`/`Decode`.
- `message_header.{c,h}` — the 4-byte header shared by every message.
- `wire.{c,h}` — little-endian field read/write helpers.
- `control_buffer.h` — `CONTROL_MESSAGE_MAX_WIRE_SIZE` and the per-message `static_assert` guards.

### Senders (C)
- HELLO: `HelloMessage_Build` in `src/protocol/hello_message.c`, called by `src/client/client.c`, `src/agent/agent.c`, `src/mirror/mirror_app.c`, `src/socketcand/socketcand_bridge.c`, `src/platform/linux/cli_main.c`, and the `test/mocks/broker_driver.c` mock.
- REGISTER / IFCONFIG_REPLY: `src/agent/agent.c`.
- LIST / OPEN / CLOSE / SUBSCRIBE / FRAME: `src/client/client.c`, `src/mirror/mirror_app.c`, `src/socketcand/socketcand_bridge.c` (per the message).
- Admin requests: `src/platform/linux/cli_main.c`.
- Admin replies, ERROR, OPEN_ACK, REGISTER_ACK, PING, LIST_REPLY: `src/hub/broker.c`.

### Rust mirror (`web/daemon/src/protocol/`)
- `admin.rs` — admin request encoders and admin reply decoders (offsets and `*_SIZE` constants duplicated from the spec).
- `mod.rs` — message header, message types, HELLO encode.
- Tests: `web/daemon/tests/`.

### Tests and mocks
- `test/unit/protocol/test_<message>_message.cpp` — codec round-trip.
- `test/mocks/broker_driver.c` — hub-facing transport mock.
- `test/unit/hub/test_broker.cpp` — broker behaviour against decoded messages.

## Port event contracts

Inbound port events (`HubTransportEvents`, `TransportEvents`, `CanEvents`) are
deliberately asymmetric, and that is correct:

- **Adapter-supplied connection metadata** goes in an `*Info` struct passed by
  pointer, with `context` / `peer_id` / `now_us` positional. Adding a field
  then never changes the signature across transports. `HubPeerConnectInfo`
  (TLS fingerprint, socket origin, transport kind, local flag) is the example.
- **Wire payload** is passed as `(data, size)` and decoded by the core, not the
  adapter. These events carry no metadata record to grow, so they stay
  positional — a `{data, size}` wrapper would be a span, not a contract.
- **Identity-only signals** (`on_peer_disconnected`, `on_peer_writable`,
  `on_connected`) carry nothing beyond `peer_id`; they stay positional rather
  than wrap an empty struct.
