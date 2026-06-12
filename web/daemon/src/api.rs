//! REST API: JSON resources mirroring the hub admin views.
//!
//! Each handler runs a short admin exchange against the hub on a blocking
//! task (the admin client is synchronous; the connection is opened per request
//! for now — a persistent pooled client is a later optimisation). Protocol
//! structs are mapped to camelCase DTOs so the React UI consumes stable shapes
//! decoupled from the wire layout.

use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::sync::Arc;

use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::State;
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::routing::get;
use axum::{Json, Router};
use serde::Serialize;
use tokio::sync::broadcast;
use tower_http::services::{ServeDir, ServeFile};

use crate::admin_client::{AdminClient, AdminClientError};
use crate::protocol::admin::{
    AclEntry, AclLevel, AgentEntry, ClientEntry, InterfaceEntry, PeerEntry, Status,
};
use crate::telemetry::TelemetryFrame;

/// Shared handler state: where the hub admin socket lives plus the telemetry
/// broadcast every WebSocket subscriber reads from.
#[derive(Clone)]
pub struct AppState {
    pub socket_path: Arc<str>,
    pub telemetry: broadcast::Sender<Arc<TelemetryFrame>>,
}

/// Build the router. When `assets_dir` is set, the built SPA is served from it
/// with an index.html fallback so client-side routes resolve (production
/// serving; dev uses the Vite proxy instead). Production packaging will embed
/// the assets in the binary (rust-embed) rather than read them from disk.
pub fn router(state: AppState, assets_dir: Option<PathBuf>) -> Router {
    let mut router = Router::new()
        .route("/healthz", get(healthz))
        .route("/api/status", get(status))
        .route("/api/peers", get(peers))
        .route("/api/agents", get(agents))
        .route("/api/clients", get(clients))
        .route("/api/interfaces", get(interfaces))
        .route("/api/acls", get(acls))
        .route("/api/telemetry/ws", get(telemetry_ws))
        .with_state(state);

    if let Some(dir) = assets_dir {
        let index = dir.join("index.html");
        router = router.fallback_service(ServeDir::new(dir).fallback(ServeFile::new(index)));
    }

    router
}

async fn healthz() -> &'static str {
    "ok"
}

async fn status(State(state): State<AppState>) -> Result<Json<StatusDto>, ApiError> {
    let status = with_admin(&state, |client| client.status()).await?;
    Ok(Json(StatusDto::from(status)))
}

async fn peers(State(state): State<AppState>) -> Result<Json<Vec<PeerDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.peers()).await?;
    Ok(Json(entries.into_iter().map(PeerDto::from).collect()))
}

async fn agents(State(state): State<AppState>) -> Result<Json<Vec<AgentDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.agents("")).await?;
    Ok(Json(entries.into_iter().map(AgentDto::from).collect()))
}

async fn clients(State(state): State<AppState>) -> Result<Json<Vec<ClientDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.clients("")).await?;
    Ok(Json(entries.into_iter().map(ClientDto::from).collect()))
}

async fn interfaces(State(state): State<AppState>) -> Result<Json<Vec<InterfaceDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.interfaces()).await?;
    Ok(Json(entries.into_iter().map(InterfaceDto::from).collect()))
}

async fn acls(State(state): State<AppState>) -> Result<Json<Vec<AclDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.acl_list()).await?;
    Ok(Json(entries.into_iter().map(AclDto::from).collect()))
}

/// Telemetry WebSocket: subscribe to the shared broadcast and forward each
/// frame as JSON. One poll loop fills the broadcast for every subscriber.
/// (Permission gating — `views.read` — lands with auth in a later phase.)
async fn telemetry_ws(ws: WebSocketUpgrade, State(state): State<AppState>) -> Response {
    let receiver = state.telemetry.subscribe();
    ws.on_upgrade(move |socket| telemetry_stream(socket, receiver))
}

async fn telemetry_stream(mut socket: WebSocket, mut receiver: broadcast::Receiver<Arc<TelemetryFrame>>) {
    loop {
        match receiver.recv().await {
            Ok(frame) => {
                let Ok(json) = serde_json::to_string(&*frame) else { continue };
                if socket.send(Message::Text(json.into())).await.is_err() {
                    break;
                }
            }
            // Slow subscriber fell behind: skip to the latest, do not disconnect.
            Err(broadcast::error::RecvError::Lagged(_)) => continue,
            Err(broadcast::error::RecvError::Closed) => break,
        }
    }
}

/// Open a short-lived admin session and run `query` on a blocking task.
async fn with_admin<T, F>(state: &AppState, query: F) -> Result<T, ApiError>
where
    F: FnOnce(&mut AdminClient<UnixStream>) -> Result<T, AdminClientError> + Send + 'static,
    T: Send + 'static,
{
    let socket_path = Arc::clone(&state.socket_path);
    let outcome = tokio::task::spawn_blocking(move || {
        let stream = UnixStream::connect(&*socket_path)?;
        let mut client = AdminClient::connect(stream)?;
        query(&mut client)
    })
    .await;

    match outcome {
        Ok(Ok(value)) => Ok(value),
        Ok(Err(error)) => Err(ApiError::Hub(error)),
        Err(join) => Err(ApiError::Internal(join.to_string())),
    }
}

/// API error mapped to an HTTP status. Hub-side failures are 502 (the panel is
/// healthy, the upstream admin plane is not); join failures are 500.
pub enum ApiError {
    Hub(AdminClientError),
    Internal(String),
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (code, message) = match self {
            ApiError::Hub(error) => (StatusCode::BAD_GATEWAY, format!("hub unreachable: {error:?}")),
            ApiError::Internal(detail) => (StatusCode::INTERNAL_SERVER_ERROR, detail),
        };
        (code, Json(ErrorBody { error: message })).into_response()
    }
}

#[derive(Serialize)]
struct ErrorBody {
    error: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StatusDto {
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
pub struct PeerDto {
    pub peer_id: u32,
    pub role: &'static str,
    pub agent_name: String,
    pub fingerprint_hex: String,
    pub frames_forwarded: u32,
    pub frames_dropped: u32,
}

impl From<PeerEntry> for PeerDto {
    fn from(entry: PeerEntry) -> Self {
        PeerDto {
            peer_id: entry.peer_id,
            role: role_name(entry.role),
            agent_name: entry.agent_name,
            fingerprint_hex: entry.fingerprint_hex,
            frames_forwarded: entry.frames_forwarded,
            frames_dropped: entry.frames_dropped,
        }
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentDto {
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
pub struct ClientDto {
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
pub struct InterfaceDto {
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
pub struct AclDto {
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

fn role_name(role: u8) -> &'static str {
    match role {
        1 => "agent",
        2 => "client",
        3 => "admin",
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
