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
| `canhub_open(session, name, flags, timeout_ms)` | by namespaced name (`agent/iface`) or numeric id; flags `CANHUB_OPEN_FLAG_WRITE`, `CANHUB_OPEN_FLAG_NO_ECHO`, `CANHUB_OPEN_FLAG_RELIABLE` (lossless ordered QUIC stream, `quic://` only; `CANHUB_ERR_RELIABLE_UNSUPPORTED` if the hub lacks the capability) |
| `canhub_set_filters(session, filters, count)` | hub-side CAN id mask filters, replace semantics, max 16 |
| `canhub_recv(session, frame, timeout_ms)` | `CANHUB_RECEIVED`, `CANHUB_ERR_TIMEOUT`, or a failure |
| `canhub_send(session, frame)` | needs an open writable channel |
| `canhub_last_error(session)` | human-readable detail for the last failure; pass `NULL` for why the last `canhub_connect` on this thread failed |
| `canhub_api_version()` | `CANHUB_API_VERSION` of the linked library |

Every `timeout_ms` follows `poll(2)`: `-1` blocks until the operation
finishes, `0` returns immediately, and a positive value is a deadline in
milliseconds. `CANHUB_ERR_TIMEOUT` when it expires.

Errors are negative `CANHUB_ERR_*` codes (`canhub.h`); ACL denials surface
as `CANHUB_ERR_READ_DENIED`/`CANHUB_ERR_WRITE_DENIED`. Without a write flag
or grant a session can still read — same baseline as every client.

`canhub_connect` returns NULL for every failure, so there is no session to
ask. `canhub_last_error(NULL)` returns the reason for the last failed
connect on the calling thread — bad url, unreadable identity, unreachable
hub, handshake timeout.

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
