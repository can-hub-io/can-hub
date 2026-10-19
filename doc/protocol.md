# can-hub wire protocol

Status: draft, version 0. Everything here may change until version 1 is frozen.

License: this specification is licensed under CC-BY-4.0 — independent
implementations are welcome. The can-hub source code is AGPL-3.0 (see
LICENSE) with a commercial option (see LICENSE.commercial).

Binary protocol, little-endian. Two planes:

- Control plane: reliable, ordered (QUIC bidirectional stream, or the TCP connection itself).
- Data plane: unreliable, latest-wins by default (QUIC datagrams; over TCP it shares the control stream). CAN frame messages. A channel may opt in to **reliable** delivery at OPEN time (see [Reliable channels](#reliable-channels)): its frames move to a dedicated bidirectional QUIC stream — in-order and lossless — without head-of-line-blocking the lossy cyclic traffic.

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
0x04  LIST         client: the interface catalogue, paginated
0x05  LIST_REPLY
0x06  OPEN         client opens an interface by global id, returns a channel
0x07  CLOSE
0x08  SUBSCRIBE    CAN id mask filters for an open channel
0x09  ERROR        code + human-readable detail
0x0A  OPEN_ACK     status + assigned channel
0x0B  IFCONFIG     hub -> agent: apply or read back an interface's settings
0x0C  IFCONFIG_REPLY agent -> hub: status, effective settings, reason
0x0D  INTERFACE_STATUS agent->hub: per-interface health/credit; also hub->client: pace-rate relay
0x0E  CONFIGURE    client or admin -> hub: configure an interface by name
0x0F  CONFIGURE_REPLY
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
0x2C  ADMIN_REJECTED      admin: identities the hub turned away (paginated)
0x2D  ADMIN_REJECTED_REPLY
0x7F  PING/PONG    liveness (flags bit 0: reply)
```

Admin ACL grants are keyed by the client TLS fingerprint (subject) and the
stable namespaced interface (agent_name + interface_name, the object). Any
of the three may be the literal `*` wildcard. Each grant carries a level:
`none` (no read, no write), `r` (read), `rw` (read and write), `rwa` (read,
write and configure the interface); on the wire this is the
`can_read`/`can_write`/`can_admin` byte triple, and `can_admin` always comes
with the other two. The hub resolves a client's
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
@8   capabilities u32 (feature bitset; bit 0 reliable channels — see Reliable
              channels. Unknown bits are ignored, never fatal.)
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
@4   status u8 (0 ok, 1 rejected, 2 identity mismatch: name pinned to another
              fingerprint, 3 unknown agent: fingerprint not in the hub allowlist,
              only sent by a hub started with --require-known-peers)
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
              write this interface, instead of silently dropping its frames.
              bit 2: reliable — carry this channel on a dedicated reliable
              QUIC stream instead of the lossy datagram plane (see Reliable
              channels). Requires the reliable-channels capability advertised
              in HELLO, else the OPEN is rejected with status 4.
              bit 3: error frames — also deliver the bus error frames (FRAME
              with can_id bit 29 set) on this channel; without it the hub
              drops them for this channel, as a SocketCAN socket without
              CAN_RAW_ERR_FILTER never sees them.)
@9   reserved u8[3]

OPEN_ACK (total 12)
@4   status u8 (0 ok, 1 rejected/unknown interface, 2 write denied, 3 read denied,
              4 reliable unsupported: OPEN set the reliable flag but the peer
              did not advertise the reliable-channels capability in HELLO)
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
mask test as struct can_filter, flag bits 29-31 included. Error frames bypass
the filters: the OPEN error frames flag alone decides. The filter applies
only to frames the hub fans out toward the client; it never affects injection
toward the agent. SUBSCRIBE on an unopened channel returns an ERROR and is
otherwise ignored (no SUBSCRIBE_ACK).

IFCONFIG (total 116, hub -> agent)
@4   request_id u32            (hub-chosen, echoed by the reply)
@8   interface_name char[16]
@24  op u8 (0 apply, 1 query)
@25  reserved u8[3]
@28  settings (88 bytes, below)

IFCONFIG_REPLY (total 164, agent -> hub)
@4   request_id u32
@8   status u8 (0 ok, 1 unknown interface, 2 apply failed)
@9   reserved u8[3]
@12  settings (88 bytes): what the device holds after the request
@100 detail char[64]           (why it failed, e.g. the kernel's netlink message)

settings (88 bytes, shared by IFCONFIG, IFCONFIG_REPLY, CONFIGURE, CONFIGURE_REPLY)
+0   fields u32                (which members carry a value: bit 0 bitrate,
                                1 sample_point, 2 data_bitrate,
                                3 data_sample_point, 4 ctrlmode, 5 restart_ms,
                                6 txqueuelen, 7 link, 8 can_state,
                                9 bit_timing, 10 data_bit_timing,
                                11 termination)
+4   bitrate u32               (bits per second)
+8   data_bitrate u32          (CAN FD data phase, bits per second)
+12  sample_point u16          (tenths of a percent: 875 = 87.5%; 0-1000)
+14  data_sample_point u16
+16  ctrlmode_mask u32         (modes to change: bit 0 loopback, 1 listen-only,
                                2 triple-sampling, 3 one-shot, 4 berr-reporting,
                                5 fd; the Linux CAN_CTRLMODE_* values)
+20  ctrlmode_flags u32        (their new values)
+24  restart_ms u32            (automatic restart after bus-off; 0 = never)
+28  txqueuelen u32
+32  link_up u8 (0/1)
+33  can_state u8              (reported only: 0 error-active, 1 error-warning,
                                2 error-passive, 3 bus-off, 4 stopped, 5 sleeping)
+34  termination u16           (ohms, 0 = off)
+36  bit_timing (24 bytes)     (nominal: tq u32 in ns, prop_seg u32,
                                phase_seg1 u32, phase_seg2 u32, sjw u32,
                                brp u32; segments in quanta)
+60  data_bit_timing (24 bytes) (the same for the CAN FD data phase)
+84  reserved u8[4]

In a request, fields names what to change and nothing else is touched. A
phase is timed either by its rate (bitrate and sample point) or raw, by tq
or brp and the segments; a request giving both for one phase is refused
(status apply failed) before anything is applied, but sjw alone may go with
a rate. A reply carries the raw timing the kernel settled on, brp included,
however it was set. In a
reply it names what the device reports: a vcan has no bit timing, so a reply
for one carries only link and txqueuelen. The agent applies everything in one
step: bit timing, data bit timing, control modes and restart-ms need the link
down, so it takes the link down, sets them and the TX queue in one netlink
request, and brings the link back to the requested state (or to the one it
had). If the request fails it restores the link and reports why in detail.
Query applies nothing and only reads back.

INTERFACE_STATUS (total 328; agent -> hub, also hub -> client)
@4   interface_count u8
@5   reserved u8[3]
@8   entries[16], 20 bytes each, addressed by channel:
       +0  channel u8
       +1  flags u8 (bit 0 reliable; reserved, sent 0)
       +2  reserved u8[2]
       +4  advertised_rate u32 (nominal bus bits/s; 0 = unpaced)
       +8  credit u32 (agent's AIMD-measured sustainable rate)
       +12 tx_dropped u64 (monotonic CAN-tx drops, ENOBUFS at the bus sink)

The agent emits this periodically while registered (and immediately on entering
RUNNING). tx_dropped exposes the silent rate-impedance loss at the CAN-tx queue
(the bus is a fixed-rate sink); the hub stores the latest value per interface and
surfaces it in ADMIN_INTERFACES. advertised_rate is the interface's nominal
bitrate (read from the kernel, or forced with the agent's --pace-rate where there
is no bit timing, e.g. vcan); credit is the agent's AIMD-measured sustainable rate
(it backs off when new tx drops appear, ramps back when none do). The hub shapes
data-plane egress to that interface to min(advertised, credit) (token bucket),
parking the excess, and relays that rate to clients holding the interface open via
the same message (entries addressed by the client's channel, advertised_rate = the
rate the client should pace its writes to) so a client sheds excess at the source
instead of overrunning the hub. flags is reserved for the reliability toggle.

CONFIGURE (total 240, client or admin -> hub)
@4   agent_name char[128]
@132 interface_name char[16]
@148 op u8 (0 apply, 1 query)
@149 reserved u8[3]
@152 settings (88 bytes)

CONFIGURE_REPLY (total 160, hub -> client or admin)
@4   status u8 (0 ok, 1 unknown interface, 2 agent unreachable, 3 apply failed,
                4 not allowed)
@5   reserved u8[3]
@8   settings (88 bytes): what the device holds afterwards
@96  detail char[64]

The requester names the interface by its namespaced (agent_name,
interface_name) pair, with no channel open. The hub resolves it, checks the
requester may configure it, forwards an IFCONFIG to the owning agent with a
fresh request_id and relays the agent's IFCONFIG_REPLY back as a
CONFIGURE_REPLY. An admin may always configure. A client needs an `rwa` grant
on the interface; clients on transports with no fingerprint (unix socket,
plain tcp) are trusted, as for writes. The agent applies the change through
its CAN port (SocketCAN: rtnetlink, needs CAP_NET_ADMIN). If the agent
disconnects before replying the requester gets status agent unreachable.

ADMIN_STATUS (total 4)
empty payload

ADMIN_STATUS_REPLY (total 48)
@4   peer_count u16
@6   agent_count u16
@8   client_count u16
@10  interface_count u16
@12  receive_overflows u32  (packets the kernel dropped before the hub read them: receive buffer full)
@16  frames_received u64    (valid data-plane frames accepted by the hub)
@24  frames_forwarded u64   (frame deliveries that reached a peer)
@32  frames_dropped u64     (deliveries dropped, destination TX budget full)
@40  frames_unroutable u64  (valid frames with no route: channel nobody opened)

ADMIN_PEERS (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_PEERS_REPLY (total 8 + count * 272)
@4   count u8          (0-15 entries in this reply)
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

ADMIN_INTERFACES_REPLY (total 8 + count * 168)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 168 bytes:
     +0   interface_id u32
     +4   subscriber_count u8  (clients holding the interface open right now)
     +5   reserved u8[3]
     +8   frames_received u64  (frames seen on the interface, both directions)
     +16  agent_name char[128]
     +144 interface_name char[16]
     +160 tx_dropped u64       (latest INTERFACE_STATUS value for the interface)

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
@215 can_admin u8 (0/1; implies the other two)

ADMIN_ACL_SET_REPLY (total 8)
@4   status u8 (0 ok, non-zero: rejected)
@5   reserved u8[3]

ADMIN_ACL_REVOKE (total 216)
@4   agent_name char[128]      (object agent; * for any)
@132 interface_name char[16]   (object interface; * for any)
@148 fingerprint_hex char[65]  (subject client fingerprint; * for any)
@213 reserved u8[3]            (the level bytes are unused on revoke)

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
     +211 can_admin u8 (0/1)

ADMIN_REJECTED (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_REJECTED_REPLY (total 8 + count * 264)
@4   count u8          (0-15 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 264 bytes:
     +0   role u8 (1 agent, 2 client)
     +1   reason u8 (0 not allowed: the hub runs --require-known-peers and
                    does not know this fingerprint; 1 fingerprint mismatch:
                    the agent name is pinned to another fingerprint)
     +2   reserved u8[2]
     +4   attempts u32              (rejections of this identity so far)
     +8   last_seen_seconds u32     (seconds since the latest one)
     +12  name char[128]            (agent name claimed at REGISTER, or the
                                     client's HELLO label)
     +140 fingerprint_hex char[65]
     +205 origin char[56]           (remote ip:port of the latest attempt)
     +261 reserved u8[3]
```

The hub remembers the last 16 identities it turned away, in memory, one entry
per fingerprint and role. A retry bumps attempts instead of taking another
entry; a new identity evicts the entry with the fewest attempts, the least
recently seen among equals, so a device retrying on its backoff outlives a
flood of one-shot certificates. An identity that is then admitted leaves the
list. Peers without a fingerprint (plaintext, unix) are never listed.

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
                        the injecting client, scoped to the agent it injects
                        through (1-63, 0 = none). The
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

Error frames: the agent subscribes to every error class its controller reports
(CAN_ERR_MASK) and uplinks them as ordinary FRAMEs, can_id bit 29 set and the
error class in the remaining id bits, payload the 8-byte SocketCAN error
detail (linux/can/error.h). A client cannot inject one: the hub drops a client
FRAME with bit 29 set.

Injected frames become visible to the other subscribers of the interface only
through their bus echo: the hub does not fan out client frames directly. The
echo returns in real bus order and only if the TX actually made it onto the
wire; the injecting client receives its own echo too unless it opened the
channel with the suppress-own-echo flag.

Multiple FRAME messages may be packed back-to-back in one datagram up to the
path MTU. A sender may pack or not; **a receiver must decode every FRAME in the
buffer**, not only the first. Decoding stops at the end of the buffer and at the
first entry that is not a well-formed FRAME, so a truncated tail costs the
frames after it and nothing before it.

## Reliable channels

The default data plane is lossy and latest-wins: correct for cyclic telemetry,
wrong for ISOTP transfers, UDS sequences and firmware upgrades, which need every
frame, in order. A channel may opt in to **reliable** delivery — in-order,
lossless, both directions — without changing the default for any other channel.

**Capability.** A peer advertises support with bit 0 of the HELLO `capabilities`
bitset (reliable channels). The bit is per connection: the client declares it on
its HELLO to the hub, the agent declares it on its REGISTER-time HELLO. Unknown
capability bits are ignored, never fatal.

**Activation is on-demand and per channel.** The client sets bit 2 (reliable) on
the OPEN for the flow that needs it; every other channel on the same connection
stays on the lossy datagram plane. Reliability is an attribute of the channel,
fixed at OPEN, and applies to **both directions** of the channel — TX injections
and the RX frames matching its SUBSCRIBE filters all ride the reliable path.
SUBSCRIBE is unchanged and carries no reliability flag of its own.

**Gating.** If an OPEN sets the reliable flag but the relevant peer did not
advertise the capability, the hub rejects it with OPEN_ACK status 4 (reliable
unsupported) rather than silently degrading to the lossy plane.

**Transport.** A reliable channel maps to one dedicated **bidirectional QUIC
stream**, distinct from the control stream and from the datagram plane:

- client ↔ hub: the client opens a client-initiated bidirectional stream;
- hub ↔ agent: the hub opens a server-initiated bidirectional stream (the hub is
  the QUIC server on both connections).

One stream per reliable channel isolates head-of-line blocking to that flow: a
lost segment on the firmware stream stalls only that stream, never the cyclic
datagrams nor another reliable channel. FRAME messages on a reliable stream are
length-framed by the message header (a stream is a byte stream, so frames are not
free-packed the way they are inside a single datagram).

**Relay and backpressure.** The hub copies frames between the client-side and
agent-side streams of the channel. Stream flow control then propagates
backpressure end to end: if the agent's CAN-tx sink cannot keep up, its receive
window closes, the hub stops reading the client stream, and the client's send
window closes in turn — the flow paces itself to the bus with no datagram drops.
For a reliable channel this flow control *is* the rate-matching the lossy plane
gets from explicit pacing credit (INTERFACE_STATUS).

A frame is relayed to every route of the interface or to none: while any
reliable route has no room, the hub leaves the frame on the source's stream and
retries it later, so no route ever receives it twice. The cost is that the
slowest reliable subscriber paces every subscriber of that interface, lossy ones
included.

**Lifecycle.** The hub drives teardown on both legs. CLOSE on a reliable channel
finishes the client's stream; once no client holds the interface reliable (CLOSE
or disconnect of the last one), the hub also finishes the agent's stream and the
agent's uplink for that channel returns to the lossy datagram plane. Finishing
is graceful: queued frames are sent, then a FIN; the peer answers with its own
FIN and stops using the stream, and both sides free it once it is closed. The
channel id is freed as usual.

**TCP / TLS fallback.** The fallback transports have no useful multistream: the
whole connection is already one reliable, ordered byte stream shared by both
planes. There the reliable flag is a no-op — the channel is already reliable, but
with connection-wide head-of-line blocking. The HOL-isolation benefit is
QUIC-only; this is the same documented fallback trade-off as the lossy plane
losing latest-wins semantics over TCP.

## External protocol bridging

This document specifies the can-hub wire protocol only. Foreign protocols are bridged outside it, not added to the message set. socketcand (its own ASCII protocol, separate spec) is bridged by `can-hub-client socketcand`, which terminates socketcand locally and speaks ordinary HELLO/LIST/OPEN/FRAME to the hub on the client's behalf — the hub sees a normal client. See doc/design.md "Compatibility adapters".

## Open questions

- Version negotiation details and capability bits in HELLO.
- Flow control / backpressure signalling on the data plane.
- P2P phase 2: endpoint exchange messages (OFFER/ANSWER pattern).
- Timestamp cost: u64 is the largest FRAME field. A u32 microsecond timestamp relative to a negotiated session base (wraps every ~71 min) would save 4 bytes per frame.
