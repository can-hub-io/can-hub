//! Admin-plane client: the request/response logic the daemon runs against the
//! hub unix socket.
//!
//! Written over `std::io::{Read, Write}` so it is transport-agnostic and unit
//! testable with an in-memory duplex; a real `std::os::unix::net::UnixStream`
//! satisfies both traits directly. The async/axum integration wraps this in a
//! later phase.

use std::io::{self, Read, Write};

use crate::protocol::admin::{
    self, AclEntry, AgentEntry, ClientEntry, InterfaceEntry, Page, PeerEntry, Status,
};
use crate::protocol::{DecodeError, Header, Role, HEADER_SIZE};

/// Pagination guard: replies cap at 16 entries, so this many pages covers any
/// plausible catalogue while bounding a misbehaving peer that always sets the
/// `more` flag.
const MAX_PAGES: u16 = 4096;

#[derive(Debug)]
pub enum AdminClientError {
    Io(io::Error),
    Decode(DecodeError),
    /// The peer set the `more` flag indefinitely.
    TooManyPages,
}

impl From<io::Error> for AdminClientError {
    fn from(error: io::Error) -> Self {
        AdminClientError::Io(error)
    }
}

impl From<DecodeError> for AdminClientError {
    fn from(error: DecodeError) -> Self {
        AdminClientError::Decode(error)
    }
}

type Result<T> = std::result::Result<T, AdminClientError>;

/// An admin session over a framed byte transport.
pub struct AdminClient<T: Read + Write> {
    transport: T,
}

impl<T: Read + Write> AdminClient<T> {
    /// Wrap a connected transport and send the admin HELLO. The hub accepts
    /// the admin role only on local transports; the caller is responsible for
    /// having connected over the unix socket.
    pub fn connect(transport: T) -> Result<Self> {
        let mut client = AdminClient { transport };
        let hello = crate::protocol::encode_hello(Role::Admin, 0, 0);
        client.transport.write_all(&hello)?;
        Ok(client)
    }

    pub fn status(&mut self) -> Result<Status> {
        let reply = self.exchange(&admin::encode_status())?;
        Ok(admin::decode_status_reply(&reply)?)
    }

    pub fn peers(&mut self) -> Result<Vec<PeerEntry>> {
        self.fetch_all(|offset| admin::encode_peers(offset).to_vec(), admin::decode_peers_reply)
    }

    pub fn agents(&mut self, filter: &str) -> Result<Vec<AgentEntry>> {
        self.fetch_all(
            |offset| admin::encode_agents(offset, filter).to_vec(),
            admin::decode_agents_reply,
        )
    }

    pub fn clients(&mut self, filter: &str) -> Result<Vec<ClientEntry>> {
        self.fetch_all(
            |offset| admin::encode_clients(offset, filter).to_vec(),
            admin::decode_clients_reply,
        )
    }

    pub fn interfaces(&mut self) -> Result<Vec<InterfaceEntry>> {
        self.fetch_all(
            |offset| admin::encode_interfaces(offset).to_vec(),
            admin::decode_interfaces_reply,
        )
    }

    pub fn acl_list(&mut self) -> Result<Vec<AclEntry>> {
        self.fetch_all(|offset| admin::encode_acl_list(offset).to_vec(), admin::decode_acl_list_reply)
    }

    /// Send a pre-encoded request and read one framed reply.
    pub fn exchange(&mut self, request: &[u8]) -> Result<Vec<u8>> {
        self.transport.write_all(request)?;
        self.read_message()
    }

    /// Walk every page of a paginated view, advancing `offset` by the number
    /// of entries each reply carried, until the `more` flag clears.
    fn fetch_all<E, R, V>(&mut self, encode: E, decode: R) -> Result<Vec<V>>
    where
        E: Fn(u16) -> Vec<u8>,
        R: Fn(&[u8]) -> std::result::Result<Page<V>, DecodeError>,
    {
        let mut collected = Vec::new();
        let mut offset: u16 = 0;
        for _ in 0..MAX_PAGES {
            let reply = self.exchange(&encode(offset))?;
            let page = decode(&reply)?;
            let count = page.entries.len() as u16;
            collected.extend(page.entries);
            if !page.more || count == 0 {
                return Ok(collected);
            }
            offset = offset.saturating_add(count);
        }
        Err(AdminClientError::TooManyPages)
    }

