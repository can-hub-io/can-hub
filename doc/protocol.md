# can-hub wire protocol

Status: draft, version 0. Everything here may change until version 1 is frozen.

License: this specification is licensed under CC-BY-4.0 — independent
implementations are welcome. The can-hub source code is AGPL-3.0 (see
LICENSE) with a commercial option (see LICENSE.commercial).

Binary protocol, little-endian. Two planes:

- Control plane: reliable, ordered (QUIC bidirectional stream, or the TCP connection itself).
- Data plane: unreliable, latest-wins by default (QUIC datagrams; over TCP it shares the control stream). CAN frame messages. A channel may opt in to **reliable** delivery at OPEN time (see [Reliable channels](#reliable-channels)): its frames move to a dedicated bidirectional QUIC stream — in-order and lossless — without head-of-line-blocking the lossy cyclic traffic.

Design rules: every message has a fixed layout with fields at known offsets, naturally aligned (u32 on multiples of 4, u64 on multiples of 8, counting from the start of the message, header included), with explicit reserved padding zeroed by the sender. A 32-bit microcontroller agent decodes with a single bounds check and a packed-struct overlay — no parsing loops, no cursors. Strings are fixed-size char arrays, NUL-terminated and NUL-padded. Control messages are rare; their wire size does not matter. The only trailing variable parts in the whole protocol are the FRAME payload and the step records of TXN_STEPS, which are 8-byte aligned so each record's fields stay naturally aligned.

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
0x2A  ADMIN_BRIDGE_SET    admin: bridge one bus into another (set or replace)
0x2B  ADMIN_BRIDGE_SET_REPLY
0x2C  ADMIN_REJECTED      admin: identities the hub turned away (paginated)
0x2D  ADMIN_REJECTED_REPLY
0x2E  ADMIN_BRIDGE_DELETE admin: drop a bridge route
0x2F  ADMIN_BRIDGE_DELETE_REPLY
0x30  ADMIN_BRIDGE_LIST   admin: bridge routes with counters (paginated)
0x31  ADMIN_BRIDGE_LIST_REPLY
0x32  ADMIN_GROUP_SET     admin: create a bridge group or replace its filters
0x33  ADMIN_GROUP_SET_REPLY
0x34  ADMIN_GROUP_DELETE  admin: drop a bridge group
0x35  ADMIN_GROUP_DELETE_REPLY
0x36  ADMIN_GROUP_JOIN    admin: add a bus to a bridge group
0x37  ADMIN_GROUP_JOIN_REPLY
0x38  ADMIN_GROUP_LEAVE   admin: remove a bus from a bridge group
0x39  ADMIN_GROUP_LEAVE_REPLY
0x3A  ADMIN_GROUP_LIST    admin: bridge groups with members and counters (one per page)
0x3B  ADMIN_GROUP_LIST_REPLY
0x3C  ADMIN_TXNS          admin: transactions in flight (paginated)
0x3D  ADMIN_TXNS_REPLY
0x41  TXN_BEGIN    client -> hub -> agent: start a transaction on an open channel
0x42  TXN_BEGIN_ACK agent (or hub) -> client: status, transaction id, window
0x43  TXN_TEMPLATE client -> hub -> agent: define a step template (on the channel's reliable stream)
0x44  TXN_STEPS    client -> hub -> agent: a batch of steps (on the channel's reliable stream)
0x45  TXN_PROGRESS agent -> hub -> client: completed steps, consumed credit
0x46  TXN_STATUS   agent (or hub) -> client: terminal outcome
0x47  TXN_ABORT    client (or hub) -> agent: cancel the transaction
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
              channels; bit 1 transactions, bit 2 ISO-TP steps — see
              Transactions. Unknown bits are ignored, never fatal.)
@12  name char[64]   (optional client label; agents name themselves in REGISTER)

ERROR (total 72)
@4   code u16 (1 malformed message, 2 role rejected, 3 hub full,
              4 hello timeout, 5 kicked, 6 slow consumer)
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
@133 flags u8          (bit 0: fd_interfaces is reported)
@134 fd_interfaces u16 (bit i: interface i carries CAN FD, its MTU is 72;
                        only meaningful with flag bit 0. An agent that does
                        not report it gets FD frames forwarded as before.)
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
       +1  flags u8 (bit 0 reliable, reserved, sent 0; bit 1 leased, hub ->
                     client only: another client's exclusive transaction holds
                     the interface, see Transactions)
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

ADMIN_STATUS_REPLY (total 96)
@4   peer_count u16
@6   agent_count u16
@8   client_count u16
@10  interface_count u16
@12  receive_overflows u32  (packets the kernel dropped before the hub read them: receive buffer full)
@16  frames_received u64    (valid data-plane frames accepted by the hub)
@24  frames_forwarded u64   (frame deliveries that reached a peer)
@32  frames_dropped u64     (deliveries dropped, destination TX budget full)
@40  frames_unroutable u64  (valid frames with no route: channel nobody opened)
@48  txns_started u64       (transactions an agent granted)
@56  txns_completed u64     (transactions that ended done)
@64  txns_aborted u64       (granted transactions that ended aborted, cancelled or orphaned)
@72  txns_rejected u64      (TXN_BEGINs refused, by the hub or the agent)
@80  txn_steps_completed u64 (steps of finished transactions)
@88  txn_lease_frame_drops u64 (client frames dropped because another client held the lease)

ADMIN_TXNS (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_TXNS_REPLY (total 8 + count * 176)
@4   count u8          (0-16 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 176 bytes, one per transaction the hub relays:
     +0   interface_id u32
     +4   client_peer_id u32 (0: its client left, cancelled once the agent grants it)
     +8   agent_peer_id u32
     +12  txn_id u16      (0 while pending)
     +14  state u8        (1 pending: waiting for the agent's grant, 2 active)
     +15  flags u8        (bit 0: exclusive lease)
     +16  completed_steps u32 (as of the last TXN_PROGRESS)
     +20  bytes_in_flight u32 (step record bytes relayed and not yet consumed)
     +24  age_ms u32      (since TXN_BEGIN)
     +28  progress_age_ms u32 (since the last TXN_PROGRESS; 0xFFFFFFFF: none yet)
     +32  agent_name char[128]
     +160 interface_name char[16]

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
     +5   mode u8              (0 not reported, 1 classic CAN, 2 CAN FD; a
                                classic interface gets no FD frames from the
                                hub, they are dropped and counted)
     +6   reserved u8[2]
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

ADMIN_BRIDGE_SET (total 424)
@4   source agent_name char[128]
@132 source interface_name char[16]
@148 destination agent_name char[128]
@276 destination interface_name char[16]
@292 filter_count u8   (0-16; 0 bridges every frame)
@293 reserved u8[3]
@296 filters, each 8 bytes:
     +0 can_id u32
     +4 can_mask u32   (a frame passes when id & mask == can_id & mask)

ADMIN_BRIDGE_SET_REPLY / ADMIN_BRIDGE_DELETE_REPLY (total 8)
@4   status u8 (0 ok, 1 invalid: source and destination are the same bus,
                2 the hub holds no more routes, 3 could not be stored,
                4 no such route)
@5   reserved u8[3]

ADMIN_BRIDGE_DELETE (total 292)
@4   source and destination, laid out as in ADMIN_BRIDGE_SET

ADMIN_BRIDGE_LIST (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_BRIDGE_LIST_REPLY (total 8 + count * 440)
@4   count u8          (0-8 entries in this reply)
@5   flags u8          (bit 0: more entries beyond offset + count)
@6   reserved u16
@8   entries, each 440 bytes:
     +0   source agent_name char[128], interface_name char[16]
     +144 destination agent_name char[128], interface_name char[16]
     +288 filter_count u8
     +289 state u8      (bit 0: source online, bit 1: destination online)
     +290 reserved u16
     +292 filters, 16 slots of 8 bytes as in ADMIN_BRIDGE_SET
     +420 reserved u32
     +424 forwarded u64 (frames injected into the destination)
     +432 dropped u64   (frames for a destination that was offline)

A route is keyed by its two endpoints; setting it again replaces its filters.
Routes name interfaces, not ids, so they outlive the agents that serve them.
The hub holds up to 64 and persists them in hub.db.

ADMIN_GROUP_SET (total 168)
@4   name char[32]     (1-15 of A-Z a-z 0-9 . _ -; the group is also
                       the interface group/<name>)
@36  filter_count u8   (0-16; 0 bridges every frame)
@37  reserved u8[3]
@40  filters, 16 slots of 8 bytes as in ADMIN_BRIDGE_SET

ADMIN_GROUP_DELETE (total 36)
@4   name char[32]

ADMIN_GROUP_JOIN / ADMIN_GROUP_LEAVE (total 248)
@4   name char[32]
@36  member agent_name char[128]
@164 member interface_name char[16]
@180 direction u8      (0 both, 1 sends: feeds the group and gets nothing,
                        2 receives: gets the group and feeds nothing;
                        ignored by LEAVE)
@181 filter_count u8   (0-8: what the member sends; 0 = all; ignored by LEAVE)
@182 reserved u16
@184 filters, 8 slots of 8 bytes as in ADMIN_BRIDGE_SET

ADMIN_GROUP_*_REPLY (total 8)
@4   status u8 (codes as in ADMIN_BRIDGE_SET_REPLY: 1 bad name, 2 no more
                groups or the group already has 16 members, 3 could not be
                stored, 4 no such group or member; joining a member again replaces its
                direction and filters)
@5   reserved u8[3]

ADMIN_GROUP_LIST (total 8)
@4   offset u16        (pagination start index)
@6   reserved u16

ADMIN_GROUP_LIST_REPLY (total 8 + count * 3640)
@4   count u8          (0-1 groups in this reply)
@5   flags u8          (bit 0: more groups beyond offset + count)
@6   reserved u16
@8   entries, each 2616 bytes:
     +0   name char[32]
     +32  filter_count u8
     +33  member_count u8 (0-16)
     +34  reserved u16
     +36  filters, 16 slots of 8 bytes
     +164 reserved u32
     +168 forwarded u64
     +176 dropped u64  (frames for a member that was offline)
     +184 members, 16 slots of 216 bytes:
          +0   agent_name char[128], interface_name char[16]
          +144 state u8 (bit 0: online)
          +145 direction u8 (as in JOIN)
          +146 filter_count u8
          +147 reserved u8
          +148 filters, 8 slots of 8 bytes
          +212 reserved u32

A frame captured on a member that passes the group filters is injected into
every other member of the group. A bus reached by several routes and groups
is injected once, charged to the first one that reaches it; the source bus
is never a target. The hub holds up to 16 groups of up to 16 members, in
hub.db.

Each group is also listed (LIST, ADMIN_INTERFACES) as the interface
`group/<name>`, an entry the hub serves itself: OPEN, FRAME and CLOSE work
as for any interface. Frames captured on members that pass the group
filters are delivered to its channels (hub injections excluded); a FRAME a
client writes on it is injected into every member and delivered to the
group's other channels. A reliable OPEN is refused, CONFIGURE answers
unknown interface, and REGISTER under the agent name `group` is refused.

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
@19  route_flags u8    (bit 0: bridged — set by the hub on the bus echo of
                        a frame it injected along a bridge route, so clients
                        can tell it from local traffic; the hub never bridges
                        such a frame again. The hub recognises the echo by a
                        per-agent origin token of its own, so agents need not
                        set or keep this bit.
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

Backpressure only paces writers. A client's frame has one route, to the agent
that owns the interface: while that reliable leg has no room the hub leaves the
frame on the client's stream and retries it later, pacing that writer to the bus.
Frames from the bus are never held back: the bus keeps producing whatever the
hub does, so pausing the agent would only make every subscriber wait for the
slowest. The hub relays each one to every subscriber that has room, and a
reliable subscriber that cannot take a frame (its stream buffer, 64 KiB, is full)
is disconnected with ERROR 6 (slow consumer): skipping the frame would break its
in-order, lossless contract. No other subscriber waits for it, lossy or
reliable.

**Lifecycle.** The hub drives teardown on both legs. CLOSE on a reliable channel
finishes the client's stream; once no client holds the interface reliable (CLOSE
or disconnect of the last one), the hub also finishes the agent's stream and the
agent's uplink for that channel returns to the lossy datagram plane. Finishing
is graceful: queued frames are sent, then a FIN; the peer answers with its own
FIN and stops using the stream, and both sides free it once it is closed. The
channel id is freed as usual.

If a client opens the interface reliable again while the agent's old stream is
still closing, the hub opens the new stream only once the old one is closed, so
the agent's frames on both never interleave. Until then the agent leg takes no
client frames (they wait on the client's stream). Reliability starts once the
agent has the stream: frames the agent sends before that, on the first opening
or a reopening, travel as datagrams like any lossy frame, and one of them may
arrive after the first frames of the stream.

**TCP / TLS fallback.** The fallback transports have no useful multistream: the
whole connection is already one reliable, ordered byte stream shared by both
planes. There the reliable flag is a no-op — the channel is already reliable, but
with connection-wide head-of-line blocking. The HOL-isolation benefit is
QUIC-only; this is the same documented fallback trade-off as the lossy plane
losing latest-wins semantics over TCP.

## Transactions

A request/response exchange driven from a remote client pays one network round
trip per exchange: a UDS firmware flash is tens of thousands of exchanges, hours
over a cellular link. A **transaction** moves the gating loop to the agent: the
client streams a declarative sequence of steps (transmit a frame, wait for the
expected answer) and the agent executes it at the bus, at bus round-trip time.
The name is colloquial: a transaction runs to completion or to the first failure;
CAN has no atomicity and nothing is rolled back.

**Capability.** An agent that runs transactions advertises bit 1 of the HELLO
`capabilities` bitset (transactions), and bit 2 if it also runs ISO-TP steps
(below). If the agent owning the interface did not advertise bit 1, or a
TXN_BEGIN with flag bit 1 (ISO-TP) reaches an agent that did not advertise bit
2, the hub answers status unsupported (3).

**Prerequisites.** The client runs a transaction on a channel it has open, and:

- the channel must be **reliable** (OPEN flag bit 2): its reliable stream carries
  the steps. Otherwise the hub answers status reliable channel required (4). Over
  TCP/TLS the flag is a no-op but must still be set;
- the client must be authorized to **write** the interface (ACL `rw`; unix and
  plain tcp clients are trusted). Otherwise the hub answers status write denied (2).

**One transaction per agent.** An agent runs one transaction at a time across all
its interfaces. A TXN_BEGIN while one is active — on any of the agent's
interfaces, from any client — is answered with status busy (1).

**Paths.** TXN_BEGIN, TXN_BEGIN_ACK, TXN_PROGRESS, TXN_STATUS and TXN_ABORT
travel on the control stream. TXN_TEMPLATE and TXN_STEPS travel on the channel's
reliable stream (over TCP/TLS, the connection), so templates and steps reach the
agent in the order the client sent them, and a large window of steps never
head-of-line-blocks the control stream. No transaction message travels as a
datagram. Every message carries the sender's `channel` as the first byte of its
body; the hub rewrites it between the client's and the agent's channel, as it
does for FRAME, and otherwise relays the message as is.

**Lifecycle.**

1. The client sends TXN_BEGIN. The agent answers TXN_BEGIN_ACK with status ok,
   the `txn_id` and the `window` in bytes (below); the hub may answer a rejection
   itself.
2. The client sends the TXN_TEMPLATEs, then TXN_STEPS batches. Sequence numbers
   start at 0 and are contiguous: a batch whose `first_seq` is not the next
   expected one aborts the transaction (reason invalid step). A template is
   defined once per transaction, before the first step that names it: a step
   naming an undefined template, or a second definition of the same id, aborts
   the transaction (reason invalid step). Steps already queued in the agent would
   otherwise run with a template sent after them.
3. The agent executes the steps and reports TXN_PROGRESS.
4. The step flagged **last** completes the transaction: TXN_STATUS done. The
   first failure ends it instead: TXN_STATUS aborted, with the reason.

Messages naming another `txn_id` than the active one are stale (steps still in
flight from a cancelled transaction) and are discarded.

**Steps.** A step transmits a frame (`can_id`, `frame_flags`, payload), unless it
is expect-only, and then waits as its template says. The frame goes through the
normal injection path: its bus echo reaches the other subscribers like any
injected frame, and the frames the agent receives meanwhile are still forwarded
to every subscriber — the runner only matches, it never hides traffic. The bus
echo of a frame the agent sent never matches a pattern. A frame the bus queue
refuses is retried until the step times out (reason tx failed); for a step that
does not wait, `timeout_ms` bounds those retries (an agent default when zero).

**Templates.** A template (id 0-15) sets how a step waits:

- `advance_mode`: 0 none (the step completes once its frame is sent), 1 count
  (`count_k` frames matching the advance pattern), 2 roster all (every roster
  origin answered), 3 roster k (`count_k` distinct roster origins answered);
- three patterns, each present if its bit is set in `patterns_present`: bit 0
  advance, bit 1 abort, bit 2 extend. A frame matches a pattern when
  `(can_id & can_mask) == (pattern.can_id & can_mask)` and
  `(P & payload_mask) == (payload_value & payload_mask)`, where P is the first 8
  payload bytes as a little-endian u64 (byte i at bits 8i-8i+7; missing bytes
  read as zero). `can_id` includes the EFF/RTR/ERR flag bits;
- each received frame is checked against extend first (the step timeout
  restarts), then abort (the transaction aborts, reason abort match), then
  advance. Extend goes first because it is the narrower answer: a UDS response
  pending (`7F xx 78`) is also a negative response (`7F`);
- a step with the expect-value flag replaces the advance pattern's
  `payload_value` with its own `expect_value` (a per-step answer such as a block
  counter or a batch CRC). A frame that matches the advance `can_id` but not the
  payload is a mismatch: it aborts the transaction (reason value mismatch,
  `expected` and `got` reported) if the template has the mismatch-abort flag, and
  is ignored otherwise;
- `timeout_ms` bounds the wait from the step's start; it must be non-zero for a
  waiting mode. After an extend match the wait restarts with
  `extend_timeout_ms` instead (the UDS P2* time after a response pending), or
  `timeout_ms` again when it is zero. On expiry the step aborts the
  transaction (reason timeout), or completes if the template has the soft flag;
- `gap_us` is the minimum delay between this step's frame and the next step's.

**ISO-TP steps.** A template of `type` 1 makes each of its steps one ISO
15765-2 message instead of one frame (UDS requests longer than a frame, such as
TransferData). The agent segments the step's message into a single frame, or a
first frame and consecutive frames, on the step's `can_id`, honouring the block
size and STmin of the flow control (FC) the peer sends on `isotp_response_id`.
It reassembles the answer arriving on `isotp_response_id`, answering its first
frame with an FC on `can_id` carrying `isotp_block_size` and `isotp_st_min`.
Then:

- the patterns match the **reassembled message**: the id compared is
  `isotp_response_id`, P is the first 8 bytes of the message (after the
  protocol control information and the address byte). Flow-control and partial
  frames never match. Every message received is checked against extend, abort
  and advance as frames are, so a response pending (`7F xx 78`) restarts the
  timeout and the agent keeps listening;
- `timeout_ms` runs from the moment the last frame of the request is sent (the
  UDS P2 time), and `gap_us` from then too. An expect-only step waits for a
  message without sending one; its `can_id` is still the request id, where
  the flow control for a multi-frame answer goes;
- the frames themselves travel the normal paths: subscribers see the segments
  and the flow control on the bus as they see any traffic;
- `isotp_flags` bit 0 (padding): every frame of up to 8 bytes the agent sends
  is filled to 8 with `isotp_padding`; without it each such frame is as short
  as its data allows. A longer CAN FD frame is always filled with
  `isotp_padding` up to the next valid data length. Bit 1 (address byte): every frame the agent sends starts with
  `isotp_tx_address`, and only frames on `isotp_response_id` starting with
  `isotp_rx_address` belong to the answer (extended addressing: target and
  source address; mixed addressing: the address extension, the same byte both
  ways). Bit 2 (CAN FD): the agent sends CAN FD frames with the data length
  `isotp_tx_data_length` (8, 12, 16, 20, 24, 32, 48 or 64), bit 3 with bit rate
  switch, and a message that fits one such frame goes as a single frame with
  the escaped length (first byte 0, length in the second); it accepts answers
  in classic or FD frames either way;
- a message is 1-4095 bytes: the escape for longer first frames is not
  supported, and an answer announcing more is refused with an FC overflow;
- the agent bounds the transfer with the standard timers: 1000 ms for the
  peer's FC (N_Bs) and between the consecutive frames of an answer (N_Cr), and
  at most 10 consecutive FC wait. A failure aborts the transaction with reasons
  10 flow-control timeout, 11 consecutive-frame timeout, 12 overflow (the peer
  answered FC overflow, or announced more than 4095 bytes), 13 sequence error
  (a consecutive frame out of order) or 14 wait limit.

A step of an ISO-TP template carries its message in records with the
**message** flag (bit 3), whose payload is part of the message (1-255 bytes,
`frame_flags` 0) instead of a frame. A message longer than one record goes on in
the next ones: every record but the last of the step carries the **continued**
flag (bit 4), and names the same `template_id` and `can_id`; only the last one
may carry the expect value or the last-step flag. A continued record is not a
step: sequence numbers count steps, so a batch may start or end inside one, and
`first_seq` is the sequence number of the step its first record belongs to. The
agent copies each record of a message out of the ring as the step reaches it and
releases its bytes at once, so a message never has to fit in the window whole. A
message record naming a generic template, a frame record naming an ISO-TP one or
a message over 4095 bytes abort the transaction (reason invalid step). The
advance modes of an ISO-TP template are none and count.

**Roster.** TXN_BEGIN carries up to 64 expected origins and how to extract a
frame's origin: from the CAN id (X = id bits 0-28) or from the payload (X = the
4 bytes at `payload_offset`, little-endian, missing bytes read as zero), then
`origin = (X & mask) >> shift`. In the roster modes, a frame matching the advance
pattern counts once per origin: a per-step bitmap (bit i = `roster[i]` answered)
drops retransmissions, and frames from origins outside the roster do not count.
A failure report carries that bitmap, so the client learns who did not answer,
and the origin of the frame that triggered it.

**Credit.** The agent buffers steps in a ring of fixed size, announced as
`window` in TXN_BEGIN_ACK: the number of step record bytes (the 8-byte-aligned
records of TXN_STEPS, not the message headers) the client may have in flight.
The agent releases a step's bytes when the step completes and reports the
running total in TXN_PROGRESS `consumed_bytes` (modulo 2^32). The client must
keep `bytes sent - consumed_bytes <= window`; a batch that does not fit in the
ring aborts the transaction (reason overrun). The hub lowers the window it
relays to what it can hold towards the agent (48 KiB of records), so a client
that keeps to it never fills the hub's stream to the agent, whatever transport
the client uses; over QUIC the hub also stops reading the client's stream while
the agent's is full. Client libraries keep the steps
not yet sent locally and send them as credit returns, so an application may hand
over a whole firmware image at once. The agent sends TXN_PROGRESS once the bytes
released since the previous one reach a quarter of the window, or 10 ms after
the first byte released since the previous one, so the pipe stays full without
one message per step. Credit is explicit rather than left to the transport because over TCP/TLS
the agent cannot stop reading the connection without stalling the control
stream; over QUIC, stream flow control backs it up.

**Starvation.** If the ring stays empty while the transaction is incomplete for
longer than an agent-defined deadline, the transaction aborts (reason starved).

**Exclusive lease.** TXN_BEGIN flag bit 0 asks for exclusive write access to the
interface for the life of the transaction, from TXN_BEGIN_ACK ok to TXN_STATUS.
Meanwhile the hub drops and counts the FRAMEs other clients send to that
interface, lossy and reliable channels alike, and tells them up front: it sends
INTERFACE_STATUS with flag bit 1 (leased) set to every other client holding the
interface open when the lease starts, to a client that opens it while leased,
and cleared when it ends. The lease
excludes other can-hub clients only: hub bridge routes, local processes on the
device and the other nodes on the bus still talk.

**Cancellation.** TXN_ABORT cancels the transaction (TXN_STATUS aborted, reason
cancelled). If the owner disconnects, the hub sends TXN_ABORT to the agent itself
and releases the lease. If the agent disconnects, the hub answers the client with
TXN_STATUS aborted, reason agent disconnected.

```
TXN_BEGIN (total 272)
@4   channel u8
@5   flags u8 (bit 0: exclusive lease, bit 1: uses ISO-TP templates)
@6   origin_source u8 (0 none, 1 can_id, 2 payload)
@7   origin_shift u8
@8   origin_mask u32
@12  origin_payload_offset u8
@13  roster_count u8 (0-64)
@14  reserved u16
@16  roster u32[64] (first roster_count used, the rest zero)

TXN_BEGIN_ACK (total 12)
@4   channel u8
@5   status u8 (0 ok, 1 busy, 2 write denied, 3 unsupported,
                4 reliable channel required, 5 invalid)
@6   txn_id u16
@8   window u32 (step record bytes the client may have in flight)

TXN_TEMPLATE (total 112)
@4   channel u8
@5   template_id u8 (0-15)
@6   txn_id u16
@8   advance_mode u8 (0 none, 1 count, 2 roster all, 3 roster k)
@9   flags u8 (bit 0: soft, bit 1: mismatch abort)
@10  count_k u8
@11  patterns_present u8 (bit 0 advance, bit 1 abort, bit 2 extend)
@12  timeout_ms u32
@16  gap_us u32
@20  type u8 (0 generic, 1 ISO-TP)
@21  reserved u8[3]
@24  patterns[3] (advance, abort, extend), 24 bytes each:
       +0  can_id u32
       +4  can_mask u32
       +8  payload_mask u64
       +16 payload_value u64
@96  isotp_response_id u32 (as FRAME can_id; ISO-TP only, 0 otherwise)
@100 isotp_flags u8 (bit 0 padding, bit 1 address byte, bit 2 CAN FD,
                     bit 3 bit rate switch)
@101 isotp_tx_address u8
@102 isotp_rx_address u8
@103 isotp_padding u8
@104 isotp_tx_data_length u8 (8 classic; 8-64 CAN FD)
@105 isotp_block_size u8 (0: the peer sends the whole answer without waiting)
@106 isotp_st_min u8 (ISO 15765-2 encoding: 0-127 ms, 0xF1-0xF9 100-900 us)
@107 reserved u8
@108 extend_timeout_ms u32 (0: timeout_ms again after an extend)

TXN_STEPS (total 16 + records, records at most 400 bytes)
@4   channel u8
@5   step_count u8 (records in the batch)
@6   txn_id u16
@8   first_seq u32 (the step the first record belongs to)
@12  reserved u32
@16  records, back to back, each:
       +0  template_id u8
       +1  flags u8 (bit 0: expect value present, bit 1: expect-only, no
                     frame, bit 2: last step, bit 3: message (ISO-TP), bit 4:
                     continued in the next record)
       +2  payload_length u8 (0-8 classic, 0-64 FD; 0 for expect-only;
                              1-255 for a message record)
       +3  frame_flags u8 (as FRAME)
       +4  can_id u32 (as FRAME; 0 for expect-only, except for ISO-TP: the
                       request id)
       +8  payload, zero-padded to a multiple of 8 bytes
       then expect_value u64, if flag bit 0

TXN_PROGRESS (total 16)
@4   channel u8
@5   reserved u8
@6   txn_id u16
@8   completed_steps u32
@12  consumed_bytes u32 (record bytes released, modulo 2^32)

TXN_STATUS (total 116)
@4   channel u8
@5   state u8 (0 done, 1 aborted)
@6   txn_id u16
@8   failing_seq u32
@12  offending_origin u32
@16  seen_bitmap u64 (roster steps: bit i set if roster[i] answered)
@24  expected u64
@32  got u64
@40  completed_steps u32
@44  rx_can_id u32
@48  reason u8 (0 none, 1 timeout, 2 abort match, 3 value mismatch,
                4 tx failed, 5 cancelled, 6 starved, 7 overrun,
                8 agent disconnected, 9 invalid step, 10 flow-control
                timeout, 11 consecutive-frame timeout, 12 overflow, 13
                sequence error, 14 wait limit)
@49  rx_payload_length u8
@50  rx_frame_flags u8
@51  flags u8 (bit 0: rx frame present — the last frame, or the first 64
               bytes of the last ISO-TP message, that matched a pattern of the
               failing step)
@52  rx_payload u8[64]

TXN_ABORT (total 8)
@4   channel u8
@5   reserved u8
@6   txn_id u16
```

A classic 8-byte step is a 16-byte record and an expect-only step 8 bytes, so a
1 MB flash in batches of 16 frames plus one acknowledgement needs about 2 bytes
of records per firmware byte.
An ISO-TP message travels in records of up to 255 bytes: a 4095-byte
TransferData request takes 17 records, about 4.2 KB.

## External protocol bridging

This document specifies the can-hub wire protocol only. Foreign protocols are bridged outside it, not added to the message set. socketcand (its own ASCII protocol, separate spec) is bridged by `can-hub-client socketcand`, which terminates socketcand locally and speaks ordinary HELLO/LIST/OPEN/FRAME to the hub on the client's behalf — the hub sees a normal client. See doc/design.md "Compatibility adapters".

## Open questions

- Version negotiation details and capability bits in HELLO.
- Flow control / backpressure signalling on the data plane.
- P2P phase 2: endpoint exchange messages (OFFER/ANSWER pattern).
- Timestamp cost: u64 is the largest FRAME field. A u32 microsecond timestamp relative to a negotiated session base (wraps every ~71 min) would save 4 bytes per frame.
