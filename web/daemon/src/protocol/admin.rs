//! Admin message family (0x10..=0x31): request encoders and reply decoders.
//!
//! The layouts mirror the authoritative C codec in
//! `src/protocol/admin_message.c`, including the pin/ACL mutations
//! (0x22, 0x24..=0x29) and the bus-to-bus bridges (0x2A, 0x2B, 0x2E..=0x31).

use super::{
    read_fixed_str, read_u16, read_u32, read_u64, write_fixed_str, DecodeError, Header,
    MessageType, HEADER_SIZE,
};

const AGENT_NAME_SIZE: usize = 128;
const INTERFACE_NAME_SIZE: usize = 16;
const FINGERPRINT_SIZE: usize = 65;
const ORIGIN_SIZE: usize = 56;
const BRIDGE_ENDPOINT_SIZE: usize = AGENT_NAME_SIZE + INTERFACE_NAME_SIZE;
const BRIDGE_FILTER_SIZE: usize = 8;
const BRIDGE_ENTRY_SIZE: usize = 440;
const BRIDGE_STATE_SOURCE_ONLINE: u8 = 0x01;
const BRIDGE_STATE_DESTINATION_ONLINE: u8 = 0x02;

/// Filters a bridge route carries at most, as SUBSCRIBE does.
pub const BRIDGE_FILTERS_MAX: usize = 16;

/// ADMIN_BRIDGE_SET_REPLY / ADMIN_BRIDGE_DELETE_REPLY status codes.
pub const BRIDGE_STATUS_INVALID: u8 = 1;
pub const BRIDGE_STATUS_FULL: u8 = 2;
pub const BRIDGE_STATUS_STORE_FAILED: u8 = 3;
pub const BRIDGE_STATUS_UNKNOWN: u8 = 4;

/// An id/mask pair: a frame passes when `id & mask == can_id & mask`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CanFilter {
    pub can_id: u32,
    pub can_mask: u32,
}

/// One end of a bridge route: a namespaced interface.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BridgeEndpoint {
    pub agent_name: String,
    pub interface_name: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BridgeEntry {
    pub source: BridgeEndpoint,
    pub destination: BridgeEndpoint,
    pub filters: Vec<CanFilter>,
    pub source_online: bool,
    pub destination_online: bool,
    pub forwarded: u64,
    pub dropped: u64,
}

