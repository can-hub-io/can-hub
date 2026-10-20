# libcanhub — embeddable C client

C client library for consuming hub interfaces from your own programs. It is
the engine behind the [python backend](../python/README.md) and the Windows
port; the API is a small blocking surface over the full transport stack —
unix socket, plain TCP, TLS and QUIC, with mTLS identity and TOFU pinning
built in.

Artifacts: `libcanhub.a` and `libcanhub.so` plus
[`include/canhub.h`](../include/canhub.h), built by `make release` and
installed by `make install`.

## API

```c
#include "canhub.h"

CanHubConnectConfig config = { 0 };
config.struct_size = sizeof(config);            // ABI guard, required
config.url = "quic://hub.example.com:7227";     // NULL = local unix socket
config.connect_timeout_ms = 5000;               // -1 = block forever

CanHubSession *session = canhub_connect(&config);

canhub_open(session, "truck42/can0", CANHUB_OPEN_FLAG_WRITE, 5000);

CanHubFrame frame = { 0 };
frame.can_id = 0x123;
frame.length = 2;
frame.payload[0] = 0xCA; frame.payload[1] = 0xFE;
canhub_send(session, &frame);

while (canhub_recv(session, &frame, 1000) == CANHUB_RECEIVED) {
    /* ... */
}

canhub_close(session);
```