    /// Read one full message: the 4-byte header then `length` payload bytes.
    fn read_message(&mut self) -> Result<Vec<u8>> {
        let mut header = [0u8; HEADER_SIZE];
        self.transport.read_exact(&mut header)?;
        let length = Header::parse(&header)?.length as usize;
        let mut message = vec![0u8; HEADER_SIZE + length];
        message[..HEADER_SIZE].copy_from_slice(&header);
        self.transport.read_exact(&mut message[HEADER_SIZE..])?;
        Ok(message)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::protocol::admin::AclLevel;
    use crate::protocol::{write_fixed_str, MessageType};
    use std::collections::VecDeque;

    /// In-memory transport: reads drain `inbound`, writes append to `outbound`.
    struct MockTransport {
        inbound: VecDeque<u8>,
        outbound: Vec<u8>,
    }

    impl MockTransport {
        fn with_replies(replies: &[Vec<u8>]) -> Self {
            let mut inbound = VecDeque::new();
            for reply in replies {
                inbound.extend(reply.iter().copied());
            }
            MockTransport { inbound, outbound: Vec::new() }
        }
    }

    impl Read for MockTransport {
        fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
            let n = out.len().min(self.inbound.len());
            for slot in out.iter_mut().take(n) {
                *slot = self.inbound.pop_front().unwrap();
            }
            Ok(n)
        }
    }

    impl Write for MockTransport {
        fn write(&mut self, data: &[u8]) -> io::Result<usize> {
            self.outbound.extend_from_slice(data);
            Ok(data.len())
        }
        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    fn status_reply(peer_count: u16) -> Vec<u8> {
        let mut buffer = vec![0u8; 48];
        Header::write(&mut buffer, MessageType::AdminStatusReply, 0, 44);
        buffer[4..6].copy_from_slice(&peer_count.to_le_bytes());
        buffer
    }

    fn interfaces_page(ids: &[u32], more: bool) -> Vec<u8> {
        let count = ids.len();
        let mut buffer = vec![0u8; 8 + count * 160];
        Header::write(
            &mut buffer,
            MessageType::AdminInterfacesReply,
            0,
            (4 + count * 160) as u16,
        );
        buffer[4] = count as u8;
        buffer[5] = if more { 0x01 } else { 0x00 };
        for (index, &id) in ids.iter().enumerate() {
            let base = 8 + index * 160;
            buffer[base..base + 4].copy_from_slice(&id.to_le_bytes());
            write_fixed_str(&mut buffer, base + 16, 128, "truck42");
            write_fixed_str(&mut buffer, base + 144, 16, "can0");
        }
        buffer
    }

    #[test]
    fn connect_sends_admin_hello() {
        let transport = MockTransport::with_replies(&[]);
        let client = AdminClient::connect(transport).unwrap();
        let hello = &client.transport.outbound;
        assert_eq!(hello.len(), 12);
        assert_eq!(hello[0], MessageType::Hello as u8);
        assert_eq!(hello[5], Role::Admin as u8);
    }

    #[test]
    fn status_round_trips_over_transport() {
        let transport = MockTransport::with_replies(&[status_reply(7)]);
        let mut client = AdminClient::connect(transport).unwrap();
        let status = client.status().unwrap();
        assert_eq!(status.peer_count, 7);
        // HELLO (12) + ADMIN_STATUS request (4) were written.
        assert_eq!(client.transport.outbound.len(), 12 + 4);
        assert_eq!(client.transport.outbound[12], MessageType::AdminStatus as u8);
    }

    #[test]
    fn interfaces_aggregates_pages_until_more_clears() {
        let replies = vec![
            interfaces_page(&[1, 2], true),
            interfaces_page(&[3], false),
        ];
        let transport = MockTransport::with_replies(&replies);
        let mut client = AdminClient::connect(transport).unwrap();
        let interfaces = client.interfaces().unwrap();
        assert_eq!(interfaces.len(), 3);
        assert_eq!(interfaces[0].interface_id, 1);
        assert_eq!(interfaces[2].interface_id, 3);
        // Second request must carry offset = 2 (entries seen so far).
        let second_request = &client.transport.outbound[12 + 8..12 + 16];
        assert_eq!(u16::from_le_bytes([second_request[4], second_request[5]]), 2);
    }

    #[test]
    fn empty_more_page_terminates() {
        // A peer that sets `more` but returns zero entries must not loop.
        let transport = MockTransport::with_replies(&[interfaces_page(&[], true)]);
        let mut client = AdminClient::connect(transport).unwrap();
        assert_eq!(client.interfaces().unwrap().len(), 0);
    }

    #[test]
    fn acl_list_decodes_levels() {
        let mut reply = vec![0u8; 8 + 212];
        Header::write(&mut reply, MessageType::AdminAclListReply, 0, (4 + 212) as u16);
        reply[4] = 1;
        write_fixed_str(&mut reply, 8, 128, "truck42");
        write_fixed_str(&mut reply, 8 + 128, 16, "can0");
        write_fixed_str(&mut reply, 8 + 144, 65, "ab12cd");
        reply[8 + 209] = 1;
        reply[8 + 210] = 1;
        let transport = MockTransport::with_replies(&[reply]);
        let mut client = AdminClient::connect(transport).unwrap();
        let acls = client.acl_list().unwrap();
        assert_eq!(acls[0].level, AclLevel::ReadWrite);
    }
}
