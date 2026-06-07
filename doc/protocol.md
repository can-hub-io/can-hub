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
0x10  ADMIN_STATUS        admin: hub counters
0x11  ADMIN_STATUS_REPLY
0x12  ADMIN_PEERS         admin: live peer table (paginated)
0x13  ADMIN_PEERS_REPLY
0x14  ADMIN_KICK          admin: disconnect an agent by name
0x15  ADMIN_KICK_REPLY
0x16  ADMIN_PINS          admin: pinned identities (paginated)
0x17  ADMIN_PINS_REPLY
0x18  ADMIN_FORGET        admin: drop a pinned identity by agent name
0x19  ADMIN_FORGET_REPLY
0x1A  ADMIN_KICK_PEER     admin: disconnect any peer by peer id
0x1B  ADMIN_KICK_PEER_REPLY
0x1C  ADMIN_AGENTS        admin: live agents (paginated, name filter)
0x1D  ADMIN_AGENTS_REPLY
0x1E  ADMIN_CLIENTS       admin: open client channels (paginated, agent filter)
0x1F  ADMIN_CLIENTS_REPLY
0x20  ADMIN_INTERFACES    admin: interface catalogue with traffic counters (paginated)
0x21  ADMIN_INTERFACES_REPLY
0x7F  PING/PONG    liveness (flags bit 0: reply)
```

Admin messages are accepted only from peers whose HELLO declared the admin
role, and the hub accepts that role only on local transports (the unix
socket); an admin HELLO arriving over TCP or QUIC closes the connection.
ACL management joins this family when ACLs land.

### Control payload layouts (version 0)

Offsets are from the start of the message (header included).

```
HELLO (total 12)
@4   version u8
@5   role u8 (1 agent, 2 client, 3 admin)
@6   reserved u16
@8   capabilities u32

ERROR (total 72)
@4   code u16 (1 malformed message, 2 role rejected, 3 hub full,
              4 hello timeout, 5 kicked)
@6   reserved u16
@8   detail char[64]

The hub sends ERROR before every deliberate disconnect where the transport
still works (malformed HELLO/REGISTER, admin role on a non-local transport,
no free peer slot, missed HELLO deadline, admin kick). Evictions caused by a
failing control plane cannot carry one. REGISTER_ACK and OPEN_ACK rejections
keep their own status codes; no duplicate ERROR is sent for those.

REGISTER (total 392)
@4   agent_name char[128]
@132 interface_count u8
@133 reserved u8[3]
@136 interface_names char[16][16]

REGISTER_ACK (total 24)
@4   status u8 (0 ok, 1 rejected, 2 identity mismatch: name pinned to another fingerprint)
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

OPEN (total 12)
@4   interface_id u32
@8   flags u8 (bit 0: suppress own echo — frames this client injects are not
              echoed back to it when they return from the bus)
@9   reserved u8[3]

OPEN_ACK (total 12)
@4   status u8 (0 ok)
@5   channel u8
@6   reserved u16
@8   interface_id u32

CLOSE (total 8)
@4   channel u8
@5   reserved u8[3]

ADMIN_STATUS (total 4)
empty payload

ADMIN_STATUS_REPLY (total 48)
@4   peer_count u16
@6   agent_count u16
@8   client_count u16
@10  interface_count u16
@12  reserved u32
@16  frames_received u64    (valid data-plane frames accepted by the hub)
@24  frames_forwarded u64   (frame deliveries that reached a peer)
@32  frames_dropped u64     (deliveries dropped, destination TX budget full)
@40  frames_unroutable u64  (valid frames with no route: channel nobody opened)