/// A page of a paginated admin reply, plus whether more entries exist beyond
/// `offset + entries.len()` (header flags bit 0).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Page<T> {
    pub entries: Vec<T>,
    pub more: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Status {
    pub peer_count: u16,
    pub agent_count: u16,
    pub client_count: u16,
    pub interface_count: u16,
    pub receive_overflows: u32,
    pub frames_received: u64,
    pub frames_forwarded: u64,
    pub frames_dropped: u64,
    pub frames_unroutable: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PeerEntry {
    pub peer_id: u32,
    pub frames_forwarded: u32,
    pub frames_dropped: u32,
    pub uptime_seconds: u32,
    pub role: u8,
    pub transport_kind: u8,
    pub agent_name: String,
    pub fingerprint_hex: String,
    pub origin: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AgentEntry {
    pub peer_id: u32,
    pub interface_count: u8,
    pub agent_name: String,
    pub fingerprint_hex: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ClientEntry {
    pub peer_id: u32,
    pub interface_id: u32,
    /// 0xFF means the client holds no open channel.
    pub channel: u8,
    pub agent_name: String,
    pub interface_name: String,
    pub frames_forwarded: u32,
    pub frames_dropped: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PinEntry {
    pub agent_name: String,
    pub fingerprint_hex: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InterfaceEntry {
    pub interface_id: u32,
    pub subscriber_count: u8,
    pub frames_received: u64,
    pub tx_dropped: u64,
    pub agent_name: String,
    pub interface_name: String,
}

/// Why the hub turned an identity away (ADMIN_REJECTED_REPLY entry @1).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RejectionReason {
    NotAllowed,
    FingerprintMismatch,
}

impl RejectionReason {
    pub fn from_u8(raw: u8) -> RejectionReason {
        match raw {
            1 => RejectionReason::FingerprintMismatch,
            _ => RejectionReason::NotAllowed,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RejectedEntry {
    pub role: u8,
    pub reason: RejectionReason,
    pub attempts: u32,
    pub last_seen_seconds: u32,
    pub name: String,
    pub fingerprint_hex: String,
    pub origin: String,
}

/// ACL permission level on the wire as a `can_read`/`can_write`/`can_admin`
/// byte triple. Admin (reconfigure the interface) always comes with rw.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AclLevel {
    None,
    Read,
    ReadWrite,
    ReadWriteAdmin,
}

impl AclLevel {
    pub fn to_bytes(self) -> (u8, u8, u8) {
        match self {
            AclLevel::None => (0, 0, 0),
            AclLevel::Read => (1, 0, 0),
            AclLevel::ReadWrite => (1, 1, 0),
            AclLevel::ReadWriteAdmin => (1, 1, 1),
        }
    }

    pub fn from_bytes(can_read: u8, can_write: u8, can_admin: u8) -> AclLevel {
        if can_admin != 0 {
            return AclLevel::ReadWriteAdmin;
        }
        match (can_read != 0, can_write != 0) {
            (_, true) => AclLevel::ReadWrite,
            (true, false) => AclLevel::Read,
            (false, false) => AclLevel::None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AclEntry {
    pub fingerprint_hex: String,
    pub agent_name: String,
    pub interface_name: String,
    pub level: AclLevel,
}

pub const SETTING_BITRATE: u32 = 1 << 0;
pub const SETTING_SAMPLE_POINT: u32 = 1 << 1;
pub const SETTING_DATA_BITRATE: u32 = 1 << 2;
pub const SETTING_DATA_SAMPLE_POINT: u32 = 1 << 3;
pub const SETTING_CTRLMODE: u32 = 1 << 4;
pub const SETTING_RESTART_MS: u32 = 1 << 5;
pub const SETTING_TXQUEUELEN: u32 = 1 << 6;
pub const SETTING_LINK: u32 = 1 << 7;
pub const SETTING_STATE: u32 = 1 << 8;
pub const SETTING_BIT_TIMING: u32 = 1 << 9;
pub const SETTING_DATA_BIT_TIMING: u32 = 1 << 10;
pub const SETTING_TERMINATION: u32 = 1 << 11;

pub const CTRLMODE_LOOPBACK: u32 = 1 << 0;
pub const CTRLMODE_LISTEN_ONLY: u32 = 1 << 1;
pub const CTRLMODE_TRIPLE_SAMPLING: u32 = 1 << 2;
pub const CTRLMODE_ONE_SHOT: u32 = 1 << 3;
pub const CTRLMODE_BERR_REPORTING: u32 = 1 << 4;
pub const CTRLMODE_FD: u32 = 1 << 5;

const SETTINGS_SIZE: usize = 88;
pub const CONFIGURE_DETAIL_SIZE: usize = 64;

/// Raw bit timing: time quantum in ns and segment lengths in quanta.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct BitTiming {
    pub tq: u32,
    pub prop_seg: u32,
    pub phase_seg1: u32,
    pub phase_seg2: u32,
    pub sjw: u32,
    pub brp: u32,
}

impl BitTiming {
    fn write(&self, out: &mut [u8], offset: usize) {
        let members = [self.tq, self.prop_seg, self.phase_seg1, self.phase_seg2, self.sjw, self.brp];
        for (index, value) in members.iter().enumerate() {
            out[offset + index * 4..offset + index * 4 + 4].copy_from_slice(&value.to_le_bytes());
        }
    }

    fn read(buffer: &[u8], offset: usize) -> BitTiming {
        BitTiming {
            tq: read_u32(buffer, offset),
            prop_seg: read_u32(buffer, offset + 4),
            phase_seg1: read_u32(buffer, offset + 8),
            phase_seg2: read_u32(buffer, offset + 12),
            sjw: read_u32(buffer, offset + 16),
            brp: read_u32(buffer, offset + 20),
        }
    }
}

/// The settings block of CONFIGURE / CONFIGURE_REPLY. `fields` names the
/// members that carry a value: what to change in a request, what the device
/// reports in a reply. Sample points are in tenths of a percent.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct InterfaceSettings {
    pub fields: u32,
    pub bitrate: u32,
    pub data_bitrate: u32,
    pub sample_point: u16,
    pub data_sample_point: u16,
    pub ctrlmode_mask: u32,
    pub ctrlmode_flags: u32,
    pub restart_ms: u32,
    pub txqueuelen: u32,
    pub link_up: bool,
    pub can_state: u8,
    pub termination: u16,
    pub bit_timing: BitTiming,
    pub data_bit_timing: BitTiming,
}

impl InterfaceSettings {
    fn write(&self, out: &mut [u8], offset: usize) {
        out[offset..offset + 4].copy_from_slice(&self.fields.to_le_bytes());
        out[offset + 4..offset + 8].copy_from_slice(&self.bitrate.to_le_bytes());
        out[offset + 8..offset + 12].copy_from_slice(&self.data_bitrate.to_le_bytes());
        out[offset + 12..offset + 14].copy_from_slice(&self.sample_point.to_le_bytes());
        out[offset + 14..offset + 16].copy_from_slice(&self.data_sample_point.to_le_bytes());
        out[offset + 16..offset + 20].copy_from_slice(&self.ctrlmode_mask.to_le_bytes());
        out[offset + 20..offset + 24].copy_from_slice(&self.ctrlmode_flags.to_le_bytes());
        out[offset + 24..offset + 28].copy_from_slice(&self.restart_ms.to_le_bytes());
        out[offset + 28..offset + 32].copy_from_slice(&self.txqueuelen.to_le_bytes());
        out[offset + 32] = u8::from(self.link_up);
        out[offset + 33] = self.can_state;
        out[offset + 34..offset + 36].copy_from_slice(&self.termination.to_le_bytes());
        self.bit_timing.write(out, offset + 36);
        self.data_bit_timing.write(out, offset + 60);
    }

    fn read(buffer: &[u8], offset: usize) -> InterfaceSettings {
        InterfaceSettings {
            fields: read_u32(buffer, offset),
            bitrate: read_u32(buffer, offset + 4),
            data_bitrate: read_u32(buffer, offset + 8),
            sample_point: read_u16(buffer, offset + 12),
            data_sample_point: read_u16(buffer, offset + 14),
            ctrlmode_mask: read_u32(buffer, offset + 16),
            ctrlmode_flags: read_u32(buffer, offset + 20),
            restart_ms: read_u32(buffer, offset + 24),
            txqueuelen: read_u32(buffer, offset + 28),
            link_up: buffer[offset + 32] != 0,
            can_state: buffer[offset + 33],
            termination: read_u16(buffer, offset + 34),
            bit_timing: BitTiming::read(buffer, offset + 36),
            data_bit_timing: BitTiming::read(buffer, offset + 60),
        }
    }
}

/// CONFIGURE op @148: apply the settings or only read them back.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ConfigureOp {
    Apply = 0,
    Query = 1,
}

/// CONFIGURE_REPLY status codes.
pub const CONFIGURE_STATUS_OK: u8 = 0;
pub const CONFIGURE_STATUS_UNKNOWN_INTERFACE: u8 = 1;
pub const CONFIGURE_STATUS_AGENT_UNREACHABLE: u8 = 2;
pub const CONFIGURE_STATUS_DENIED: u8 = 4;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ConfigureReply {
    pub status: u8,
    pub settings: InterfaceSettings,
    pub detail: String,
}

// ---------------------------------------------------------------- requests

/// ADMIN_STATUS (total 4): header only.
pub fn encode_status() -> [u8; 4] {
    let mut out = [0u8; 4];
    Header::write(&mut out, MessageType::AdminStatus, 0, 0);
    out
}

/// ADMIN_PEERS / ADMIN_INTERFACES (total 8): a pagination offset.
pub fn encode_peers(offset: u16) -> [u8; 8] {
    encode_paginated(MessageType::AdminPeers, offset)
}

pub fn encode_interfaces(offset: u16) -> [u8; 8] {
    encode_paginated(MessageType::AdminInterfaces, offset)
}

pub fn encode_pins(offset: u16) -> [u8; 8] {
    encode_paginated(MessageType::AdminPins, offset)
}

pub fn encode_rejected(offset: u16) -> [u8; 8] {
    encode_paginated(MessageType::AdminRejected, offset)
}

fn encode_paginated(message_type: MessageType, offset: u16) -> [u8; 8] {
    let mut out = [0u8; 8];
    Header::write(&mut out, message_type, 0, 4);
    out[4..6].copy_from_slice(&offset.to_le_bytes());
    out
}

/// ADMIN_AGENTS / ADMIN_CLIENTS (total 136): offset + an agent-name filter
/// (empty = all).
pub fn encode_agents(offset: u16, agent_filter: &str) -> [u8; 136] {
    encode_filtered(MessageType::AdminAgents, offset, agent_filter)
}

pub fn encode_clients(offset: u16, agent_filter: &str) -> [u8; 136] {
    encode_filtered(MessageType::AdminClients, offset, agent_filter)
}

fn encode_filtered(message_type: MessageType, offset: u16, agent_filter: &str) -> [u8; 136] {
    let mut out = [0u8; 136];
    Header::write(&mut out, message_type, 0, 132);
    out[4..6].copy_from_slice(&offset.to_le_bytes());
    write_fixed_str(&mut out, 8, AGENT_NAME_SIZE, agent_filter);
    out
}

/// ADMIN_KICK / ADMIN_FORGET (total 132): a target agent name.
pub fn encode_kick(agent_name: &str) -> [u8; 132] {
    encode_named(MessageType::AdminKick, agent_name)
}

pub fn encode_forget(agent_name: &str) -> [u8; 132] {
    encode_named(MessageType::AdminForget, agent_name)
}

fn encode_named(message_type: MessageType, agent_name: &str) -> [u8; 132] {
    let mut out = [0u8; 132];
    Header::write(&mut out, message_type, 0, 128);
    write_fixed_str(&mut out, 4, AGENT_NAME_SIZE, agent_name);
    out
}

/// ADMIN_KICK_PEER (total 8): a peer id.
pub fn encode_kick_peer(peer_id: u32) -> [u8; 8] {
    let mut out = [0u8; 8];
    Header::write(&mut out, MessageType::AdminKickPeer, 0, 4);
    out[4..8].copy_from_slice(&peer_id.to_le_bytes());
    out
}

/// CONFIGURE (total 240): apply or read back one interface's settings by
/// namespaced pair. Body: agent_name[128] @4, interface_name[16] @132,
/// op @148, 3 reserved, settings[88] @152.
pub fn encode_configure(
    agent_name: &str,
    interface_name: &str,
    op: ConfigureOp,
    settings: &InterfaceSettings,
) -> [u8; 240] {
    let mut out = [0u8; 240];
    Header::write(&mut out, MessageType::Configure, 0, 236);
    write_fixed_str(&mut out, 4, AGENT_NAME_SIZE, agent_name);
    write_fixed_str(&mut out, 132, INTERFACE_NAME_SIZE, interface_name);
    out[148] = op as u8;
    settings.write(&mut out, 152);
    out
}

/// ADMIN_PIN_ADD (total 200): authorize an agent fingerprint.
/// Body layout from `src/protocol/admin_message.c`: agent_name[128] @4,
/// fingerprint_hex[65] @132, 3 reserved.
pub fn encode_pin_add(agent_name: &str, fingerprint_hex: &str) -> [u8; 200] {
    let mut out = [0u8; 200];
    Header::write(&mut out, MessageType::AdminPinAdd, 0, 196);
    write_fixed_str(&mut out, 4, AGENT_NAME_SIZE, agent_name);
    write_fixed_str(&mut out, 132, FINGERPRINT_SIZE, fingerprint_hex);
    out
}

/// ADMIN_ACL_SET (total 216): grant a client read/write on an interface.
/// Body layout from `src/protocol/admin_message.c`: agent_name[128] @4,
/// interface_name[16] @132, fingerprint_hex[65] @148, can_read @213,
/// can_write @214, can_admin @215.
pub fn encode_acl_set(
    fingerprint_hex: &str,
    agent_name: &str,
    interface_name: &str,
    level: AclLevel,
) -> [u8; 216] {
    let mut out = encode_acl_target(MessageType::AdminAclSet, agent_name, interface_name, fingerprint_hex);
    let (can_read, can_write, can_admin) = level.to_bytes();
    out[213] = can_read;
    out[214] = can_write;
    out[215] = can_admin;
    out
}

/// ADMIN_ACL_REVOKE (total 216): drop a client grant. Same layout as ACL_SET
/// without the can_read/can_write bytes.
pub fn encode_acl_revoke(
    fingerprint_hex: &str,
    agent_name: &str,
    interface_name: &str,
) -> [u8; 216] {
    encode_acl_target(MessageType::AdminAclRevoke, agent_name, interface_name, fingerprint_hex)
}

fn encode_acl_target(
    message_type: MessageType,
    agent_name: &str,
    interface_name: &str,
    fingerprint_hex: &str,
) -> [u8; 216] {
    let mut out = [0u8; 216];
    Header::write(&mut out, message_type, 0, 212);
    write_fixed_str(&mut out, 4, AGENT_NAME_SIZE, agent_name);
    write_fixed_str(&mut out, 132, INTERFACE_NAME_SIZE, interface_name);
    write_fixed_str(&mut out, 148, FINGERPRINT_SIZE, fingerprint_hex);
    out
}

/// ADMIN_ACL_LIST (total 8): a pagination offset.
pub fn encode_acl_list(offset: u16) -> [u8; 8] {
    encode_paginated(MessageType::AdminAclList, offset)
}

/// ADMIN_BRIDGE_SET (total 424): source @4 and destination @148, each
/// agent_name[128] + interface_name[16]; filter_count @292; filters[16] @296,
/// each can_id u32 + can_mask u32. Filters past the 16th are not sent.
pub fn encode_bridge_set(source: &BridgeEndpoint, destination: &BridgeEndpoint, filters: &[CanFilter]) -> [u8; 424] {
    let mut out = [0u8; 424];
    let sent = &filters[..filters.len().min(BRIDGE_FILTERS_MAX)];
    Header::write(&mut out, MessageType::AdminBridgeSet, 0, 420);
    write_bridge_endpoints(&mut out, source, destination);
    out[HEADER_SIZE + 2 * BRIDGE_ENDPOINT_SIZE] = sent.len() as u8;
    for (index, filter) in sent.iter().enumerate() {
        let base = HEADER_SIZE + 292 + index * BRIDGE_FILTER_SIZE;
        out[base..base + 4].copy_from_slice(&filter.can_id.to_le_bytes());
        out[base + 4..base + 8].copy_from_slice(&filter.can_mask.to_le_bytes());
    }
    out
}

/// ADMIN_BRIDGE_DELETE (total 292): source @4, destination @148.
pub fn encode_bridge_delete(source: &BridgeEndpoint, destination: &BridgeEndpoint) -> [u8; 292] {
    let mut out = [0u8; 292];
    Header::write(&mut out, MessageType::AdminBridgeDelete, 0, 288);
    write_bridge_endpoints(&mut out, source, destination);
    out
}

/// ADMIN_BRIDGE_LIST (total 8): a pagination offset.
pub fn encode_bridge_list(offset: u16) -> [u8; 8] {
    encode_paginated(MessageType::AdminBridgeList, offset)
}

fn write_bridge_endpoints(out: &mut [u8], source: &BridgeEndpoint, destination: &BridgeEndpoint) {
    write_fixed_str(out, HEADER_SIZE, AGENT_NAME_SIZE, &source.agent_name);
    write_fixed_str(out, HEADER_SIZE + AGENT_NAME_SIZE, INTERFACE_NAME_SIZE, &source.interface_name);
    write_fixed_str(out, HEADER_SIZE + BRIDGE_ENDPOINT_SIZE, AGENT_NAME_SIZE, &destination.agent_name);
    write_fixed_str(
        out,
        HEADER_SIZE + BRIDGE_ENDPOINT_SIZE + AGENT_NAME_SIZE,
        INTERFACE_NAME_SIZE,
        &destination.interface_name,
    );
}

// ---------------------------------------------------------------- replies

fn expect(buffer: &[u8], message_type: MessageType, min_len: usize) -> Result<Header, DecodeError> {
    let header = Header::parse(buffer)?;
    if header.message_type != message_type as u8 {
        return Err(DecodeError::UnexpectedType {
            expected: message_type as u8,
            got: header.message_type,
        });
    }
    if buffer.len() < min_len {
        return Err(DecodeError::TooShort { need: min_len, got: buffer.len() });
    }
    Ok(header)
}

/// ADMIN_STATUS_REPLY (total 48).
pub fn decode_status_reply(buffer: &[u8]) -> Result<Status, DecodeError> {
    expect(buffer, MessageType::AdminStatusReply, 48)?;
    Ok(Status {
        peer_count: read_u16(buffer, 4),
        agent_count: read_u16(buffer, 6),
        client_count: read_u16(buffer, 8),
        interface_count: read_u16(buffer, 10),
        receive_overflows: read_u32(buffer, 12),
        frames_received: read_u64(buffer, 16),
        frames_forwarded: read_u64(buffer, 24),
        frames_dropped: read_u64(buffer, 32),
        frames_unroutable: read_u64(buffer, 40),
    })
}

/// Decode the common paginated-reply envelope (count u8 @4, flags u8 @5,
/// entries from @8) into a `Page<T>`, parsing each fixed-size entry.
fn decode_page<T>(
    buffer: &[u8],
    message_type: MessageType,
    entry_size: usize,
    parse_entry: impl Fn(&[u8]) -> T,
) -> Result<Page<T>, DecodeError> {
    expect(buffer, message_type, 8)?;
    let count = buffer[4] as usize;
    let more = buffer[5] & 0x01 != 0;
    let needed = 8 + count * entry_size;
    if buffer.len() < needed {
        return Err(DecodeError::TruncatedEntries);
    }
    let mut entries = Vec::with_capacity(count);
    for index in 0..count {
        let base = 8 + index * entry_size;
        entries.push(parse_entry(&buffer[base..base + entry_size]));
    }
    Ok(Page { entries, more })
}

/// ADMIN_PEERS_REPLY (8 + count * 272). Entry layout (entry-relative):
/// peer_id @0, frames_forwarded @4, frames_dropped @8, role @12,
/// transport_kind @13, agent_name[128] @16, fingerprint_hex[65] @144,
/// uptime_seconds @209, origin[56] @213.
pub fn decode_peers_reply(buffer: &[u8]) -> Result<Page<PeerEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminPeersReply, 272, |entry| PeerEntry {
        peer_id: read_u32(entry, 0),
        frames_forwarded: read_u32(entry, 4),
        frames_dropped: read_u32(entry, 8),
        uptime_seconds: read_u32(entry, 209),
        role: entry[12],
        transport_kind: entry[13],
        agent_name: read_fixed_str(entry, 16, AGENT_NAME_SIZE),
        fingerprint_hex: read_fixed_str(entry, 144, FINGERPRINT_SIZE),
        origin: read_fixed_str(entry, 213, ORIGIN_SIZE),
    })
}

/// ADMIN_AGENTS_REPLY (8 + count * 204).
pub fn decode_agents_reply(buffer: &[u8]) -> Result<Page<AgentEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminAgentsReply, 204, |entry| AgentEntry {
        peer_id: read_u32(entry, 0),
        interface_count: entry[4],
        agent_name: read_fixed_str(entry, 8, AGENT_NAME_SIZE),
        fingerprint_hex: read_fixed_str(entry, 136, FINGERPRINT_SIZE),
    })
}

/// ADMIN_CLIENTS_REPLY (8 + count * 164).
pub fn decode_clients_reply(buffer: &[u8]) -> Result<Page<ClientEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminClientsReply, 164, |entry| ClientEntry {
        peer_id: read_u32(entry, 0),
        interface_id: read_u32(entry, 4),
        channel: entry[8],
        agent_name: read_fixed_str(entry, 12, AGENT_NAME_SIZE),
        interface_name: read_fixed_str(entry, 140, INTERFACE_NAME_SIZE),
        frames_forwarded: read_u32(entry, 156),
        frames_dropped: read_u32(entry, 160),
    })
}

