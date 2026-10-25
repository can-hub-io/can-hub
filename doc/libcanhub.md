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
frame.id = CANHUB_STANDARD_ID(0x123);           // CANHUB_EXTENDED_ID(...) for 29 bits
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
| `canhub_open(session, name, flags, timeout_ms)` | by namespaced name (`agent/iface`) or numeric id; flags `CANHUB_OPEN_FLAG_WRITE`, `CANHUB_OPEN_FLAG_NO_ECHO`, `CANHUB_OPEN_FLAG_RELIABLE` (lossless ordered QUIC stream, `quic://` only; `CANHUB_ERR_RELIABLE_UNSUPPORTED` if the hub lacks the capability), `CANHUB_OPEN_FLAG_ERROR_FRAMES` (also receive the bus error frames, `CANHUB_FRAME_FLAG_ERROR` set) |
| `canhub_set_filters(session, filters, count)` | hub-side filters, replace semantics, max 16: a frame passes when its id equals `filter.id` except in `id_ignored_bits`; a standard and an extended id never match each other, so filter both kinds with two filters. No filter receives everything |
| `canhub_recv(session, frame, timeout_ms)` | `CANHUB_RECEIVED`, `CANHUB_ERR_TIMEOUT`, or a failure; `frame.id` is the value with its `extended` flag; `frame.flags` carries `CANHUB_FRAME_FLAG_FD`, `_BRS`, `_RTR`, `_ERROR` and `_BRIDGED` (the hub injected the frame along a [bridge](hub.md#bridging-buses); ignored on send) |
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
upgrades; [protocol](protocol.md#transactions)). It speaks the vocabulary of
[transaction scripts](transaction-scripts.md): a step sends a frame and waits
for a **response**. Open the interface with
`CANHUB_OPEN_FLAG_RELIABLE | CANHUB_OPEN_FLAG_WRITE`, then:

```c
static const CanHubTxnResponse SESSION_CHANGED = {
    .success = { .id = { 0x7E8 }, .bytes = { [1] = CANHUB_TXN_EQUALS(0x50) } },
    .timeout_ms = 50,
};

CanHubTxnReport report;
CanHubTxn *transaction = canhub_txn_create(session, NULL);

canhub_txn_send(transaction, CANHUB_STANDARD_ID(0x7E0), CANHUB_TXN_BYTES(0x02, 0x10, 0x03), &SESSION_CHANGED);
if (canhub_txn_finish(transaction, &report) != CANHUB_OK) {
    fprintf(stderr, "step %" PRIu32 ": %s\n", report.failing_step, canhub_txn_reason_name(report.reason));
}
```

Declarations are structs, written with designated initializers and fine as
`static const`; actions are functions. **Every zero means what it says**: id 0
is the id 0x000, `id_ignored_bits` 0 compares the whole id, a byte whose mask
is 0 is not compared. `CANHUB_TXN_ANY_ID_BITS` as `id_ignored_bits` compares
no id at all, standard or extended. What is optional is a pointer, `NULL` when
absent.
`CANHUB_TXN_BYTES(...)` expands to a byte array and its length,
`CANHUB_TXN_EQUALS(v)` and `CANHUB_TXN_MASKED(m, v)` to a byte to compare;
they use C99 compound literals, so C++ passes an array and its length instead.

### Responses

| `CanHubTxnResponse` | |
|---|---|
| `success` | the `CanHubTxnMatch` that completes the step: id except `id_ignored_bits`, the first 8 data bytes under their masks |
| `failure` | aborts the transaction (`CANHUB_TXN_REASON_FAILURE_MATCHED`) |
| `pending` | a "busy, wait" answer (UDS `7F xx 78`), checked before `failure`: the wait restarts with `timeout_after_pending_ms` |
| `timeout_ms` | how long the step waits, required (UDS P2) |
| `timeout_after_pending_ms` | required with `pending` (UDS P2*) |
| `continue_on_timeout` | when the time runs out the step completes and the transaction goes on, instead of aborting with `CANHUB_TXN_REASON_TIMEOUT` |
| `on_mismatch_abort` | a frame with the success id that fails `success` aborts (`CANHUB_TXN_REASON_VALUE_MISMATCH`, `expected`/`got` in the report) |
| `nodes`, `node_count` | wait for an answer from each of these nodes (up to 64) |
| `allowed_missing` | how many may stay silent; the step completes as soon as the rest answered |
| `node_from`, `node_byte` | how a node is told: `CANHUB_TXN_NODE_IN_ID`, the bits `success.id_ignored_bits` leaves free (shifted down: `0x1F00` gives nodes 0-31 from id bits 8-12), or `CANHUB_TXN_NODE_IN_BYTE`, data byte `node_byte` |

```c
static const CanHubTxnMatch NEGATIVE = { .id = { 0x7E8 }, .bytes = { [1] = CANHUB_TXN_EQUALS(0x7F) } };
static const CanHubTxnMatch BUSY = { .id = { 0x7E8 }, .bytes = { [1] = CANHUB_TXN_EQUALS(0x7F), [3] = CANHUB_TXN_EQUALS(0x78) } };

static const CanHubTxnResponse ROUTINE_RESPONSE = {
    .success = { .id = { 0x7E8 }, .bytes = { [1] = CANHUB_TXN_EQUALS(0x71) } },
    .failure = &NEGATIVE,
    .pending = &BUSY,
    .timeout_ms = 50,
    .timeout_after_pending_ms = 5000,
};

static const uint32_t NODES[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

static const CanHubTxnResponse ALIVE = {                  /* answers on 0x700-0x70F */
    .success = { .id = { 0x700 }, .id_ignored_bits = 0x00F },
    .nodes = NODES,
    .node_count = 16,
    .timeout_ms = 500,
    .continue_on_timeout = true,
};
```

A transaction waits for one set of nodes, told the same way, in every response
that names nodes.

### Steps

| Function | Notes |
|---|---|
| `canhub_txn_create(session, options)` | sends nothing: the first step starts the transaction at the agent. `NULL` with `canhub_last_error` when a transaction already runs on the session or the interface is not open |
| `canhub_txn_send(transaction, id, data, length, response)` | a frame (more than 8 bytes is CAN FD: 12, 16, 20, 24, 32, 48 or 64), then the wait `response` describes; `NULL` waits for nothing |
| `canhub_txn_send_message(transaction, link, data, length, response)` | an [ISO-TP](#iso-tp) message |
| `canhub_txn_expect(transaction, response)` | waits without sending |
| `canhub_txn_step(transaction, step)` | every option of a step: `where`, `gap_us`, a frame of length 0 (`data` not NULL), a wait on an ISO-TP link (`data` NULL) |
| `canhub_txn_try_step(transaction, step, timeout_ms)` | gives up with `CANHUB_ERR_TIMEOUT` when the agent has not granted the transaction yet or its window did not take the step in time, nothing of it sent: call it again with the same step |
| `canhub_txn_progress(transaction, progress)` | steps sent and completed, record bytes in flight, window |
| `canhub_txn_abort(transaction)` | cancels at the agent; may be called from another thread (a Ctrl-C handler thread, say) |
| `canhub_txn_wait(transaction, timeout_ms)` | ends the transaction (no step follows), then waits for the outcome: `CANHUB_ERR_TIMEOUT` while it is not known |
| `canhub_txn_finish(transaction, report)` | waits for the outcome, fills `report` and frees the transaction. Call it after every `canhub_txn_create`, also after an error, as `fclose` after `fopen`. After `canhub_close` it only frees the transaction and returns `CANHUB_ERR_DISCONNECTED` |
| `canhub_txn_reason_name(reason)` | `"timeout"`, `"failure_matched"`... as the CLI prints them |

A step waits for the agent's window like a blocking write and goes out as soon
as the call returns, so the agent runs it while the application builds the
next one; frames received meanwhile stay queued for `canhub_recv`. `where` adds to the response for that
step only: its id and `id_ignored_bits` replace the response's, its bytes add
to the response's bytes, so one response serves every block of a firmware
image, each with its own counter or checksum:

```c
for (offset = 0, counter = 1; offset < image_length; offset += 8, counter++) {
    CanHubTxnMatch where = { .id = { 0x7E8 }, .bytes = { [2] = CANHUB_TXN_EQUALS(counter) } };

    canhub_txn_step(transaction, &(CanHubTxnStep){
        .id = CANHUB_STANDARD_ID(0x7E0),
        .data = image + offset,
        .length = 8,
        .response = &ACCEPTED,
        .where = &where,
        .gap_us = 1000,
    });
}
```

The agent holds 16 ways of waiting per transaction: each distinct response,
`where` shape, gap and ISO-TP link is one, found again by content, so building
a response anew for every step costs nothing. A step the library refuses — a
17th way of waiting, a timeout of 0, a node outside the ignored bits — returns
`CANHUB_ERR_ARGUMENT` with `"step N: reason"` in `canhub_last_error` and
cancels the transaction; try a long sequence against a vcan first, or write it
as a [script](transaction-scripts.md), which is checked whole before anything
is sent.

### Options and reports

| `CanHubTxnOptions` (or `NULL` for the defaults) | |
|---|---|
| `exclusive` | other clients' frames to the interface are dropped while the transaction runs |
| `idle_timeout_ms` | required: how long the agent waits for steps once it ran out of them (`CANHUB_TXN_REASON_STARVED` after) |
| `keepalive` | a `CanHubTxnKeepalive` frame the agent sends every `every_ms` while it waits for steps, for instance to keep a device session open |
| `bitrate_switch` | sends the CAN FD frames of the steps with bit rate switch |
| `on_roster`, `context` | called with a `CanHubTxnRosterResult` (`step`, `missing`) for every step waiting for nodes that ended without all of them; it runs inside the transaction's calls, on their thread, and must not call them |

| `CanHubTxnReport` | |
|---|---|
| `done`, `reason` | `CANHUB_TXN_REASON_*` |
| `failing_step`, `completed_steps` | |
| `failing_roster` | who had not answered, when the failing step waited for nodes |
| `offending_node`, `expected`, `got` | the node and the 8 data bytes of a value mismatch |
| `has_frame`, `frame` | the frame that ended the transaction |

`canhub_txn_create` and the first step answer `CANHUB_ERR_TXN_BUSY` while the
agent runs another transaction, `CANHUB_ERR_TXN_UNSUPPORTED` for an agent that
cannot, `CANHUB_ERR_WRITE_DENIED`, `CANHUB_ERR_STATE` on a lossy channel, and
`CANHUB_ERR_AGENT_UNREACHABLE` when the agent did not grant the transaction
within 5 s (the library cancels it, even if the grant comes later). Once the
transaction ran, the calls return `CANHUB_ERR_TXN_ABORTED` and
`canhub_txn_finish` reports why. An agent without ISO-TP aborts the
transaction with `CANHUB_TXN_REASON_UNSUPPORTED` when the first ISO-TP step
arrives, after the steps before it ran; `failing_step` names that step.
`canhub_close` cancels a transaction still running at the agent.

### ISO-TP

A `CanHubTxnIsotpLink` makes a step one ISO 15765-2 message of up to 4095 bytes
that the agent segments on `tx_id`, honouring the ECU's flow control, and
whose answer on `rx_id` it reassembles before matching the response against
it ([protocol](protocol.md#transactions), "ISO-TP steps"):

```c
static const CanHubTxnIsotpLink ECU = {
    .tx_id = { 0x7E0 }, .rx_id = { 0x7E8 }, .padded = true, .padding = 0xCC,
};
static const CanHubTxnMatch TRANSFER_NEGATIVE = { .id = { 0x7E8 }, .bytes = { [0] = CANHUB_TXN_EQUALS(0x7F) } };
static const CanHubTxnMatch TRANSFER_BUSY = {
    .id = { 0x7E8 }, .bytes = { [0] = CANHUB_TXN_EQUALS(0x7F), [2] = CANHUB_TXN_EQUALS(0x78) },
};
static const CanHubTxnResponse TRANSFER_ACCEPTED = {        /* 76 <counter>, abort on 7F, 7F xx 78 waits up to 5 s */
    .success = { .id = { 0x7E8 }, .bytes = { [0] = CANHUB_TXN_EQUALS(0x76) } },
    .failure = &TRANSFER_NEGATIVE,
    .pending = &TRANSFER_BUSY,
    .timeout_ms = 50,
    .timeout_after_pending_ms = 5000,
};

CanHubTxnMatch where = { .id = { 0x7E8 }, .bytes = { [1] = CANHUB_TXN_EQUALS(counter) } };

canhub_txn_step(transaction, &(CanHubTxnStep){
    .id = ECU.tx_id, .data = request, .length = request_length,
    .isotp = &ECU, .response = &TRANSFER_ACCEPTED, .where = &where,
});
```

| `CanHubTxnIsotpLink` | |
|---|---|
| `tx_id`, `rx_id` | where the agent sends (and its flow control goes) and where the ECU answers; a response on the link matches `rx_id` exactly |
| `address_byte`, `tx_address`, `rx_address` | extended or mixed addressing: frames start with `tx_address`, answers with `rx_address` |
| `padded`, `padding` | fill short frames |
| `fd`, `fd_data_length`, `bitrate_switch` | CAN FD frames of `fd_data_length` bytes (8-64) |
| `block_size`, `st_min` | the flow control the agent answers a multi-frame answer with |

A step on a link without data waits for a message without sending one.
Transport failures abort with `CANHUB_TXN_REASON_FLOW_CONTROL_TIMEOUT`,
`_CONSECUTIVE_FRAME_TIMEOUT`, `_OVERFLOW`, `_SEQUENCE_ERROR` or `_WAIT_LIMIT`;
an agent without ISO-TP aborts the transaction with `_UNSUPPORTED` at the first
ISO-TP step.

These are transport steps: what a UDS answer means (positive, negative
response codes, sessions) stays with the application or its diagnostics
library; the example above is the whole of what a TransferData block needs.

### Scripts

`canhub_txn_run_script` runs a [transaction script](transaction-scripts.md)
given as text on the open interface, `canhub_txn_run_script_file` one read
from a file. Both check the whole script before anything is sent, then run it
over the calls above.

```c
static int32_t onProgress(void *context, const CanHubTxnProgress *progress, uint32_t total_steps)
{
    fprintf(stderr, "\r%" PRIu32 "/%" PRIu32, progress->completed_steps, total_steps);
    return 1;                                     /* 0 cancels the transaction */
}

CanHubTxnScriptOptions options = { .on_progress = onProgress };
CanHubTxnReport report;
int32_t status = canhub_txn_run_script_file(session, "flash.txn", &options, &report);
if (status == CANHUB_ERR_ARGUMENT) {
    fprintf(stderr, "%s\n", canhub_last_error(session));   /* "flash.txn:5: no response has this name" */
}
```

Files a script file reads resolve against its directory; for text, against
`base_directory` (`NULL`: the working directory), and errors say
`script:LINE: reason`. `options` may be `NULL`; `on_roster` works as for a
transaction, `on_progress` is called a few times a second. What the script
declares (`exclusive`, `idle timeout`, `keepalive`) is the script's own.

## Example

[`examples/canhub_dump.c`](../examples/canhub_dump.c) is a complete
list/dump/send tool built as `canhub-dump` in every build tree, and
[`examples/canhub_txn.c`](../examples/canhub_txn.c) a transaction one
(`canhub-txn`): a UDS-style request sequence, and batches acknowledged by
nodes that reports who stayed silent:

```sh
./build/x86_64/release/canhub-dump unix:///run/can-hub/hub.sock list
./build/x86_64/release/canhub-txn unix:///run/can-hub/hub.sock truck42/can0 batch 4 8
```

## Windows

`make windows` (llvm-mingw or mingw-w64 in PATH, or `CAN_HUB_MINGW_ROOT`)
cross-builds `canhub.dll`, the static library, `canhub-dump.exe` and `canhub-txn.exe` with all
transports including QUIC. `scripts/build-python-wheel-windows.sh` wraps it
into the win_amd64 python wheel.