ADMIN_PEERS (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_PEERS_REPLY (total 8 + count * 212)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 212 bytes:
     +0   peer_id u32
     +4   frames_forwarded u32      (frames the hub delivered to this peer)
     +8   frames_dropped u32        (frames dropped towards this peer, TX budget full)
     +12  role u8 (0 unknown, 1 agent, 2 client, 3 admin)
     +13  reserved u8[3]
     +16  agent_name char[128]      (empty unless a registered agent)
     +144 fingerprint_hex char[65]  (empty on plaintext transports)
     +209 reserved u8[3]

ADMIN_KICK (total 132)
@4   agent_name char[128]

ADMIN_KICK_REPLY (total 8)
@4   status u8 (0 ok, 1 unknown agent)
@5   reserved u8[3]

ADMIN_PINS (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_PINS_REPLY (total 8 + count * 196)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 196 bytes:
     +0   agent_name char[128]
     +128 fingerprint_hex char[65]
     +193 reserved u8[3]

ADMIN_FORGET (total 132)
@4   agent_name char[128]

ADMIN_FORGET_REPLY (total 8)
@4   status u8 (0 ok, 1 unknown agent)
@5   reserved u8[3]

ADMIN_KICK_PEER (total 8)
@4   peer_id u32

ADMIN_KICK_PEER_REPLY (total 8)
@4   status u8 (0 ok, 1 unknown peer)
@5   reserved u8[3]

ADMIN_AGENTS (total 136)
@4   offset u16        (pagination start index)
@6   reserved u16
@8   agent_name char[128]  (filter: only this agent; empty = all)

ADMIN_AGENTS_REPLY (total 8 + count * 204)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 204 bytes:
     +0   peer_id u32
     +4   interface_count u8
     +5   reserved u8[3]
     +8   agent_name char[128]
     +136 fingerprint_hex char[65]  (empty on plaintext transports)
     +201 reserved u8[3]

ADMIN_CLIENTS (total 136)
@4   offset u16        (pagination start index)
@6   reserved u16
@8   agent_name char[128]  (filter: only channels on this agent; empty = all)

ADMIN_INTERFACES (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_INTERFACES_REPLY (total 8 + count * 160)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 160 bytes:
     +0   interface_id u32
     +4   subscriber_count u8  (clients holding the interface open right now)
     +5   reserved u8[3]
     +8   frames_received u64  (frames seen on the interface, both directions)
     +16  agent_name char[128]
     +144 interface_name char[16]

ADMIN_CLIENTS_REPLY (total 8 + count * 156)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 156 bytes, one per open channel; a client with no open
     channels yields one entry with channel 0xFF and empty names (only when
     the filter is empty):
     +0   peer_id u32
     +4   interface_id u32
     +8   channel u8 (0xFF: none)
     +9   reserved u8[3]
     +12  agent_name char[128]
     +140 interface_name char[16]
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
@19  route_flags u8    (bit 0: bridged — set by the hub when forwarding a
                        frame across a bus-to-bus bridge rule; bridged frames
                        are never bridged again.
                        bit 1: echo — this frame is the bus echo of an
                        injected TX (set by the agent on kernel TX-confirm).
                        bits 2-7: origin token — hub-assigned opaque tag of
                        the injecting client (peer slot + 1, 0 = none). The
                        hub stamps it on injections towards the agent; the
                        agent carries it through its local TX/echo
                        correlation and returns it on the echo frame; the
                        hub strips it before fanning out to clients.)
@20  payload 0-64 bytes
```

Injected frames become visible to the other subscribers of the interface only
through their bus echo: the hub does not fan out client frames directly. The
echo returns in real bus order and only if the TX actually made it onto the
wire; the injecting client receives its own echo too unless it opened the
channel with the suppress-own-echo flag.

Multiple FRAME messages may be packed back-to-back in one datagram up to the path MTU.

## Open questions

- Version negotiation details and capability bits in HELLO.
- ACL management messages in the admin family (waiting on the ACL feature itself).
- Flow control / backpressure signalling on the data plane.
- P2P phase 2: endpoint exchange messages (OFFER/ANSWER pattern).
- Reliable data plane: flows that need guaranteed in-order delivery (ISOTP transfers, UDS/firmware upgrades) mapped to dedicated QUIC streams instead of datagrams — reliable per flow without head-of-line blocking the cyclic traffic. Candidate signalling: a flag on OPEN/SUBSCRIBE selecting reliable transport for matching CAN ids.
- Timestamp cost: u64 is the largest FRAME field. A u32 microsecond timestamp relative to a negotiated session base (wraps every ~71 min) would save 4 bytes per frame.
- Interface configuration messaging (bitrate, bit timings, FD parameters, up/down): deferred. Needs its own message family and an authorization story.
- SUBSCRIBE filter semantics: deferred. In version 0, OPEN delivers every frame of the interface.