/// ADMIN_PINS_REPLY (8 + count * 196). Entry: agent_name[128] @0,
/// fingerprint_hex[65] @128, 3 reserved.
pub fn decode_pins_reply(buffer: &[u8]) -> Result<Page<PinEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminPinsReply, 196, |entry| PinEntry {
        agent_name: read_fixed_str(entry, 0, AGENT_NAME_SIZE),
        fingerprint_hex: read_fixed_str(entry, 128, FINGERPRINT_SIZE),
    })
}

/// ADMIN_INTERFACES_REPLY (8 + count * 168).
pub fn decode_interfaces_reply(buffer: &[u8]) -> Result<Page<InterfaceEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminInterfacesReply, 168, |entry| InterfaceEntry {
        interface_id: read_u32(entry, 0),
        subscriber_count: entry[4],
        frames_received: read_u64(entry, 8),
        agent_name: read_fixed_str(entry, 16, AGENT_NAME_SIZE),
        interface_name: read_fixed_str(entry, 144, INTERFACE_NAME_SIZE),
        tx_dropped: read_u64(entry, 160),
    })
}

/// ADMIN_ACL_LIST_REPLY (8 + count * 212). Entry layout (entry-relative):
/// agent_name[128] @0, interface_name[16] @128, fingerprint_hex[65] @144,
/// can_read @209, can_write @210, can_admin @211.
pub fn decode_acl_list_reply(buffer: &[u8]) -> Result<Page<AclEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminAclListReply, 212, |entry| AclEntry {
        agent_name: read_fixed_str(entry, 0, AGENT_NAME_SIZE),
        interface_name: read_fixed_str(entry, 128, INTERFACE_NAME_SIZE),
        fingerprint_hex: read_fixed_str(entry, 144, FINGERPRINT_SIZE),
        level: AclLevel::from_bytes(entry[209], entry[210], entry[211]),
    })
}

