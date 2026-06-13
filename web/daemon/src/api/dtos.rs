//! Request bodies and the camelCase response DTOs the React UI consumes,
//! decoupled from the wire layout of the protocol structs.

use serde::{Deserialize, Serialize};

use crate::protocol::admin::{
    AclEntry, AclLevel, AgentEntry, ClientEntry, InterfaceEntry, PeerEntry, PinEntry, Status,
};

#[derive(Serialize)]
pub(crate) struct ActionResult {
    pub ok: bool,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct PinAddBody {
    pub agent_name: String,
    pub fingerprint_hex: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct AclBody {
    pub fingerprint_hex: String,
    pub agent_name: String,
    pub interface_name: String,
    /// "none" | "ro" | "rw"; ignored by revoke.
    #[serde(default)]
    pub level: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct InterfaceConfigBody {
    pub agent_name: String,
    pub interface_name: String,
    /// "bitrate" | "up" | "down".
    pub op: String,
    #[serde(default)]
    pub bitrate: u32,
}

#[derive(Deserialize)]
pub(crate) struct Credentials {
    pub name: String,
    pub password: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct AuthStateDto {
    pub needs_bootstrap: bool,
    pub authenticated: bool,
    pub user: Option<String>,
    pub permissions: Vec<String>,
    /// Token the client echoes in the X-CSRF-Token header on mutating requests.
    pub csrf_token: Option<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct StatusDto {
    pub peer_count: u16,
    pub agent_count: u16,
    pub client_count: u16,
    pub interface_count: u16,
    pub frames_received: u64,
    pub frames_forwarded: u64,
    pub frames_dropped: u64,
    pub frames_unroutable: u64,
}

impl From<Status> for StatusDto {
    fn from(status: Status) -> Self {
        StatusDto {
            peer_count: status.peer_count,
            agent_count: status.agent_count,
            client_count: status.client_count,
            interface_count: status.interface_count,
            frames_received: status.frames_received,
            frames_forwarded: status.frames_forwarded,
            frames_dropped: status.frames_dropped,
            frames_unroutable: status.frames_unroutable,
        }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct PeerDto {
    pub peer_id: u32,
    pub role: &'static str,
    pub transport: &'static str,
    pub agent_name: String,
    pub origin: String,
    pub fingerprint_hex: String,
    pub uptime_seconds: u32,
    pub frames_forwarded: u32,
    pub frames_dropped: u32,
}

impl From<PeerEntry> for PeerDto {
    fn from(entry: PeerEntry) -> Self {
        PeerDto {
            peer_id: entry.peer_id,
            role: role_name(entry.role),
            transport: transport_name(entry.transport_kind),
            agent_name: entry.agent_name,
            origin: entry.origin,
            fingerprint_hex: entry.fingerprint_hex,
            uptime_seconds: entry.uptime_seconds,
            frames_forwarded: entry.frames_forwarded,
            frames_dropped: entry.frames_dropped,
        }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct AgentDto {
    pub peer_id: u32,
    pub interface_count: u8,
    pub agent_name: String,
    pub fingerprint_hex: String,
}

impl From<AgentEntry> for AgentDto {
    fn from(entry: AgentEntry) -> Self {
        AgentDto {
            peer_id: entry.peer_id,
            interface_count: entry.interface_count,
            agent_name: entry.agent_name,
            fingerprint_hex: entry.fingerprint_hex,
        }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct ClientDto {
    pub peer_id: u32,
    pub interface_id: u32,
    pub channel: Option<u8>,
    pub agent_name: String,
    pub interface_name: String,
    pub frames_forwarded: u32,
    pub frames_dropped: u32,
}

impl From<ClientEntry> for ClientDto {
    fn from(entry: ClientEntry) -> Self {
        ClientDto {
            peer_id: entry.peer_id,
            interface_id: entry.interface_id,
            channel: if entry.channel == 0xFF { None } else { Some(entry.channel) },
            agent_name: entry.agent_name,
            interface_name: entry.interface_name,
            frames_forwarded: entry.frames_forwarded,
            frames_dropped: entry.frames_dropped,
        }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct InterfaceDto {
    pub interface_id: u32,
    pub subscriber_count: u8,
    pub frames_received: u64,
    pub agent_name: String,
    pub interface_name: String,
}

impl From<InterfaceEntry> for InterfaceDto {
    fn from(entry: InterfaceEntry) -> Self {
        InterfaceDto {
            interface_id: entry.interface_id,
            subscriber_count: entry.subscriber_count,
            frames_received: entry.frames_received,
            agent_name: entry.agent_name,
            interface_name: entry.interface_name,
        }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct AclDto {
    pub fingerprint_hex: String,
    pub agent_name: String,
    pub interface_name: String,
    pub level: &'static str,
}

impl From<AclEntry> for AclDto {
    fn from(entry: AclEntry) -> Self {
        AclDto {
            fingerprint_hex: entry.fingerprint_hex,
            agent_name: entry.agent_name,
            interface_name: entry.interface_name,
            level: acl_level_name(entry.level),
        }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct PinDto {
    pub agent_name: String,
    pub fingerprint_hex: String,
}

impl From<PinEntry> for PinDto {
    fn from(entry: PinEntry) -> Self {
        PinDto { agent_name: entry.agent_name, fingerprint_hex: entry.fingerprint_hex }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct UserDto {
    pub id: i64,
    pub name: String,
    pub enabled: bool,
    pub group_ids: Vec<i64>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct GroupDto {
    pub id: i64,
    pub name: String,
    pub permissions: Vec<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct CreateUserBody {
    pub name: String,
    pub password: String,
    #[serde(default = "default_true")]
    pub enabled: bool,
}

fn default_true() -> bool {
    true
}

#[derive(Deserialize)]
pub(crate) struct EnabledBody {
    pub enabled: bool,
}

#[derive(Deserialize)]
pub(crate) struct NameBody {
    pub name: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct MembershipBody {
    pub group_id: i64,
}

#[derive(Deserialize)]
pub(crate) struct PermissionsBody {
    pub permissions: Vec<String>,
}

#[derive(Deserialize)]
pub(crate) struct PasswordBody {
    pub password: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct SelfPasswordBody {
    pub current_password: String,
    pub new_password: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct AuditDto {
    pub at: i64,
    pub actor: String,
    pub action: String,
    pub target: String,
    pub status: u16,
}

fn role_name(role: u8) -> &'static str {
    match role {
        1 => "agent",
        2 => "client",
        3 => "admin",
        _ => "unknown",
    }
}

fn transport_name(transport_kind: u8) -> &'static str {
    match transport_kind {
        1 => "unix",
        2 => "tcp",
        3 => "tls",
        4 => "quic",
        _ => "unknown",
    }
}

fn acl_level_name(level: AclLevel) -> &'static str {
    match level {
        AclLevel::None => "none",
        AclLevel::ReadOnly => "ro",
        AclLevel::ReadWrite => "rw",
    }
}
