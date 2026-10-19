//! Request bodies and the camelCase response DTOs the React UI consumes,
//! decoupled from the wire layout of the protocol structs.

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

use crate::protocol::admin::{
    AclEntry, AclLevel, AgentEntry, BridgeEndpoint, BridgeEntry, CanFilter, GroupEntry, BitTiming, ClientEntry, InterfaceEntry, InterfaceSettings, PeerEntry, PinEntry,
    RejectedEntry, RejectionReason, Status, CTRLMODE_BERR_REPORTING, CTRLMODE_FD, CTRLMODE_LISTEN_ONLY,
    CTRLMODE_LOOPBACK, CTRLMODE_ONE_SHOT, CTRLMODE_TRIPLE_SAMPLING, SETTING_BITRATE, SETTING_CTRLMODE,
    SETTING_DATA_BITRATE, SETTING_DATA_SAMPLE_POINT, SETTING_LINK, SETTING_RESTART_MS, SETTING_SAMPLE_POINT,
    SETTING_BIT_TIMING, SETTING_DATA_BIT_TIMING, SETTING_STATE, SETTING_TERMINATION, SETTING_TXQUEUELEN,
};

/// Wire sample points are tenths of a percent; the API speaks fractions.
const SAMPLE_POINT_SCALE: f64 = 1000.0;

/// The control modes by their API name, in display order.
const MODES: [(&str, u32); 6] = [
    ("fd", CTRLMODE_FD),
    ("listenOnly", CTRLMODE_LISTEN_ONLY),
    ("loopback", CTRLMODE_LOOPBACK),
    ("oneShot", CTRLMODE_ONE_SHOT),
    ("tripleSampling", CTRLMODE_TRIPLE_SAMPLING),
    ("berrReporting", CTRLMODE_BERR_REPORTING),
];

const STATES: [&str; 6] = ["error-active", "error-warning", "error-passive", "bus-off", "stopped", "sleeping"];

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
    /// "none" | "r" | "rw" | "rwa"; ignored by revoke.
    #[serde(default)]
    pub level: String,
}

#[derive(Deserialize, Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeEndpointDto {
    pub agent_name: String,
    pub interface_name: String,
}

#[derive(Deserialize, Serialize, Clone, Copy)]
#[serde(rename_all = "camelCase")]
pub(crate) struct CanFilterDto {
    pub can_id: u32,
    pub can_mask: u32,
}

