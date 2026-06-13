//! can-hub wire protocol — the subset the web daemon needs.
//!
//! Layouts mirror `doc/protocol.md` exactly: little-endian, fixed offsets,
//! naturally aligned, explicit reserved padding zeroed by the sender. Strings
//! are fixed-size NUL-terminated, NUL-padded char arrays. Decode is a bounds
//! check plus an overlay read; there are no parsing loops.
//!
//! The daemon connects as an admin client, so it *encodes* requests
//! (HELLO + the admin family) and *decodes* replies. Both directions are
//! implemented where useful, but the reply decoders carry the load.

pub mod admin;

/// Fixed header on every message, both planes.
pub const HEADER_SIZE: usize = 4;

/// Message types used by the admin client. Data-plane FRAME (0x40) is not
/// handled here — the panel never touches the data plane.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum MessageType {
    Hello = 0x01,
    Error = 0x09,
    AdminStatus = 0x10,
    AdminStatusReply = 0x11,
    AdminPeers = 0x12,
    AdminPeersReply = 0x13,
    AdminKick = 0x14,
    AdminKickReply = 0x15,
    AdminPins = 0x16,
    AdminPinsReply = 0x17,
    AdminForget = 0x18,
    AdminForgetReply = 0x19,
    AdminKickPeer = 0x1A,
    AdminKickPeerReply = 0x1B,
    AdminAgents = 0x1C,
    AdminAgentsReply = 0x1D,
    AdminClients = 0x1E,
    AdminClientsReply = 0x1F,
    AdminInterfaces = 0x20,
    AdminInterfacesReply = 0x21,
    AdminPinAdd = 0x22,
    AdminPinAddReply = 0x23,
    AdminAclSet = 0x24,
    AdminAclSetReply = 0x25,
    AdminAclRevoke = 0x26,
    AdminAclRevokeReply = 0x27,
    AdminAclList = 0x28,
    AdminAclListReply = 0x29,
    AdminIfconfig = 0x2A,
    AdminIfconfigReply = 0x2B,
}

impl MessageType {
    pub fn from_u8(value: u8) -> Option<Self> {
        use MessageType::*;
        let kind = match value {
            0x01 => Hello,
            0x09 => Error,
            0x10 => AdminStatus,
            0x11 => AdminStatusReply,
            0x12 => AdminPeers,
            0x13 => AdminPeersReply,
            0x14 => AdminKick,
            0x15 => AdminKickReply,
            0x16 => AdminPins,
            0x17 => AdminPinsReply,
            0x18 => AdminForget,
            0x19 => AdminForgetReply,
            0x1A => AdminKickPeer,
            0x1B => AdminKickPeerReply,
            0x1C => AdminAgents,
            0x1D => AdminAgentsReply,
            0x1E => AdminClients,
            0x1F => AdminClientsReply,
            0x20 => AdminInterfaces,
            0x21 => AdminInterfacesReply,
            0x22 => AdminPinAdd,
            0x23 => AdminPinAddReply,
            0x24 => AdminAclSet,
            0x25 => AdminAclSetReply,
            0x26 => AdminAclRevoke,
            0x27 => AdminAclRevokeReply,
            0x28 => AdminAclList,
            0x29 => AdminAclListReply,
            0x2A => AdminIfconfig,
            0x2B => AdminIfconfigReply,
            _ => return None,
        };
        Some(kind)
    }
}

/// HELLO role field (1 agent, 2 client, 3 admin).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Role {
    Agent = 1,
    Client = 2,
    Admin = 3,
}

/// Errors a decode can produce. Malformed input never panics.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DecodeError {
    /// Buffer shorter than the fixed layout requires.
    TooShort { need: usize, got: usize },
    /// Header type byte is not the message the caller expected.
    UnexpectedType { expected: u8, got: u8 },
    /// Header length field disagrees with the buffer.
    LengthMismatch { header: usize, buffer: usize },
    /// A count/length field would read past the buffer.
    TruncatedEntries,
}

/// Parsed message header.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Header {
    pub message_type: u8,
    pub flags: u8,
    pub length: u16,
}

impl Header {
    /// Write a header into the first four bytes of `out`. `payload_len` is the
    /// byte count after the header.
    pub fn write(out: &mut [u8], message_type: MessageType, flags: u8, payload_len: u16) {
        out[0] = message_type as u8;
        out[1] = flags;
        out[2..4].copy_from_slice(&payload_len.to_le_bytes());
    }