/// ADMIN_REJECTED_REPLY (8 + count * 264). Entry layout (entry-relative):
/// role @0, reason @1, attempts @4, last_seen_seconds @8, name[128] @12,
/// fingerprint_hex[65] @140, origin[56] @205.
pub fn decode_rejected_reply(buffer: &[u8]) -> Result<Page<RejectedEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminRejectedReply, 264, |entry| RejectedEntry {
        role: entry[0],
        reason: RejectionReason::from_u8(entry[1]),
        attempts: read_u32(entry, 4),
        last_seen_seconds: read_u32(entry, 8),
        name: read_fixed_str(entry, 12, AGENT_NAME_SIZE),
        fingerprint_hex: read_fixed_str(entry, 140, FINGERPRINT_SIZE),
        origin: read_fixed_str(entry, 205, ORIGIN_SIZE),
    })
}

/// ADMIN_BRIDGE_LIST_REPLY (8 + count * 440). Entry layout (entry-relative):
/// source @0, destination @144, filter_count @288, state @289 (bit 0 source
/// online, bit 1 destination online), filters[16] @292, forwarded u64 @424,
/// dropped u64 @432.
pub fn decode_bridge_list_reply(buffer: &[u8]) -> Result<Page<BridgeEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminBridgeListReply, BRIDGE_ENTRY_SIZE, |entry| {
        let filter_count = (entry[288] as usize).min(BRIDGE_FILTERS_MAX);
        BridgeEntry {
            source: read_bridge_endpoint(entry, 0),
            destination: read_bridge_endpoint(entry, BRIDGE_ENDPOINT_SIZE),
            filters: (0..filter_count)
                .map(|index| CanFilter {
                    can_id: read_u32(entry, 292 + index * BRIDGE_FILTER_SIZE),
                    can_mask: read_u32(entry, 296 + index * BRIDGE_FILTER_SIZE),
                })
                .collect(),
            source_online: entry[289] & BRIDGE_STATE_SOURCE_ONLINE != 0,
            destination_online: entry[289] & BRIDGE_STATE_DESTINATION_ONLINE != 0,
            forwarded: read_u64(entry, 424),
            dropped: read_u64(entry, 432),
        }
    })
}