/// A bus-to-bus route; no filters bridges every frame.
#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeBody {
    pub source: BridgeEndpointDto,
    pub destination: BridgeEndpointDto,
    #[serde(default)]
    pub filters: Vec<CanFilterDto>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeDeleteBody {
    pub source: BridgeEndpointDto,
    pub destination: BridgeEndpointDto,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeDto {
    pub source: BridgeEndpointDto,
    pub destination: BridgeEndpointDto,
    pub filters: Vec<CanFilterDto>,
    pub source_online: bool,
    pub destination_online: bool,
    pub forwarded: u64,
    pub dropped: u64,
}

impl From<BridgeEndpointDto> for BridgeEndpoint {
    fn from(endpoint: BridgeEndpointDto) -> Self {
        BridgeEndpoint { agent_name: endpoint.agent_name, interface_name: endpoint.interface_name }
    }
}

impl From<BridgeEndpoint> for BridgeEndpointDto {
    fn from(endpoint: BridgeEndpoint) -> Self {
        BridgeEndpointDto { agent_name: endpoint.agent_name, interface_name: endpoint.interface_name }
    }
}

impl From<CanFilterDto> for CanFilter {
    fn from(filter: CanFilterDto) -> Self {
        CanFilter { can_id: filter.can_id, can_mask: filter.can_mask }
    }
}

impl From<BridgeEntry> for BridgeDto {
    fn from(entry: BridgeEntry) -> Self {
        BridgeDto {
            source: entry.source.into(),
            destination: entry.destination.into(),
            filters: entry
                .filters
                .into_iter()
                .map(|filter| CanFilterDto { can_id: filter.can_id, can_mask: filter.can_mask })
                .collect(),
            source_online: entry.source_online,
            destination_online: entry.destination_online,
            forwarded: entry.forwarded,
            dropped: entry.dropped,
        }
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeGroupBody {
    pub name: String,
    #[serde(default)]
    pub filters: Vec<CanFilterDto>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeGroupNameBody {
    pub name: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeGroupMemberBody {
    pub name: String,
    pub member: BridgeEndpointDto,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeGroupMemberDto {
    pub agent_name: String,
    pub interface_name: String,
    pub online: bool,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct BridgeGroupDto {
    pub name: String,
    pub filters: Vec<CanFilterDto>,
    pub members: Vec<BridgeGroupMemberDto>,
    pub forwarded: u64,
    pub dropped: u64,
}

impl From<GroupEntry> for BridgeGroupDto {
    fn from(entry: GroupEntry) -> Self {
        BridgeGroupDto {
            name: entry.name,
            filters: entry
                .filters
                .into_iter()
                .map(|filter| CanFilterDto { can_id: filter.can_id, can_mask: filter.can_mask })
                .collect(),
            members: entry
                .members
                .into_iter()
                .map(|member| BridgeGroupMemberDto {
                    agent_name: member.endpoint.agent_name,
                    interface_name: member.endpoint.interface_name,
                    online: member.online,
                })
                .collect(),
            forwarded: entry.forwarded,
            dropped: entry.dropped,
        }
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct InterfaceConfigBody {
    pub agent_name: String,
    pub interface_name: String,
    pub settings: InterfaceSettingsBody,
}

/// What to change on an interface, all applied in one step. Absent keys are
/// left as they are; sample points are fractions (0.875).
#[derive(Deserialize, Default)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub(crate) struct InterfaceSettingsBody {
    pub bitrate: Option<u32>,
    pub sample_point: Option<f64>,
    pub data_bitrate: Option<u32>,
    pub data_sample_point: Option<f64>,
    pub restart_ms: Option<u32>,
    pub txqueuelen: Option<u32>,
    pub termination: Option<u16>,
    pub bit_timing: Option<BitTimingDto>,
    pub data_bit_timing: Option<BitTimingDto>,
    pub up: Option<bool>,
    #[serde(default)]
    pub modes: BTreeMap<String, bool>,
}

/// Raw bit timing: time quantum in ns, segments in quanta. In a request each
/// member is optional; a reply carries them all.
#[derive(Serialize, Deserialize, Default, Debug, PartialEq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub(crate) struct BitTimingDto {
    pub tq: Option<u32>,
    pub prop_seg: Option<u32>,
    pub phase_seg1: Option<u32>,
    pub phase_seg2: Option<u32>,
    pub sjw: Option<u32>,
    pub brp: Option<u32>,
}

impl BitTimingDto {
    fn to_timing(&self) -> BitTiming {
        BitTiming {
            tq: self.tq.unwrap_or(0),
            prop_seg: self.prop_seg.unwrap_or(0),
            phase_seg1: self.phase_seg1.unwrap_or(0),
            phase_seg2: self.phase_seg2.unwrap_or(0),
            sjw: self.sjw.unwrap_or(0),
            brp: self.brp.unwrap_or(0),
        }
    }
}

impl From<BitTiming> for BitTimingDto {
    fn from(timing: BitTiming) -> Self {
        BitTimingDto {
            tq: Some(timing.tq),
            prop_seg: Some(timing.prop_seg),
            phase_seg1: Some(timing.phase_seg1),
            phase_seg2: Some(timing.phase_seg2),
            sjw: Some(timing.sjw),
            brp: Some(timing.brp),
        }
    }
}

impl InterfaceSettingsBody {
    pub fn to_settings(&self) -> Result<InterfaceSettings, String> {
        let mut settings = InterfaceSettings::default();

        if let Some(bitrate) = self.bitrate {
            settings.fields |= SETTING_BITRATE;
            settings.bitrate = bitrate;
        }
        if let Some(data_bitrate) = self.data_bitrate {
            settings.fields |= SETTING_DATA_BITRATE;
            settings.data_bitrate = data_bitrate;
        }
        if let Some(sample_point) = self.sample_point {
            settings.fields |= SETTING_SAMPLE_POINT;
            settings.sample_point = scaled_sample_point(sample_point)?;
        }
        if let Some(data_sample_point) = self.data_sample_point {
            settings.fields |= SETTING_DATA_SAMPLE_POINT;
            settings.data_sample_point = scaled_sample_point(data_sample_point)?;
        }
        if let Some(restart_ms) = self.restart_ms {
            settings.fields |= SETTING_RESTART_MS;
            settings.restart_ms = restart_ms;
        }
        if let Some(txqueuelen) = self.txqueuelen {
            settings.fields |= SETTING_TXQUEUELEN;
            settings.txqueuelen = txqueuelen;
        }
        if let Some(termination) = self.termination {
            settings.fields |= SETTING_TERMINATION;
            settings.termination = termination;
        }
        if let Some(timing) = &self.bit_timing {
            settings.fields |= SETTING_BIT_TIMING;
            settings.bit_timing = timing.to_timing();
        }
        if let Some(timing) = &self.data_bit_timing {
            settings.fields |= SETTING_DATA_BIT_TIMING;
            settings.data_bit_timing = timing.to_timing();
        }
        if let Some(up) = self.up {
            settings.fields |= SETTING_LINK;
            settings.link_up = up;
        }
        for (name, enabled) in &self.modes {
            let flag = mode_flag(name).ok_or_else(|| format!("unknown mode: {name}"))?;
            settings.fields |= SETTING_CTRLMODE;
            settings.ctrlmode_mask |= flag;
            if *enabled {
                settings.ctrlmode_flags |= flag;
            }
        }

        if settings.fields == 0 {
            return Err("nothing to configure".to_string());
        }
        Ok(settings)
    }
}

/// The settings a device reports; absent where it reports nothing.
#[derive(Serialize, Debug, PartialEq)]
#[serde(rename_all = "camelCase")]
pub(crate) struct InterfaceSettingsDto {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub up: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub state: Option<&'static str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bitrate: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sample_point: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data_bitrate: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data_sample_point: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub restart_ms: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub txqueuelen: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub termination: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bit_timing: Option<BitTimingDto>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data_bit_timing: Option<BitTimingDto>,
    pub modes: BTreeMap<&'static str, bool>,
}

impl From<InterfaceSettings> for InterfaceSettingsDto {
    fn from(settings: InterfaceSettings) -> Self {
        let has = |flag: u32| settings.fields & flag != 0;
        let mut modes = BTreeMap::new();
        if has(SETTING_CTRLMODE) {
            for (name, flag) in MODES {
                if settings.ctrlmode_mask & flag != 0 {
                    modes.insert(name, settings.ctrlmode_flags & flag != 0);
                }
            }
        }

        InterfaceSettingsDto {
            up: has(SETTING_LINK).then_some(settings.link_up),
            state: has(SETTING_STATE).then(|| state_name(settings.can_state)),
            bitrate: has(SETTING_BITRATE).then_some(settings.bitrate),
            sample_point: has(SETTING_SAMPLE_POINT).then(|| f64::from(settings.sample_point) / SAMPLE_POINT_SCALE),
            data_bitrate: has(SETTING_DATA_BITRATE).then_some(settings.data_bitrate),
            data_sample_point: has(SETTING_DATA_SAMPLE_POINT)
                .then(|| f64::from(settings.data_sample_point) / SAMPLE_POINT_SCALE),
            restart_ms: has(SETTING_RESTART_MS).then_some(settings.restart_ms),
            txqueuelen: has(SETTING_TXQUEUELEN).then_some(settings.txqueuelen),
            termination: has(SETTING_TERMINATION).then_some(settings.termination),
            bit_timing: has(SETTING_BIT_TIMING).then(|| BitTimingDto::from(settings.bit_timing)),
            data_bit_timing: has(SETTING_DATA_BIT_TIMING).then(|| BitTimingDto::from(settings.data_bit_timing)),
            modes,
        }
    }
}

fn scaled_sample_point(fraction: f64) -> Result<u16, String> {
    if !(fraction > 0.0 && fraction < 1.0) {
        return Err(format!("sample point {fraction} is not a fraction between 0 and 1"));
    }
    Ok((fraction * SAMPLE_POINT_SCALE).round() as u16)
}

fn mode_flag(name: &str) -> Option<u32> {
    MODES.iter().find(|(mode, _)| *mode == name).map(|(_, flag)| *flag)
}

fn state_name(can_state: u8) -> &'static str {
    STATES.get(usize::from(can_state)).copied().unwrap_or("unknown")
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
    pub receive_overflows: u32,
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
            receive_overflows: status.receive_overflows,
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
    pub mode: &'static str,
    pub frames_received: u64,
    pub tx_dropped: u64,
    pub agent_name: String,
    pub interface_name: String,
}

impl From<InterfaceEntry> for InterfaceDto {
    fn from(entry: InterfaceEntry) -> Self {
        InterfaceDto {
            interface_id: entry.interface_id,
            subscriber_count: entry.subscriber_count,
            mode: interface_mode_name(entry.mode),
            frames_received: entry.frames_received,
            tx_dropped: entry.tx_dropped,
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
pub(crate) struct RejectedDto {
    pub role: &'static str,
    pub reason: &'static str,
    pub attempts: u32,
    pub last_seen_seconds: u32,
    pub name: String,
    pub fingerprint_hex: String,
    pub origin: String,
}

impl From<RejectedEntry> for RejectedDto {
    fn from(entry: RejectedEntry) -> Self {
        RejectedDto {
            role: role_name(entry.role),
            reason: rejection_reason_name(entry.reason),
            attempts: entry.attempts,
            last_seen_seconds: entry.last_seen_seconds,
            name: entry.name,
            fingerprint_hex: entry.fingerprint_hex,
            origin: entry.origin,
        }
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

fn rejection_reason_name(reason: RejectionReason) -> &'static str {
    match reason {
        RejectionReason::NotAllowed => "not-allowed",
        RejectionReason::FingerprintMismatch => "fingerprint-mismatch",
    }
}

fn interface_mode_name(mode: u8) -> &'static str {
    match mode {
        1 => "classic",
        2 => "fd",
        _ => "unknown",
    }
}

fn acl_level_name(level: AclLevel) -> &'static str {
    match level {
        AclLevel::None => "none",
        AclLevel::Read => "r",
        AclLevel::ReadWrite => "rw",
        AclLevel::ReadWriteAdmin => "rwa",
    }
}
