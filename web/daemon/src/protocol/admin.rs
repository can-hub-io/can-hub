//! Admin message family (0x10..=0x2B): request encoders and reply decoders.
//!
//! The layouts mirror the authoritative C codec in
//! `src/protocol/admin_message.c`, including the pin/ACL mutations
//! (0x22, 0x24..=0x29).

use super::{
    read_fixed_str, read_u16, read_u32, read_u64, write_fixed_str, DecodeError, Header,
    MessageType, HEADER_SIZE,
};

const AGENT_NAME_SIZE: usize = 128;
const INTERFACE_NAME_SIZE: usize = 16;
const FINGERPRINT_SIZE: usize = 65;
const ORIGIN_SIZE: usize = 56;

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
    pub agent_name: String,
    pub interface_name: String,
}

/// ACL permission level on the wire as a `can_read`/`can_write` byte pair.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AclLevel {
    None,
    ReadOnly,
    ReadWrite,
}

impl AclLevel {
    pub fn to_bytes(self) -> (u8, u8) {
        match self {
            AclLevel::None => (0, 0),
            AclLevel::ReadOnly => (1, 0),
            AclLevel::ReadWrite => (1, 1),
        }
    }

    pub fn from_bytes(can_read: u8, can_write: u8) -> AclLevel {
        match (can_read != 0, can_write != 0) {
            (_, true) => AclLevel::ReadWrite,
            (true, false) => AclLevel::ReadOnly,
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

/// Interface reconfiguration op (ADMIN_IFCONFIG @148).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum IfconfigOp {
    SetBitrate = 0,
    LinkUp = 1,
    LinkDown = 2,
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

/// ADMIN_IFCONFIG (total 156): reconfigure one interface by namespaced pair.
/// `bitrate` is only read for `IfconfigOp::SetBitrate`.
pub fn encode_ifconfig(
    agent_name: &str,
    interface_name: &str,
    op: IfconfigOp,
    bitrate: u32,
) -> [u8; 156] {
    let mut out = [0u8; 156];
    Header::write(&mut out, MessageType::AdminIfconfig, 0, 152);
    write_fixed_str(&mut out, 4, AGENT_NAME_SIZE, agent_name);
    write_fixed_str(&mut out, 132, INTERFACE_NAME_SIZE, interface_name);
    out[148] = op as u8;
    out[152..156].copy_from_slice(&bitrate.to_le_bytes());
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
/// can_write @214, 1 reserved.
pub fn encode_acl_set(
    fingerprint_hex: &str,
    agent_name: &str,
    interface_name: &str,
    level: AclLevel,
) -> [u8; 216] {
    let mut out = encode_acl_target(MessageType::AdminAclSet, agent_name, interface_name, fingerprint_hex);
    let (can_read, can_write) = level.to_bytes();
    out[213] = can_read;
    out[214] = can_write;
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

/// ADMIN_INTERFACES_REPLY (8 + count * 160).
pub fn decode_interfaces_reply(buffer: &[u8]) -> Result<Page<InterfaceEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminInterfacesReply, 160, |entry| InterfaceEntry {
        interface_id: read_u32(entry, 0),
        subscriber_count: entry[4],
        frames_received: read_u64(entry, 8),
        agent_name: read_fixed_str(entry, 16, AGENT_NAME_SIZE),
        interface_name: read_fixed_str(entry, 144, INTERFACE_NAME_SIZE),
    })
}

/// ADMIN_ACL_LIST_REPLY (8 + count * 212). Entry layout (entry-relative):
/// agent_name[128] @0, interface_name[16] @128, fingerprint_hex[65] @144,
/// can_read @209, can_write @210.
pub fn decode_acl_list_reply(buffer: &[u8]) -> Result<Page<AclEntry>, DecodeError> {
    decode_page(buffer, MessageType::AdminAclListReply, 212, |entry| AclEntry {
        agent_name: read_fixed_str(entry, 0, AGENT_NAME_SIZE),
        interface_name: read_fixed_str(entry, 128, INTERFACE_NAME_SIZE),
        fingerprint_hex: read_fixed_str(entry, 144, FINGERPRINT_SIZE),
        level: AclLevel::from_bytes(entry[209], entry[210]),
    })
}

/// Status-only reply (KICK/FORGET/KICK_PEER/IFCONFIG/PIN_ADD/ACL_SET/
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
    fn ifconfig_request_layout() {
        let request = encode_ifconfig("truck42", "can0", IfconfigOp::SetBitrate, 500_000);
        assert_eq!(request.len(), 156);
        assert_eq!(read_fixed_str(&request, 4, AGENT_NAME_SIZE), "truck42");
        assert_eq!(read_fixed_str(&request, 132, INTERFACE_NAME_SIZE), "can0");
        assert_eq!(request[148], IfconfigOp::SetBitrate as u8);
        assert_eq!(read_u32(&request, 152), 500_000);
    }

    #[test]
    fn status_reply_decodes() {
        let mut buffer = [0u8; 48];
        Header::write(&mut buffer, MessageType::AdminStatusReply, 0, 44);
        buffer[4..6].copy_from_slice(&3u16.to_le_bytes()); // peer_count
        buffer[6..8].copy_from_slice(&1u16.to_le_bytes()); // agent_count
        buffer[8..10].copy_from_slice(&2u16.to_le_bytes()); // client_count
        buffer[10..12].copy_from_slice(&4u16.to_le_bytes()); // interface_count
        buffer[16..24].copy_from_slice(&1000u64.to_le_bytes());
        buffer[24..32].copy_from_slice(&900u64.to_le_bytes());
        buffer[32..40].copy_from_slice(&50u64.to_le_bytes());
        buffer[40..48].copy_from_slice(&10u64.to_le_bytes());

        let status = decode_status_reply(&buffer).unwrap();
        assert_eq!(status.peer_count, 3);
        assert_eq!(status.interface_count, 4);
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
        let mut buffer = vec![0u8; 8 + count * 160];
        Header::write(&mut buffer, MessageType::AdminInterfacesReply, 0, (4 + count * 160) as u16);
        buffer[4] = count as u8;
        buffer[5] = 0x01; // more
        for index in 0..count {
            let base = 8 + index * 160;
            let id = (index as u32) + 1;
            buffer[base..base + 4].copy_from_slice(&id.to_le_bytes());
            buffer[base + 4] = (index as u8) + 1; // subscriber_count
            buffer[base + 8..base + 16].copy_from_slice(&((index as u64 + 1) * 100).to_le_bytes());
            write_fixed_str(&mut buffer, base + 16, AGENT_NAME_SIZE, "truck42");
            write_fixed_str(&mut buffer, base + 144, INTERFACE_NAME_SIZE, "can0");
        }

        let page = decode_interfaces_reply(&buffer).unwrap();
        assert!(page.more);
        assert_eq!(page.entries.len(), 2);
        assert_eq!(page.entries[0].interface_id, 1);
        assert_eq!(page.entries[0].subscriber_count, 1);
        assert_eq!(page.entries[1].frames_received, 200);
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
    fn acl_level_byte_pairs() {
        assert_eq!(AclLevel::None.to_bytes(), (0, 0));
        assert_eq!(AclLevel::ReadOnly.to_bytes(), (1, 0));
        assert_eq!(AclLevel::ReadWrite.to_bytes(), (1, 1));
        assert_eq!(AclLevel::from_bytes(0, 0), AclLevel::None);
        assert_eq!(AclLevel::from_bytes(1, 0), AclLevel::ReadOnly);
        assert_eq!(AclLevel::from_bytes(1, 1), AclLevel::ReadWrite);
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
        assert_eq!(acl.level, AclLevel::ReadOnly);
    }

    #[test]
    fn status_byte_reply_decodes() {
        let mut buffer = [0u8; 8];
        Header::write(&mut buffer, MessageType::AdminKickReply, 0, 4);
        buffer[4] = 1; // unknown agent
        assert_eq!(decode_status_byte(&buffer, MessageType::AdminKickReply), Ok(1));
    }
}
