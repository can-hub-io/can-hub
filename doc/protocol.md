# can-hub wire protocol

Status: draft, version 0. Everything here may change until version 1 is frozen.

Binary protocol, little-endian. Two planes:

- Control plane: reliable, ordered (QUIC bidirectional stream, or the TCP connection itself). TLV messages.
- Data plane: unreliable, latest-wins (QUIC datagrams; over TCP it shares the control stream). CAN frame messages.

## Message header (both planes)

```
offset  size  field
0       1     type
1       1     flags
2       2     length   (payload bytes, little-endian)
4       ...   payload
```

## Control message types

```
0x01  HELLO        version negotiation, role (agent | client | admin)
0x02  REGISTER     agent: name + interface list
0x03  REGISTER_ACK assigned/confirmed names, or error
0x04  LIST         query interfaces/agents (filter by name, fingerprint)
0x05  LIST_REPLY
0x06  OPEN         client opens an interface by id
0x07  CLOSE
0x08  SUBSCRIBE    CAN id filters for an open interface
0x09  ERROR        code + human-readable detail
0x7F  PING/PONG    liveness (flags bit 0: reply)
```

Strings: length-prefixed (u8), UTF-8, no NUL. Interface ids: u32 assigned by the hub on registration, stable for the session.

## Data plane: FRAME (0x40)

```
offset  size  field
0       4     interface id (u32)
4       4     CAN id + flags (bits 0-28 id, 29 ERR, 30 RTR, 31 EFF)
8       1     payload length (0-8 classic, 0-64 FD)
9       1     frame flags (bit 0: FD, bit 1: BRS)
10      8     timestamp (u64, microseconds since epoch, capture time)
18      ...   payload
```

Multiple FRAME messages may be packed back-to-back in one datagram up to the path MTU.

## Open questions

- Version negotiation details and capability bits in HELLO.
- Admin message types for can-hub-cli (status, kick, ACL management).
- Flow control / backpressure signalling on the data plane.
- P2P phase 2: endpoint exchange messages (OFFER/ANSWER pattern).
- Reliable data plane: flows that need guaranteed in-order delivery (ISOTP transfers, UDS/firmware upgrades) mapped to dedicated QUIC streams instead of datagrams — reliable per flow without head-of-line blocking the cyclic traffic. Candidate signalling: a flag on OPEN/SUBSCRIBE selecting reliable transport for matching CAN ids.
