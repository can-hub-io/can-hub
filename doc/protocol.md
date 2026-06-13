# can-hub wire protocol

Status: draft, version 0. Everything here may change until version 1 is frozen.

License: this specification is licensed under CC-BY-4.0 — independent
implementations are welcome. The can-hub source code is AGPL-3.0 (see
LICENSE) with a commercial option (see LICENSE.commercial).

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
0x08  SUBSCRIBE    CAN id mask filters for an open channel
0x09  ERROR        code + human-readable detail
0x0A  OPEN_ACK     status + assigned channel
0x0B  IFCONFIG     hub -> agent: apply an interface change (bitrate, up, down)
0x0C  IFCONFIG_REPLY agent -> hub: apply status
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
0x22  ADMIN_PIN_ADD       admin: authorize an agent fingerprint
0x23  ADMIN_PIN_ADD_REPLY
0x24  ADMIN_ACL_SET       admin: grant a client read/write on an interface
0x25  ADMIN_ACL_SET_REPLY
0x26  ADMIN_ACL_REVOKE    admin: drop a client grant
0x27  ADMIN_ACL_REVOKE_REPLY
0x28  ADMIN_ACL_LIST      admin: list client grants (paginated)
0x29  ADMIN_ACL_LIST_REPLY
0x2A  ADMIN_IFCONFIG      admin: reconfigure an interface (bitrate, up, down)
0x2B  ADMIN_IFCONFIG_REPLY
0x7F  PING/PONG    liveness (flags bit 0: reply)
```

Admin ACL grants are keyed by the client TLS fingerprint (subject) and the
stable namespaced interface (agent_name + interface_name, the object). Any
of the three may be the literal `*` wildcard. Each grant carries a level:
`none` (no read, no write), `ro` (read), `rw` (read and write); on the wire
this is the `can_read`/`can_write` byte pair. The hub resolves a client's
permission most-specific-wins with the subject dominating: a rule naming the
fingerprint beats any `*`-subject rule, then narrower object scope wins
(exact > agent/* > */*). With no matching rule the baseline is read-open,
no-write.

Admin messages are accepted only from peers whose HELLO declared the admin
role, and the hub accepts that role only on local transports (the unix
socket); an admin HELLO arriving over TCP or QUIC closes the connection.

### Control payload layouts (version 0)

Offsets are from the start of the message (header included).

```
HELLO (total 76)
@4   version u8
@5   role u8 (1 agent, 2 client, 3 admin)
@6   reserved u16
@8   capabilities u32
@12  name char[64]   (optional client label; agents name themselves in REGISTER)

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
              echoed back to it when they return from the bus.
              bit 1: want write — the client intends to inject; the hub
              rejects the OPEN up front if the client is not authorized to
              write this interface, instead of silently dropping its frames)
@9   reserved u8[3]

OPEN_ACK (total 12)
@4   status u8 (0 ok, 1 rejected/unknown interface, 2 write denied, 3 read denied)
@5   channel u8
@6   reserved u16
@8   interface_id u32

CLOSE (total 8)
@4   channel u8
@5   reserved u8[3]

SUBSCRIBE (total 8 + filter_count * 8)
@4   channel u8
@5   filter_count u8   (0-16; 0 clears the channel back to pass-all)
@6   reserved u16
@8   filters, each 8 bytes:
     +0   can_id u32
     +4   can_mask u32

Per-channel CAN id filter, hub-side, replace semantics: each SUBSCRIBE sets
the channel's complete filter list (it does not accumulate). A frame is
delivered on the channel when filter_count is 0, or when any filter matches
SocketCAN-style: (frame.can_id & can_mask) == (can_id & can_mask) — the same
mask test as struct can_filter, flag bits 29-31 included. The filter applies
only to frames the hub fans out toward the client; it never affects injection
toward the agent. SUBSCRIBE on an unopened channel returns an ERROR and is
otherwise ignored (no SUBSCRIBE_ACK).

IFCONFIG (total 28, hub -> agent)
@4   interface_name char[16]
@20  op u8 (0 set bitrate, 1 link up, 2 link down)
@21  reserved u8[3]
@24  bitrate u32 (bits per second; only read for op 0)

IFCONFIG_REPLY (total 24, agent -> hub)
@4   interface_name char[16]   (echo, lets the hub correlate the request)
@20  status u8 (0 ok, 1 unknown interface, 2 apply failed)
@21  reserved u8[3]

ADMIN_IFCONFIG (total 156, admin -> hub)
@4   agent_name char[128]
@132 interface_name char[16]
@148 op u8 (0 set bitrate, 1 link up, 2 link down)
@149 reserved u8[3]
@152 bitrate u32

ADMIN_IFCONFIG_REPLY (total 8)
@4   status u8 (0 ok, 1 unknown interface, 2 agent unreachable, 3 apply failed)
@5   reserved u8[3]

Interface configuration is admin-only. The admin names the interface by its
namespaced (agent_name, interface_name) pair; the hub resolves it, forwards an
IFCONFIG to the owning agent, and relays the agent's IFCONFIG_REPLY back as an
ADMIN_IFCONFIG_REPLY — a synchronous round trip. The agent applies the change
through its CAN port (SocketCAN: rtnetlink, needs CAP_NET_ADMIN). A bitrate
change brings the link down, sets the bitrate, brings it up. If the agent
disconnects before replying the admin gets status agent unreachable.

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