| Function | Notes |
|---|---|
| `canhub_connect(config)` | NULL on failure; optional `state_directory`, `certificate_path`/`key_path`, `hub_fingerprint` (pre-pin instead of TOFU) |
| `canhub_list(session, out, max, timeout_ms)` | catalogue; returns the count |
| `canhub_open(session, name, flags, timeout_ms)` | by namespaced name (`agent/iface`) or numeric id; flags `CANHUB_OPEN_FLAG_WRITE`, `CANHUB_OPEN_FLAG_NO_ECHO`, `CANHUB_OPEN_FLAG_RELIABLE` (lossless ordered QUIC stream, `quic://` only; `CANHUB_ERR_RELIABLE_UNSUPPORTED` if the hub lacks the capability), `CANHUB_OPEN_FLAG_ERROR_FRAMES` (also receive the bus error frames, `CANHUB_CAN_ID_FLAG_ERR` set) |
| `canhub_set_filters(session, filters, count)` | hub-side CAN id mask filters, replace semantics, max 16 |
| `canhub_recv(session, frame, timeout_ms)` | `CANHUB_RECEIVED`, `CANHUB_ERR_TIMEOUT`, or a failure; `frame.flags` carries `CANHUB_FRAME_FLAG_FD`, `CANHUB_FRAME_FLAG_BRS` and `CANHUB_FRAME_FLAG_BRIDGED` (the hub injected the frame along a [bridge](hub.md#bridging-buses); ignored on send) |
| `canhub_send(session, frame)` | needs an open writable channel; `CANHUB_ERR_RATE_LIMITED` when the local pacer drops the frame, `CANHUB_ERR_LEASED` while another client's exclusive [transaction](#transactions) holds the interface |
| `canhub_stats(session, stats)` | local drop counters: `frames_rate_limited` (pacer), `frames_ring_dropped` (receive ring overrun) |
| `canhub_configure(session, name, requested, effective, timeout_ms)` | reconfigure `agent/iface` in one step (needs an `rwa` grant; [settings](#interface-settings)); `effective` may be NULL |
| `canhub_query(session, name, effective, timeout_ms)` | the settings the bus holds now |
| `canhub_txn_*` | run a [transaction](#transactions) at the agent |
| `canhub_last_error(session)` | human-readable detail for the last failure; pass `NULL` for why the last `canhub_connect` on this thread failed |
| `canhub_api_version()` | `CANHUB_API_VERSION` of the linked library |

Every `timeout_ms` follows `poll(2)`: `-1` blocks until the operation
finishes, `0` returns immediately, and a positive value is a deadline in
milliseconds. `CANHUB_ERR_TIMEOUT` when it expires.

Errors are negative `CANHUB_ERR_*` codes (`canhub.h`); ACL denials surface
as `CANHUB_ERR_READ_DENIED`/`CANHUB_ERR_WRITE_DENIED`. Without a write flag
or grant a session can still read — same baseline as every client.

Two drops happen inside the library and never reach the hub, so the hub-side
counters cannot show them. `canhub_send` returns `CANHUB_ERR_RATE_LIMITED`
when the pace relayed by the hub leaves no budget for the frame, and the
receive ring (512 frames) overwrites its oldest entry when the caller reads
slower than the bus produces. `canhub_stats` returns both as monotonic
counters, so a consumer can tell a quiet bus from one it is losing:

```c
CanHubStats stats = { 0 };
stats.struct_size = sizeof(stats);
canhub_stats(session, &stats);
printf("paced out %" PRIu64 ", ring lost %" PRIu64 "\n",
       stats.frames_rate_limited,
       stats.frames_ring_dropped);
```

## Interface settings

`CanHubInterfaceSettings` carries what to change: set `struct_size`, then the
`CANHUB_SETTING_*` bit in `fields` for each member you fill. Everything is
applied at once and `effective` comes back with what the device holds
afterwards, the fields it reports set in `fields`.

```c
CanHubInterfaceSettings requested = { 0 };
CanHubInterfaceSettings effective = { 0 };
requested.struct_size = sizeof(requested);
effective.struct_size = sizeof(effective);
requested.fields = CANHUB_SETTING_BITRATE | CANHUB_SETTING_CTRLMODE;
requested.bitrate = 500000;
requested.ctrlmode_mask = CANHUB_CTRLMODE_FD;   // change FD...
requested.ctrlmode_flags = CANHUB_CTRLMODE_FD;  // ...to on

if (canhub_configure(session, "truck42/can0", &requested, &effective, 5000) != CANHUB_OK) {
    fprintf(stderr, "%s\n", canhub_last_error(session));   // e.g. the kernel's reason
}
```

Sample points are in tenths of a percent (875 = 87.5%). Raw timing goes in
`bit_timing`/`data_bit_timing` (`CANHUB_SETTING_BIT_TIMING`, time quantum in
ns, segments in quanta) instead of a bitrate; `termination` is in ohms. It works with or
without an open interface. `CANHUB_ERR_CONFIGURE_DENIED` without an `rwa`
grant, `CANHUB_ERR_APPLY_FAILED` when the agent could not apply it,
`CANHUB_ERR_AGENT_UNREACHABLE` when the agent dropped mid-request.

A session is not thread-safe: `canhub_recv`, `canhub_send` and the rest all
drive the same connection and receive ring, so calls on one session must not
overlap. Serialise them yourself when several threads share a session, and
keep the lock out of a long blocking `canhub_recv` — receive in short
timeouts and release in between, so a sender is not starved. The python-can
backend does exactly that. Separate sessions are independent.

`canhub_connect` returns NULL for every failure, so there is no session to
ask. `canhub_last_error(NULL)` returns the reason for the last failed
connect on the calling thread — bad url, unreadable identity, a hub host
that does not resolve, unreachable hub, timeout. The hub host resolves on a
short-lived thread of its own, so `connect_timeout_ms` also bounds a slow
resolver.

## Transactions

A transaction runs a request/response sequence at the agent, at bus speed,
instead of paying a network round trip per exchange (UDS flashing, broadcast
upgrades; [protocol](protocol.md#transactions)). Open the interface with
`CANHUB_OPEN_FLAG_RELIABLE | CANHUB_OPEN_FLAG_WRITE`, then:

```c
CanHubTxnBegin begin = { 0 };
begin.struct_size = sizeof(begin);
begin.flags = CANHUB_TXN_FLAG_EXCLUSIVE;          // other clients' writes are dropped meanwhile
canhub_txn_begin(session, &begin, 5000);

CanHubTxnTemplate response = { 0 };               // wait for 0x7E8 xx 76, abort on 7F, extend on 7F xx 78
response.struct_size = sizeof(response);
response.template_id = 0;
response.advance_mode = CANHUB_TXN_ADVANCE_COUNT;
response.count_k = 1;
response.timeout_ms = 1000;
response.patterns_present = (1u << CANHUB_TXN_PATTERN_ADVANCE) | (1u << CANHUB_TXN_PATTERN_ABORT)
                          | (1u << CANHUB_TXN_PATTERN_EXTEND);
response.patterns[CANHUB_TXN_PATTERN_ADVANCE] = (CanHubTxnPattern){ 0x7E8, 0x7FF, 0xFF00, 0x7600 };
response.patterns[CANHUB_TXN_PATTERN_ABORT] = (CanHubTxnPattern){ 0x7E8, 0x7FF, 0xFF00, 0x7F00 };
response.patterns[CANHUB_TXN_PATTERN_EXTEND] = (CanHubTxnPattern){ 0x7E8, 0x7FF, 0xFF00FF00, 0x78007F00 };
canhub_txn_template(session, &response, 5000);

canhub_txn_send(session, steps, step_count, -1);  // blocks while waiting for credit
canhub_txn_end(session, -1);                      // marks the last step

CanHubTxnResult result = { 0 };
result.struct_size = sizeof(result);
if (canhub_txn_wait(session, &result, -1) == CANHUB_ERR_TXN_ABORTED) {
    fprintf(stderr, "step %" PRIu32 " failed, reason %u\n", result.failing_seq, result.reason);
}
```

| Function | Notes |
|---|---|
| `canhub_txn_begin(session, begin, timeout_ms)` | waits for the agent's grant; `begin` carries the exclusive flag and the roster with its origin extractor. `CANHUB_ERR_TXN_BUSY` while the agent runs another one, `CANHUB_ERR_TXN_UNSUPPORTED` for an agent that cannot, `CANHUB_ERR_WRITE_DENIED`, `CANHUB_ERR_STATE` on a lossy channel |
| `canhub_txn_template(session, template, timeout_ms)` | define template 0-15 (once per transaction), before the steps that use it |
| `canhub_txn_send(session, steps, count, timeout_ms)` | like a blocking socket write: hands the steps over as the agent's window allows and returns how many it took; hand over a whole image at once or in chunks. Frames received meanwhile stay queued for `canhub_recv` |
| `canhub_txn_end(session, timeout_ms)` | the step handed over last is held back until now, so it can be marked the last one |
| `canhub_txn_wait(session, result, timeout_ms)` | `CANHUB_OK` when done, `CANHUB_ERR_TXN_ABORTED` with the failing step, the reason, the roster bitmap of who answered, expected/got values and the frame that triggered it |
| `canhub_txn_progress(session, progress)` | steps sent and completed, record bytes in flight, window |
| `canhub_txn_abort(session)` | cancel; `canhub_txn_wait` then reports the reason cancelled |

## Example

[`examples/canhub_dump.c`](../examples/canhub_dump.c) is a complete
list/dump/send tool built as `canhub-dump` in every build tree:

```sh
./build/x86_64/release/canhub-dump unix:///run/can-hub/hub.sock list
```

## Windows

`make windows` (llvm-mingw or mingw-w64 in PATH, or `CAN_HUB_MINGW_ROOT`)
cross-builds `canhub.dll`, the static library and `canhub-dump.exe` with all
transports including QUIC. `scripts/build-python-wheel-windows.sh` wraps it
into the win_amd64 python wheel.
