# Transaction scripts

A transaction script describes a request/response sequence that the agent runs
at its bus ([transactions](protocol.md#transactions)): what to send, and how to
recognise the answers. It is declarative, one statement per line, with blocks
by indentation like Python. Loops are unrolled by the client while it sends, so
the agent never runs code.

```sh
can-hub-client --connect quic://hub:7227 txn truck42/can0 flash.txn
```

The same script runs from C (`canhub_txn_run_script`) and Python
(`bus.run_script("flash.txn")`).

## Examples

### 1. One request, one answer

```
template response
    success  id == 0x7E8 and byte[1] == 0x50
    timeout  50ms

send 0x7E0 data 02 10 03 expect response
```

Sends a UDS session change and waits for its positive response (0x50). No
answer within 50 ms aborts the transaction with `timeout`.

### 2. Constants, and what one step expects on top of its template

```
REQUEST  = 0x7E0
RESPONSE = 0x7E8

template read-response
    success  id == RESPONSE and byte[1] == 0x62
    timeout  50ms

send REQUEST data 03 22 F1 90 expect read-response where byte[2] == 0xF1 and byte[3] == 0x90
send REQUEST data 03 22 F1 89 expect read-response where byte[2] == 0xF1 and byte[3] == 0x89
```

`where` adds what holds for that step only: here, that the answer is for the
identifier that was asked for.

### 3. Negative answers, "wait", unexpected answers

```
template uds-response
    success  id == 0x7E8 and byte[1] == 0x71
    failure  id == 0x7E8 and byte[1] == 0x7F
    pending  id == 0x7E8 and byte[1] == 0x7F and byte[3] == 0x78
    on mismatch abort
    timeout  2s

send 0x7E0 data 04 31 01 FF 00 expect uds-response
```

`pending` is checked before `failure`, so a `7F .. 78` restarts the timeout
instead of aborting. `on mismatch abort` aborts with `value_mismatch` when a
frame of the success identifier does not satisfy `success`; without it such a
frame is ignored.

### 4. Extended identifiers and masks

```
template any-ecu-response
    success  id & 0x1FFFFF00 == 0x18DAF100 and byte[1] == 0x7E
    timeout  100ms

send 0x18DA10F1 data 02 3E 00 expect any-ecu-response
```

The mask leaves the last byte, the answering ECU's address, free: the answer may come from any 0x18DAF1xx.

### 5. Frames without waiting, spacing, optional answers

```
template optional-wake-up
    success  id == 0x7E8
    timeout  200ms then continue

send 0x7DF data 02 3E 80 gap 10ms
send 0x7DF data 02 3E 80 gap 10ms
send 0x7DF data 02 3E 80 expect optional-wake-up
```

A `send` without `expect` does not wait; `gap` keeps at least that long before
the next frame. `then continue` moves on when nothing answers.

### 6. Who is alive

```
NODES = 0..15

template alive
    for node in NODES
        success  id & 0x7F0 == 0x700 and id & 0x0F == node
    timeout  500ms then continue

send 0x7FF data 01 expect alive
```

A `for` inside a template waits for an answer from each value of the list; the
result names who did not answer (`missing node=[3, 9]`).

### 7. Each node in turn

```
NODES = 0..15

template configured
    success  id & 0x7F0 == 0x580 and byte[0] == 0x60
    timeout  100ms

for node in NODES
    send 0x600 + node data 2B 00 10 00 node expect configured where id == 0x580 + node
```

A `for` outside a template repeats steps. The loop variable can be used in an
identifier and as a data byte.

### 8. Broadcast firmware, acknowledged by batches

```
FIRMWARE    = read("battery.bin")
FIRMWARE_ID = 0x123
BATTERIES   = 0..31

template batch-acknowledgement
    for battery in BATTERIES
        success  id & 0x7E0 == 0x700 and id & 0x1F == battery and byte[0] == 0x01
    on mismatch abort
    timeout  2s

for block in chunks(FIRMWARE, 128)
    for frame in chunks(block, 8)
        send FIRMWARE_ID data frame
    expect batch-acknowledgement where byte[1..2] == little_endian(sum16(block))
```

Each 128-byte block goes out in 16 frames, then every battery confirms it with
the block's sum.

### 9. Custom CRC, final verification, exclusive access

```
exclusive

FIRMWARE    = read("inverter.bin")
FIRMWARE_ID = 0x18FF5000
INVERTERS   = 0..7
VENDOR_CRC  = crc(width 16, polynomial 0x8005, initial 0xFFFF, reflect_input yes, reflect_output yes, final_xor 0x0000)

template block-received
    for inverter in INVERTERS
        success  id & 0x1FFFFF00 == 0x18FF5100 and id & 0xFF == inverter and byte[0] == 0x01
    on mismatch abort
    timeout  1s

template image-verified
    for inverter in INVERTERS
        success  id & 0x1FFFFF00 == 0x18FF5100 and id & 0xFF == inverter and byte[0] == 0x02
    on mismatch abort
    timeout  10s

for block in chunks(FIRMWARE, 256)
    for frame in chunks(block, 8, pad=0xFF)
        send FIRMWARE_ID data frame gap 1ms
    expect block-received where byte[1..2] == little_endian(VENDOR_CRC(block))

send FIRMWARE_ID data 00 FF FF FF FF FF FF FF expect image-verified where byte[1..4] == big_endian(crc32(FIRMWARE))
```

`exclusive` keeps other clients off the bus. `crc32(FIRMWARE)` is computed while
the file streams, without loading it.

### 10. Node in the data, quorum

```
CELLS = 0..31

template quorum
    for 24 of cell in CELLS
        success  id == 0x650 and byte[0] == 0xA5 and byte[2] == cell
    timeout  1s

send 0x640 data 01 expect quorum
```

The cell number is the third data byte; 24 answers out of 32 are enough.

## Reference

### Lines

| Statement | Meaning |
|---|---|
| `# ...` | comment, to the end of the line |
| `exclusive` | lease the interface for the transaction: other clients' frames are dropped |
| `NAME = value` | constant: a number, a list (`0..31`, `1, 4, 7..9`), `read("file")` or `crc(...)` |
| `template NAME` | starts a template block |
| `send ID data BYTES [gap TIME] [expect TEMPLATE [where CONDITION]]` | transmit a frame, then wait as the template says (or not at all) |
| `expect TEMPLATE [where CONDITION]` | wait without transmitting |
| `for VARIABLE in ITEMS` | repeat the indented statements for each item |

Blocks are indented with spaces (tabs are refused), every line of one block at
the same depth.

### Template block

| Line | Meaning |
|---|---|
| `success CONDITION` | the step completes on a frame meeting it |
| `failure CONDITION` | the transaction aborts (`abort_match`) |
| `pending CONDITION` | the step timeout restarts (checked before `failure`) |
| `for VARIABLE in LIST` + `success ...` | completes once every value of the list answered |
| `for N of VARIABLE in LIST` + `success ...` | completes once N different values answered |
| `on mismatch abort` | a frame of the success identifier that fails `success` aborts (`value_mismatch`) |
| `timeout TIME [then continue]` | how long the step waits; `then continue` moves on instead of aborting |

### Conditions

Terms joined with `and`, each one of:

| Term | Example |
|---|---|
| `id == VALUE` | `id == 0x7E8`, `id == 0x580 + node` |
| `id & MASK == VALUE` | `id & 0x7F0 == 0x700` |
| `byte[I] == VALUE`, `byte[I] & MASK == VALUE` | `byte[1] == 0x76`, `byte[0] & 0xF0 == 0x30` |
| `byte[I..J] == ORDER(NUMBER)` | `byte[1..2] == little_endian(sum16(block))` |

Only the first 8 data bytes can be compared. Inside a template `for`, one term
names the loop variable: `id & MASK == variable`, `(id >> SHIFT) & MASK ==
variable`, `byte[I] == variable` or `byte[I] & MASK == variable`, or the
shorthand `id == BASE + variable`; every value of the list must fit the mask.

An identifier written with 8 hexadecimal digits, or above 0x7FF, is extended
(29 bits); otherwise it is standard (11 bits). A standard identifier never
matches an extended one with the same value.

### Data

`data` takes bytes in hexadecimal without prefix (`02 10 03`), numbers
(`0xF1`), constants and loop variables (one byte each) and chunks (their
bytes). Up to 8 bytes is a classic frame; 12, 16, 20, 24, 32, 48 or 64 bytes is
a CAN FD frame.

### Files, chunks, checks

| Function | Meaning |
|---|---|
| `read("path")` | a file, relative to the script; read as it is sent |
| `chunks(DATA, N)` | DATA in pieces of N bytes; the last one may be shorter |
| `chunks(DATA, N, pad=0xFF)` | the last piece padded to N bytes |
| `sum8`, `sum16`, `sum32` | sum of the bytes, truncated to 8, 16 or 32 bits |
| `crc16` | CRC-16/CCITT-FALSE |
| `crc32` | CRC-32 (zip, Ethernet) |
| `crc(width W, polynomial P, initial I, reflect_input yes, reflect_output yes, final_xor X)` | any CRC of the standard (Rocksoft) model, used as `NAME(data)` |
| `little_endian(X)`, `big_endian(X)` | the byte order a multi-byte value is compared in |

### Times

`500us`, `50ms`, `2s`.

### Limits

| Limit | Why |
|---|---|
| 16 templates, counting the variants that `where` on bytes the template does not compare, `where id == ...` and `gap` create | the agent holds 16 templates per transaction |
| one template `for` (one list, one way of telling who answered) per script | one origin extractor per transaction |
| 64 values in a template `for` | the roster |
| chunks of up to 4096 bytes | the client's chunk buffer |
| no `if`, no user functions, no arithmetic beyond `BASE + variable` | a script describes what the agent can compare; anything else belongs in the C or Python API |

A script that breaks a rule is refused before anything is sent, with the line
and the reason.

Multi-frame ISO-TP (UDS TransferData) is not part of the language yet: it needs
the native ISO-TP step of [#95](https://github.com/can-hub-io/can-hub/issues/95).