    pub fn parse(buffer: &[u8]) -> Result<Header, DecodeError> {
        if buffer.len() < HEADER_SIZE {
            return Err(DecodeError::TooShort { need: HEADER_SIZE, got: buffer.len() });
        }
        Ok(Header {
            message_type: buffer[0],
            flags: buffer[1],
            length: u16::from_le_bytes([buffer[2], buffer[3]]),
        })
    }
}

/// Encode a HELLO (total 76 bytes). Capabilities default to 0 in version 0.
/// The trailing name[64] is left empty; admin sessions carry no peer name.
pub fn encode_hello(role: Role, version: u8, capabilities: u32) -> [u8; 76] {
    let mut out = [0u8; 76];
    Header::write(&mut out, MessageType::Hello, 0, 72);
    out[4] = version;
    out[5] = role as u8;
    // @6 reserved u16 stays zero.
    out[8..12].copy_from_slice(&capabilities.to_le_bytes());
    // @12 name[64] stays zero.
    out
}

// --- little-endian field readers; all bounds-checked by the caller's guard ---

pub(crate) fn read_u16(buffer: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([buffer[offset], buffer[offset + 1]])
}

pub(crate) fn read_u32(buffer: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([
        buffer[offset],
        buffer[offset + 1],
        buffer[offset + 2],
        buffer[offset + 3],
    ])
}

pub(crate) fn read_u64(buffer: &[u8], offset: usize) -> u64 {
    let mut bytes = [0u8; 8];
    bytes.copy_from_slice(&buffer[offset..offset + 8]);
    u64::from_le_bytes(bytes)
}

/// Read a fixed-size NUL-terminated, NUL-padded char array as a String,
/// stopping at the first NUL. Non-UTF-8 bytes are replaced (the protocol
/// fields are ASCII names and hex fingerprints).
pub(crate) fn read_fixed_str(buffer: &[u8], offset: usize, size: usize) -> String {
    let slice = &buffer[offset..offset + size];
    let end = slice.iter().position(|&b| b == 0).unwrap_or(size);
    String::from_utf8_lossy(&slice[..end]).into_owned()
}

/// Write a String into a fixed-size NUL-padded char array, truncating to
/// `size - 1` so the result stays NUL-terminated.
pub(crate) fn write_fixed_str(out: &mut [u8], offset: usize, size: usize, value: &str) {
    let field = &mut out[offset..offset + size];
    field.fill(0);
    let bytes = value.as_bytes();
    let copy = bytes.len().min(size - 1);
    field[..copy].copy_from_slice(&bytes[..copy]);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn header_round_trips() {
        let mut buffer = [0u8; 4];
        Header::write(&mut buffer, MessageType::AdminStatus, 0, 0);
        let header = Header::parse(&buffer).unwrap();
        assert_eq!(header.message_type, 0x10);
        assert_eq!(header.flags, 0);
        assert_eq!(header.length, 0);
    }

    #[test]
    fn header_parse_rejects_short_buffer() {
        assert_eq!(
            Header::parse(&[0x10, 0x00]),
            Err(DecodeError::TooShort { need: 4, got: 2 })
        );
    }

    #[test]
    fn hello_admin_layout() {
        let hello = encode_hello(Role::Admin, 0, 0);
        assert_eq!(hello.len(), 76);
        assert_eq!(hello[0], MessageType::Hello as u8);
        assert_eq!(u16::from_le_bytes([hello[2], hello[3]]), 72);
        assert_eq!(hello[4], 0); // version
        assert_eq!(hello[5], Role::Admin as u8);
        assert_eq!(read_u32(&hello, 8), 0); // capabilities
    }

    #[test]
    fn message_type_is_total_round_trip() {
        for raw in [0x01u8, 0x10, 0x13, 0x21, 0x2B] {
            let kind = MessageType::from_u8(raw).unwrap();
            assert_eq!(kind as u8, raw);
        }
        assert_eq!(MessageType::from_u8(0x99), None);
    }

    #[test]
    fn fixed_str_reads_up_to_nul() {
        let mut buffer = [0u8; 16];
        write_fixed_str(&mut buffer, 0, 16, "can0");
        assert_eq!(read_fixed_str(&buffer, 0, 16), "can0");
        assert_eq!(buffer[4], 0);
    }

    #[test]
    fn fixed_str_truncates_and_stays_terminated() {
        let mut buffer = [0xFFu8; 4];
        write_fixed_str(&mut buffer, 0, 4, "toolong");
        assert_eq!(buffer[3], 0);
        assert_eq!(read_fixed_str(&buffer, 0, 4), "too");
    }
}