ADMIN_PEERS_REPLY (total 8 + count * 272)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 272 bytes:
     +0   peer_id u32
     +4   frames_forwarded u32      (frames the hub delivered to this peer)
     +8   frames_dropped u32        (frames dropped towards this peer, TX budget full)
     +12  role u8 (0 unknown, 1 agent, 2 client, 3 admin)
     +13  transport_kind u8 (0 unknown, 1 unix, 2 tcp, 3 tls, 4 quic)
     +14  reserved u8[2]
     +16  agent_name char[128]      (agent name, or the client's HELLO label)
     +144 fingerprint_hex char[65]  (empty on plaintext transports)
     +209 uptime_seconds u32        (seconds since this peer connected)
     +213 origin char[56]           (remote ip:port; empty for local peers)
     +269 reserved u8[3]

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

ADMIN_PIN_ADD (total 200)
@4   agent_name char[128]
@132 fingerprint_hex char[65]  (sha256 hex, NUL-terminated)
@197 reserved u8[3]

ADMIN_PIN_ADD_REPLY (total 8)
@4   status u8 (0 ok, non-zero: rejected)
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

ADMIN_CLIENTS_REPLY (total 8 + count * 164)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 164 bytes, one per open channel; a client with no open
     channels yields one entry with channel 0xFF and empty names (only when
     the filter is empty):
     +0   peer_id u32
     +4   interface_id u32
     +8   channel u8 (0xFF: none)
     +9   reserved u8[3]
     +12  agent_name char[128]
     +140 interface_name char[16]
     +156 frames_forwarded u32  (this channel, hub -> client)
     +160 frames_dropped u32    (this channel, dropped at the hub egress)

ADMIN_ACL_SET (total 216)
@4   agent_name char[128]      (object agent; * for any)
@132 interface_name char[16]   (object interface; * for any)
@148 fingerprint_hex char[65]  (subject client fingerprint; * for any)
@213 can_read u8 (0/1)
@214 can_write u8 (0/1)
@215 reserved u8[1]

ADMIN_ACL_SET_REPLY (total 8)
@4   status u8 (0 ok, non-zero: rejected)
@5   reserved u8[3]

ADMIN_ACL_REVOKE (total 216)
@4   agent_name char[128]      (object agent; * for any)
@132 interface_name char[16]   (object interface; * for any)
@148 fingerprint_hex char[65]  (subject client fingerprint; * for any)
@213 reserved u8[3]            (can_read/can_write unused on revoke)

ADMIN_ACL_REVOKE_REPLY (total 8)
@4   status u8 (0 ok, 1 no matching grant)
@5   reserved u8[3]

ADMIN_ACL_LIST (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_ACL_LIST_REPLY (total 8 + count * 212)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 212 bytes:
     +0   agent_name char[128]
     +128 interface_name char[16]
     +144 fingerprint_hex char[65]
     +209 can_read u8 (0/1)
     +210 can_write u8 (0/1)
     +211 reserved u8[1]
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

Authorization: a client may read an interface unless an ACL denies it, and
may inject frames only if an ACL grants write. Clients on transports that
carry no fingerprint (unix socket, plain tcp) are network-trusted and may
always read and write; on the encrypted transports the hub checks the client
fingerprint against its ACLs. OPEN is rejected up front with status read
denied (3) or write denied (2) per the `want write` flag, and an injected
frame on a non-writable channel is **dropped** regardless (the security
boundary behind the honest-client OPEN check).

Injected frames become visible to the other subscribers of the interface only
through their bus echo: the hub does not fan out client frames directly. The
echo returns in real bus order and only if the TX actually made it onto the
wire; the injecting client receives its own echo too unless it opened the
channel with the suppress-own-echo flag.

Multiple FRAME messages may be packed back-to-back in one datagram up to the path MTU.

## External protocol bridging

This document specifies the can-hub wire protocol only. Foreign protocols are bridged outside it, not added to the message set. socketcand (its own ASCII protocol, separate spec) is bridged by `can-hub-client socketcand`, which terminates socketcand locally and speaks ordinary HELLO/LIST/OPEN/FRAME to the hub on the client's behalf — the hub sees a normal client. See doc/design.md "Compatibility adapters".

## Open questions

- Version negotiation details and capability bits in HELLO.
- Flow control / backpressure signalling on the data plane.
- P2P phase 2: endpoint exchange messages (OFFER/ANSWER pattern).
- Reliable data plane: flows that need guaranteed in-order delivery (ISOTP transfers, UDS/firmware upgrades) mapped to dedicated QUIC streams instead of datagrams — reliable per flow without head-of-line blocking the cyclic traffic. Candidate signalling: a flag on OPEN/SUBSCRIBE selecting reliable transport for matching CAN ids.
- Timestamp cost: u64 is the largest FRAME field. A u32 microsecond timestamp relative to a negotiated session base (wraps every ~71 min) would save 4 bytes per frame.
- Interface configuration: only bitrate and link up/down ship today (admin-only). Bit timings and FD data parameters are not yet exposed.
