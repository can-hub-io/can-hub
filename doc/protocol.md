# can-hub wire protocol

Status: draft, version 0. Everything here may change until version 1 is frozen.

Binary protocol, little-endian. Two planes:

- Control plane: reliable, ordered (QUIC bidirectional stream, or the TCP connection itself).
- Data plane: unreliable, latest-wins (QUIC datagrams; over TCP it shares the control stream). CAN frame messages.

Design rules: every message has a fixed layout with fields at known offsets, naturally aligned (u32 on multiples of 4, u64 on multiples of 8, counting from the start of the message, header included), with explicit reserved padding zeroed by the sender. A 32-bit microcontroller agent decodes with a single bounds check and a packed-struct overlay — no parsing loops, no cursors. Strings are fixed-size char arrays, NUL-terminated and NUL-padded. Control messages are rare; their wire size does not matter. The only trailing variable part in the whole protocol is the FRAME payload.

## Message header (both planes)

```
offset  size  field
0       1     type
1       1     flags
2       2     length   (payload bytes, little-endian)
4       ...   payload
```

## Identity and channels

- The agent never sends its identity per message: the connection is the identity (TLS certificate fingerprint).
- Global interface ids (u32, hub-assigned) exist only on the control plane (LIST, OPEN).
- The data plane uses a **channel** (u8, connection-scoped, like a file descriptor): assigned per interface by REGISTER_ACK (agents) or OPEN (clients). QUIC datagrams have no stream association, so the channel is the demultiplexer — one byte instead of a repeated global id.

## Control message types

```
0x01  HELLO        version negotiation, role (agent | client | admin)
0x02  REGISTER     agent: name + interface list
0x03  REGISTER_ACK status + assigned channels
0x04  LIST         query interfaces/agents (filter by name, fingerprint)
0x05  LIST_REPLY
0x06  OPEN         client opens an interface by global id, returns a channel
0x07  CLOSE
0x08  SUBSCRIBE    CAN id filters for an open channel (layout deferred)
0x09  ERROR        code + human-readable detail
0x0A  OPEN_ACK     status + assigned channel
0x7F  PING/PONG    liveness (flags bit 0: reply)
```

### Control payload layouts (version 0)

Offsets are from the start of the message (header included).

```
HELLO (total 12)
@4   version u8
@5   role u8 (1 agent, 2 client, 3 admin)
@6   reserved u16
@8   capabilities u32

ERROR (total 72)
@4   code u16
@6   reserved u16
@8   detail char[64]

REGISTER (total 392)
@4   agent_name char[128]
@132 interface_count u8
@133 reserved u8[3]
@136 interface_names char[16][16]

REGISTER_ACK (total 24)
@4   status u8 (0 ok)
@5   interface_count u8
@6   reserved u16
@8   channels u8[16]   (same order as the REGISTER names)

PING (total 4)
empty payload; header flags bit 0 set on the reply (pong)

LIST (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

LIST_REPLY (total 8 + count * 148)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 148 bytes:
     +0   interface_id u32
     +4   agent_name char[128]
     +132 interface_name char[16]

OPEN (total 8)
@4   interface_id u32

OPEN_ACK (total 12)
@4   status u8 (0 ok)
@5   channel u8
@6   reserved u16
@8   interface_id u32

CLOSE (total 8)
@4   channel u8
@5   reserved u8[3]
```

Limits: agent name <= 127 chars, interface name 1-15 chars (Linux IFNAMSIZ), <= 16 interfaces per agent, error detail <= 63 chars.

## Data plane: FRAME (0x40)

```
FRAME (total 20 + payload)
@4   can_id u32        (bits 0-28 id, 29 ERR, 30 RTR, 31 EFF)
@8   timestamp_us u64  (microseconds since epoch, capture time)
@16  channel u8
@17  payload_length u8 (0-8 classic, 0-64 FD)
@18  frame_flags u8    (bit 0: FD, bit 1: BRS)
@19  reserved u8
@20  payload 0-64 bytes
```

Multiple FRAME messages may be packed back-to-back in one datagram up to the path MTU.

## Open questions

- Version negotiation details and capability bits in HELLO.
- Admin message types for can-hub-cli (status, kick, ACL management).
- Flow control / backpressure signalling on the data plane.
- P2P phase 2: endpoint exchange messages (OFFER/ANSWER pattern).
- Reliable data plane: flows that need guaranteed in-order delivery (ISOTP transfers, UDS/firmware upgrades) mapped to dedicated QUIC streams instead of datagrams — reliable per flow without head-of-line blocking the cyclic traffic. Candidate signalling: a flag on OPEN/SUBSCRIBE selecting reliable transport for matching CAN ids.
- Timestamp cost: u64 is the largest FRAME field. A u32 microsecond timestamp relative to a negotiated session base (wraps every ~71 min) would save 4 bytes per frame.
- Interface configuration messaging (bitrate, bit timings, FD parameters, up/down): deferred. Needs its own message family and an authorization story.
- SUBSCRIBE filter semantics: deferred. In version 0, OPEN delivers every frame of the interface.