fn read_bridge_endpoint(entry: &[u8], offset: usize) -> BridgeEndpoint {
    BridgeEndpoint {
        agent_name: read_fixed_str(entry, offset, AGENT_NAME_SIZE),
        interface_name: read_fixed_str(entry, offset + AGENT_NAME_SIZE, INTERFACE_NAME_SIZE),
    }
}

/// CONFIGURE_REPLY (total 160): status @4, settings[88] @8, detail[64] @96.
pub fn decode_configure_reply(buffer: &[u8]) -> Result<ConfigureReply, DecodeError> {
    expect(buffer, MessageType::ConfigureReply, HEADER_SIZE + 4 + SETTINGS_SIZE + CONFIGURE_DETAIL_SIZE)?;
    Ok(ConfigureReply {
        status: buffer[4],
        settings: InterfaceSettings::read(buffer, 8),
        detail: read_fixed_str(buffer, 8 + SETTINGS_SIZE, CONFIGURE_DETAIL_SIZE),
    })
}

/// Status-only reply (KICK/FORGET/KICK_PEER/PIN_ADD/ACL_SET/
/// ACL_REVOKE replies): a single status byte at @4. Returns the raw code.
pub fn decode_status_byte(buffer: &[u8], message_type: MessageType) -> Result<u8, DecodeError> {
    expect(buffer, message_type, HEADER_SIZE + 1)?;
    Ok(buffer[4])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_request_is_bare_header() {
        let request = encode_status();
        assert_eq!(request, [0x10, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn peers_request_carries_offset() {
        let request = encode_peers(0x0102);
        assert_eq!(request[0], MessageType::AdminPeers as u8);
        assert_eq!(read_u16(&request, 4), 0x0102);
    }

    #[test]
    fn agents_request_carries_offset_and_filter() {
        let request = encode_agents(7, "truck42");
        assert_eq!(request.len(), 136);
        assert_eq!(read_u16(&request, 4), 7);
        assert_eq!(read_fixed_str(&request, 8, AGENT_NAME_SIZE), "truck42");
    }

    #[test]
    fn kick_peer_request_carries_id() {
        let request = encode_kick_peer(0x80000001);
        assert_eq!(request[0], MessageType::AdminKickPeer as u8);
        assert_eq!(read_u32(&request, 4), 0x80000001);
    }

    #[test]
    fn status_reply_decodes() {
        let mut buffer = [0u8; 48];
        Header::write(&mut buffer, MessageType::AdminStatusReply, 0, 44);
        buffer[4..6].copy_from_slice(&3u16.to_le_bytes()); // peer_count
        buffer[6..8].copy_from_slice(&1u16.to_le_bytes()); // agent_count
        buffer[8..10].copy_from_slice(&2u16.to_le_bytes()); // client_count
        buffer[10..12].copy_from_slice(&4u16.to_le_bytes()); // interface_count
        buffer[12..16].copy_from_slice(&7u32.to_le_bytes()); // receive_overflows
        buffer[16..24].copy_from_slice(&1000u64.to_le_bytes());
        buffer[24..32].copy_from_slice(&900u64.to_le_bytes());
        buffer[32..40].copy_from_slice(&50u64.to_le_bytes());
        buffer[40..48].copy_from_slice(&10u64.to_le_bytes());

        let status = decode_status_reply(&buffer).unwrap();
        assert_eq!(status.peer_count, 3);
        assert_eq!(status.interface_count, 4);
        assert_eq!(status.receive_overflows, 7);
        assert_eq!(status.frames_received, 1000);
        assert_eq!(status.frames_unroutable, 10);
    }

    #[test]
    fn status_reply_rejects_wrong_type() {
        let mut buffer = [0u8; 48];
        Header::write(&mut buffer, MessageType::AdminStatus, 0, 44);
        assert_eq!(
            decode_status_reply(&buffer),
            Err(DecodeError::UnexpectedType { expected: 0x11, got: 0x10 })
        );
    }

    #[test]
    fn interfaces_reply_decodes_entries_and_more_flag() {
        let count = 2usize;
        let mut buffer = vec![0u8; 8 + count * 168];
        Header::write(&mut buffer, MessageType::AdminInterfacesReply, 0, (4 + count * 168) as u16);
        buffer[4] = count as u8;
        buffer[5] = 0x01; // more
        for index in 0..count {
            let base = 8 + index * 168;
            let id = (index as u32) + 1;
            buffer[base..base + 4].copy_from_slice(&id.to_le_bytes());
            buffer[base + 4] = (index as u8) + 1; // subscriber_count
            buffer[base + 8..base + 16].copy_from_slice(&((index as u64 + 1) * 100).to_le_bytes());
            write_fixed_str(&mut buffer, base + 16, AGENT_NAME_SIZE, "truck42");
            write_fixed_str(&mut buffer, base + 144, INTERFACE_NAME_SIZE, "can0");
            buffer[base + 160..base + 168].copy_from_slice(&((index as u64 + 1) * 7).to_le_bytes());
        }

        let page = decode_interfaces_reply(&buffer).unwrap();
        assert!(page.more);
        assert_eq!(page.entries.len(), 2);
        assert_eq!(page.entries[0].interface_id, 1);
        assert_eq!(page.entries[0].subscriber_count, 1);
        assert_eq!(page.entries[1].frames_received, 200);
        assert_eq!(page.entries[1].tx_dropped, 14);
        assert_eq!(page.entries[1].agent_name, "truck42");
        assert_eq!(page.entries[1].interface_name, "can0");
    }

    #[test]
    fn peers_reply_decodes_entry() {
        let mut buffer = vec![0u8; 8 + 272];
        Header::write(&mut buffer, MessageType::AdminPeersReply, 0, (4 + 272) as u16);
        buffer[4] = 1;
        let base = 8;
        buffer[base..base + 4].copy_from_slice(&0x40000001u32.to_le_bytes());
        buffer[base + 4..base + 8].copy_from_slice(&123u32.to_le_bytes());
        buffer[base + 8..base + 12].copy_from_slice(&4u32.to_le_bytes());
        buffer[base + 12] = 1; // role agent
        buffer[base + 13] = 4; // transport quic
        write_fixed_str(&mut buffer, base + 16, AGENT_NAME_SIZE, "truck42");
        write_fixed_str(&mut buffer, base + 144, FINGERPRINT_SIZE, "ab12cd");
        buffer[base + 209..base + 213].copy_from_slice(&90u32.to_le_bytes());
        write_fixed_str(&mut buffer, base + 213, ORIGIN_SIZE, "203.0.113.7:51000");

        let page = decode_peers_reply(&buffer).unwrap();
        assert_eq!(page.entries.len(), 1);
        let peer = &page.entries[0];
        assert_eq!(peer.peer_id, 0x40000001);
        assert_eq!(peer.frames_forwarded, 123);
        assert_eq!(peer.uptime_seconds, 90);
        assert_eq!(peer.role, 1);
        assert_eq!(peer.transport_kind, 4);
        assert_eq!(peer.agent_name, "truck42");
        assert_eq!(peer.fingerprint_hex, "ab12cd");
        assert_eq!(peer.origin, "203.0.113.7:51000");
        assert!(!page.more);
    }

    #[test]
    fn clients_reply_decodes_idle_channel_sentinel() {
        let mut buffer = vec![0u8; 8 + 164];
        Header::write(&mut buffer, MessageType::AdminClientsReply, 0, (4 + 164) as u16);
        buffer[4] = 1;
        let base = 8;
        buffer[base..base + 4].copy_from_slice(&5u32.to_le_bytes());
        buffer[base + 8] = 0xFF; // idle
        let page = decode_clients_reply(&buffer).unwrap();
        assert_eq!(page.entries[0].channel, 0xFF);
        assert_eq!(page.entries[0].peer_id, 5);
    }

    #[test]
    fn page_decode_detects_truncation() {
        let mut buffer = vec![0u8; 8 + 100]; // claims 1 entry of 160 but short
        Header::write(&mut buffer, MessageType::AdminInterfacesReply, 0, 104);
        buffer[4] = 1;
        assert_eq!(decode_interfaces_reply(&buffer), Err(DecodeError::TruncatedEntries));
    }

    #[test]
    fn pins_reply_decodes_entry() {
        let mut buffer = vec![0u8; 8 + 196];
        Header::write(&mut buffer, MessageType::AdminPinsReply, 0, (4 + 196) as u16);
        buffer[4] = 1;
        write_fixed_str(&mut buffer, 8, AGENT_NAME_SIZE, "truck42");
        write_fixed_str(&mut buffer, 8 + 128, FINGERPRINT_SIZE, "ab12cd");
        let page = decode_pins_reply(&buffer).unwrap();
        assert_eq!(page.entries[0].agent_name, "truck42");
        assert_eq!(page.entries[0].fingerprint_hex, "ab12cd");
    }

    #[test]
    fn rejected_reply_decodes_entry() {
        let mut buffer = vec![0u8; 8 + 264];
        Header::write(&mut buffer, MessageType::AdminRejectedReply, 0, (4 + 264) as u16);
        buffer[4] = 1;
        buffer[8] = 2;
        buffer[9] = 1;
        buffer[12..16].copy_from_slice(&12u32.to_le_bytes());
        buffer[16..20].copy_from_slice(&8u32.to_le_bytes());
        write_fixed_str(&mut buffer, 8 + 12, AGENT_NAME_SIZE, "laptop");
        write_fixed_str(&mut buffer, 8 + 140, FINGERPRINT_SIZE, "ab12cd");
        write_fixed_str(&mut buffer, 8 + 205, ORIGIN_SIZE, "203.0.113.5:40000");
        let page = decode_rejected_reply(&buffer).unwrap();
        let entry = &page.entries[0];
        assert_eq!(entry.role, 2);
        assert_eq!(entry.reason, RejectionReason::FingerprintMismatch);
        assert_eq!(entry.attempts, 12);
        assert_eq!(entry.last_seen_seconds, 8);
        assert_eq!(entry.name, "laptop");
        assert_eq!(entry.fingerprint_hex, "ab12cd");
        assert_eq!(entry.origin, "203.0.113.5:40000");
    }

    #[test]
    fn pin_add_request_layout() {
        let request = encode_pin_add("truck42", "ab12cd");
        assert_eq!(request.len(), 200);
        assert_eq!(request[0], MessageType::AdminPinAdd as u8);
        assert_eq!(read_fixed_str(&request, 4, AGENT_NAME_SIZE), "truck42");
        assert_eq!(read_fixed_str(&request, 132, FINGERPRINT_SIZE), "ab12cd");
    }

    #[test]
    fn acl_set_request_layout() {
        let request = encode_acl_set("ab12cd", "truck42", "can0", AclLevel::ReadWrite);
        assert_eq!(request.len(), 216);
        assert_eq!(request[0], MessageType::AdminAclSet as u8);
        assert_eq!(read_fixed_str(&request, 4, AGENT_NAME_SIZE), "truck42");
        assert_eq!(read_fixed_str(&request, 132, INTERFACE_NAME_SIZE), "can0");
        assert_eq!(read_fixed_str(&request, 148, FINGERPRINT_SIZE), "ab12cd");
        assert_eq!(request[213], 1); // can_read
        assert_eq!(request[214], 1); // can_write
    }

    #[test]
    fn acl_revoke_omits_permission_bytes() {
        let request = encode_acl_revoke("ab12cd", "truck42", "can0");
        assert_eq!(request.len(), 216);
        assert_eq!(request[0], MessageType::AdminAclRevoke as u8);
        assert_eq!(request[213], 0);
        assert_eq!(request[214], 0);
    }

    #[test]
    fn bridge_set_request_layout() {
        let source = BridgeEndpoint { agent_name: "truck42".into(), interface_name: "can0".into() };
        let destination = BridgeEndpoint { agent_name: "bus7".into(), interface_name: "can1".into() };
        let request = encode_bridge_set(&source, &destination, &[CanFilter { can_id: 0x100, can_mask: 0x700 }]);
        assert_eq!(request[0], MessageType::AdminBridgeSet as u8);
        assert_eq!(read_u16(&request, 2), 420);
        assert_eq!(read_fixed_str(&request, 4, AGENT_NAME_SIZE), "truck42");
        assert_eq!(read_fixed_str(&request, 148 + AGENT_NAME_SIZE, INTERFACE_NAME_SIZE), "can1");
        assert_eq!(request[292], 1);
        assert_eq!(read_u32(&request, 296), 0x100);
        assert_eq!(read_u32(&request, 300), 0x700);
    }

    #[test]
    fn bridge_list_reply_decodes_entry() {
        let mut buffer = vec![0u8; 8 + BRIDGE_ENTRY_SIZE];
        Header::write(&mut buffer, MessageType::AdminBridgeListReply, 0, (4 + BRIDGE_ENTRY_SIZE) as u16);
        buffer[4] = 1;
        write_fixed_str(&mut buffer, 8, AGENT_NAME_SIZE, "truck42");
        write_fixed_str(&mut buffer, 8 + AGENT_NAME_SIZE, INTERFACE_NAME_SIZE, "can0");
        write_fixed_str(&mut buffer, 8 + 144, AGENT_NAME_SIZE, "bus7");
        buffer[8 + 288] = 1;
        buffer[8 + 289] = BRIDGE_STATE_SOURCE_ONLINE;
        buffer[8 + 292..8 + 296].copy_from_slice(&0x123u32.to_le_bytes());
        buffer[8 + 424..8 + 432].copy_from_slice(&42u64.to_le_bytes());
        let page = decode_bridge_list_reply(&buffer).unwrap();
        let entry = &page.entries[0];
        assert_eq!(entry.source.agent_name, "truck42");
        assert_eq!(entry.destination.agent_name, "bus7");
        assert_eq!(entry.filters, vec![CanFilter { can_id: 0x123, can_mask: 0 }]);
        assert!(entry.source_online);
        assert!(!entry.destination_online);
        assert_eq!(entry.forwarded, 42);
    }

    #[test]
    fn acl_level_byte_triples() {
        assert_eq!(AclLevel::None.to_bytes(), (0, 0, 0));
        assert_eq!(AclLevel::Read.to_bytes(), (1, 0, 0));
        assert_eq!(AclLevel::ReadWrite.to_bytes(), (1, 1, 0));
        assert_eq!(AclLevel::ReadWriteAdmin.to_bytes(), (1, 1, 1));
        assert_eq!(AclLevel::from_bytes(0, 0, 0), AclLevel::None);
        assert_eq!(AclLevel::from_bytes(1, 0, 0), AclLevel::Read);
        assert_eq!(AclLevel::from_bytes(1, 1, 0), AclLevel::ReadWrite);
        assert_eq!(AclLevel::from_bytes(1, 1, 1), AclLevel::ReadWriteAdmin);
    }

    #[test]
    fn acl_list_reply_decodes_entry() {
        let mut buffer = vec![0u8; 8 + 212];
        Header::write(&mut buffer, MessageType::AdminAclListReply, 0, (4 + 212) as u16);
        buffer[4] = 1;
        let base = 8;
        write_fixed_str(&mut buffer, base, AGENT_NAME_SIZE, "truck42");
        write_fixed_str(&mut buffer, base + 128, INTERFACE_NAME_SIZE, "can0");
        write_fixed_str(&mut buffer, base + 144, FINGERPRINT_SIZE, "ab12cd");
        buffer[base + 209] = 1; // can_read
        buffer[base + 210] = 0; // can_write

        let page = decode_acl_list_reply(&buffer).unwrap();
        let acl = &page.entries[0];
        assert_eq!(acl.agent_name, "truck42");
        assert_eq!(acl.interface_name, "can0");
        assert_eq!(acl.fingerprint_hex, "ab12cd");
        assert_eq!(acl.level, AclLevel::Read);
    }

    #[test]
    fn acl_list_reply_decodes_the_admin_level() {
        let mut buffer = vec![0u8; 8 + 212];
        Header::write(&mut buffer, MessageType::AdminAclListReply, 0, (4 + 212) as u16);
        buffer[4] = 1;
        buffer[8 + 209] = 1;
        buffer[8 + 210] = 1;
        buffer[8 + 211] = 1;

        let page = decode_acl_list_reply(&buffer).unwrap();
        assert_eq!(page.entries[0].level, AclLevel::ReadWriteAdmin);
    }

    #[test]
    fn configure_request_carries_op_and_settings() {
        let settings = InterfaceSettings {
            fields: SETTING_BITRATE | SETTING_SAMPLE_POINT,
            bitrate: 500_000,
            sample_point: 875,
            ..Default::default()
        };

        let request = encode_configure("truck42", "can0", ConfigureOp::Apply, &settings);

        assert_eq!(request[0], MessageType::Configure as u8);
        assert_eq!(read_u16(&request, 2), 236);
        assert_eq!(read_fixed_str(&request, 4, AGENT_NAME_SIZE), "truck42");
        assert_eq!(read_fixed_str(&request, 132, INTERFACE_NAME_SIZE), "can0");
        assert_eq!(request[148], ConfigureOp::Apply as u8);
        assert_eq!(read_u32(&request, 152), SETTING_BITRATE | SETTING_SAMPLE_POINT);
        assert_eq!(read_u32(&request, 156), 500_000);
        assert_eq!(read_u16(&request, 164), 875);
    }

    #[test]
    fn configure_reply_decodes_settings_and_detail() {
        let mut buffer = vec![0u8; 160];
        let reported = InterfaceSettings {
            fields: SETTING_LINK | SETTING_STATE | SETTING_BIT_TIMING | SETTING_TERMINATION,
            link_up: true,
            can_state: 3,
            termination: 120,
            bit_timing: BitTiming { tq: 125, prop_seg: 6, phase_seg1: 7, phase_seg2: 2, sjw: 1, brp: 5 },
            ..Default::default()
        };
        Header::write(&mut buffer, MessageType::ConfigureReply, 0, 156);
        buffer[4] = 3;
        reported.write(&mut buffer, 8);
        write_fixed_str(&mut buffer, 96, CONFIGURE_DETAIL_SIZE, "Device or resource busy");

        let reply = decode_configure_reply(&buffer).unwrap();

        assert_eq!(reply.status, 3);
        assert_eq!(reply.settings, reported);
        assert_eq!(reply.detail, "Device or resource busy");
    }

    #[test]
    fn status_byte_reply_decodes() {
        let mut buffer = [0u8; 8];
        Header::write(&mut buffer, MessageType::AdminKickReply, 0, 4);
        buffer[4] = 1; // unknown agent
        assert_eq!(decode_status_byte(&buffer, MessageType::AdminKickReply), Ok(1));
    }
}
