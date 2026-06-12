//! REST API: JSON resources mirroring the hub admin views.
//!
//! Each handler runs a short admin exchange against the hub on a blocking
//! task (the admin client is synchronous; the connection is opened per request
//! for now — a persistent pooled client is a later optimisation). Protocol
//! structs are mapped to camelCase DTOs so the React UI consumes stable shapes
//! decoupled from the wire layout.

use std::net::SocketAddr;
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::{ConnectInfo, Path, Request, State};
use axum::http::header::{COOKIE, SET_COOKIE};
use axum::http::{HeaderMap, HeaderValue, Method, StatusCode};
use axum::middleware::{self, Next};
use axum::response::{IntoResponse, Response};
use axum::routing::{delete, get, post, put};
use axum::{Json, Router};
use serde::{Deserialize, Serialize};
use tokio::sync::broadcast;
use tower_http::services::{ServeDir, ServeFile};

use crate::admin_client::{AdminClient, AdminClientError};
use crate::auth::store::{AuthStore, StoreError};
use crate::auth::Permission;
use crate::login_limiter::LoginLimiter;
use crate::protocol::admin::{
    AclEntry, AclLevel, AgentEntry, ClientEntry, IfconfigOp, InterfaceEntry, PeerEntry, PinEntry,
    Status,
};
use crate::telemetry::TelemetryFrame;

const SESSION_COOKIE: &str = "canhub_session";
const CSRF_HEADER: &str = "x-csrf-token";

/// Shared handler state: the hub admin socket, the telemetry broadcast every
/// WebSocket subscriber reads from, the auth store (web.db), the login rate
/// limiter, and whether to mark the session cookie Secure (behind TLS).
#[derive(Clone)]
pub struct AppState {
    pub socket_path: Arc<str>,
    pub telemetry: broadcast::Sender<Arc<TelemetryFrame>>,
    pub auth: Arc<Mutex<AuthStore>>,
    pub login_limiter: Arc<Mutex<LoginLimiter>>,
    pub secure_cookies: bool,
}

/// Build the router. When `assets_dir` is set, the built SPA is served from it
/// with an index.html fallback so client-side routes resolve (production
/// serving; dev uses the Vite proxy instead). Production packaging will embed
/// the assets in the binary (rust-embed) rather than read them from disk.
pub fn router(state: AppState, assets_dir: Option<PathBuf>) -> Router {
    let mut router = Router::new()
        .route("/healthz", get(healthz))
        // auth (public: login/logout/setup/state)
        .route("/api/auth/state", get(auth_state))
        .route("/api/login", post(login))
        .route("/api/logout", post(logout))
        .route("/api/setup", post(setup))
        // read views (views.read)
        .route("/api/status", get(status))
        .route("/api/peers", get(peers))
        .route("/api/agents", get(agents))
        .route("/api/clients", get(clients))
        .route("/api/interfaces", get(interfaces))
        .route("/api/telemetry/ws", get(telemetry_ws))
        // actions (per-class permissions)
        .route("/api/acls", get(acls).post(acl_set))
        .route("/api/acls/revoke", post(acl_revoke))
        .route("/api/peers/{id}/kick", post(kick_peer))
        .route("/api/agents/{name}/kick", post(kick_agent))
        .route("/api/pins", get(pins).post(pin_add))
        .route("/api/pins/{name}", delete(pin_delete))
        .route("/api/interfaces/config", post(interface_config))
        // user/group management + audit (users.manage)
        .route("/api/audit", get(list_audit_log))
        .route("/api/permissions", get(list_permissions))
        .route("/api/users", get(list_users).post(create_user))
        .route("/api/users/{id}", delete(delete_user))
        .route("/api/users/{id}/enabled", post(set_user_enabled))
        .route("/api/users/{id}/groups", post(add_membership))
        .route("/api/users/{id}/groups/{group_id}", delete(remove_membership))
        .route("/api/groups", get(list_groups).post(create_group))
        .route("/api/groups/{id}", delete(delete_group))
        .route("/api/groups/{id}/permissions", put(set_group_permissions))
        .route_layer(middleware::from_fn_with_state(state.clone(), require_permission))
        .with_state(state);

    if let Some(dir) = assets_dir {
        let index = dir.join("index.html");
        router = router.fallback_service(ServeDir::new(dir).fallback(ServeFile::new(index)));
    } else {
        #[cfg(feature = "embed-ui")]
        {
            router = router.fallback(crate::embedded::serve);
        }
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

async fn pins(State(state): State<AppState>) -> Result<Json<Vec<PinDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.pins()).await?;
    Ok(Json(entries.into_iter().map(PinDto::from).collect()))
}

// ---------------------------------------------------------------- actions

#[derive(Serialize)]
struct ActionResult {
    ok: bool,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct PinAddBody {
    agent_name: String,
    fingerprint_hex: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct AclBody {
    fingerprint_hex: String,
    agent_name: String,
    interface_name: String,
    /// "none" | "ro" | "rw"; ignored by revoke.
    #[serde(default)]
    level: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct InterfaceConfigBody {
    agent_name: String,
    interface_name: String,
    /// "bitrate" | "up" | "down".
    op: String,
    #[serde(default)]
    bitrate: u32,
}

async fn kick_peer(State(state): State<AppState>, Path(id): Path<String>) -> ActionResponse {
    let peer_id = parse_peer_id(&id)?;
    action(with_admin(&state, move |client| client.kick_peer(peer_id)).await?)
}

async fn kick_agent(State(state): State<AppState>, Path(name): Path<String>) -> ActionResponse {
    action(with_admin(&state, move |client| client.kick_agent(&name)).await?)
}

async fn pin_add(State(state): State<AppState>, Json(body): Json<PinAddBody>) -> ActionResponse {
    action(with_admin(&state, move |client| client.pin_add(&body.agent_name, &body.fingerprint_hex)).await?)
}

async fn pin_delete(State(state): State<AppState>, Path(name): Path<String>) -> ActionResponse {
    action(with_admin(&state, move |client| client.pin_delete(&name)).await?)
}

async fn acl_set(State(state): State<AppState>, Json(body): Json<AclBody>) -> ActionResponse {
    let level = parse_level(&body.level)?;
    action(
        with_admin(&state, move |client| {
            client.acl_set(&body.fingerprint_hex, &body.agent_name, &body.interface_name, level)
        })
        .await?,
    )
}

async fn acl_revoke(State(state): State<AppState>, Json(body): Json<AclBody>) -> ActionResponse {
    action(
        with_admin(&state, move |client| {
            client.acl_revoke(&body.fingerprint_hex, &body.agent_name, &body.interface_name)
        })
        .await?,
    )
}

async fn interface_config(
    State(state): State<AppState>,
    Json(body): Json<InterfaceConfigBody>,
) -> ActionResponse {
    let op = parse_ifconfig_op(&body.op)?;
    let bitrate = body.bitrate;
    action(
        with_admin(&state, move |client| {
            client.ifconfig(&body.agent_name, &body.interface_name, op, bitrate)
        })
        .await?,
    )
}

type ActionResponse = Result<Json<ActionResult>, ApiError>;

/// Map an admin status byte (0 = ok) to the action response.
fn action(status: u8) -> ActionResponse {
    if status == 0 {
        Ok(Json(ActionResult { ok: true }))
    } else {
        Err(ApiError::HubRejected(status))
    }
}

fn parse_peer_id(raw: &str) -> Result<u32, ApiError> {
    let parsed = if let Some(hex) = raw.strip_prefix("0x") {
        u32::from_str_radix(hex, 16)
    } else {
        raw.parse::<u32>()
    };
    parsed.map_err(|_| ApiError::BadRequest(format!("invalid peer id: {raw}")))
}

fn parse_level(raw: &str) -> Result<AclLevel, ApiError> {
    match raw {
        "none" => Ok(AclLevel::None),
        "ro" => Ok(AclLevel::ReadOnly),
        "rw" => Ok(AclLevel::ReadWrite),
        other => Err(ApiError::BadRequest(format!("invalid level: {other} (none|ro|rw)"))),
    }
}

fn parse_ifconfig_op(raw: &str) -> Result<IfconfigOp, ApiError> {
    match raw {
        "bitrate" => Ok(IfconfigOp::SetBitrate),
        "up" => Ok(IfconfigOp::LinkUp),
        "down" => Ok(IfconfigOp::LinkDown),
        other => Err(ApiError::BadRequest(format!("invalid op: {other} (bitrate|up|down)"))),
    }
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
    BadRequest(String),
    /// The hub processed the request but returned a non-zero status (unknown
    /// agent/peer, apply failed, …).
    HubRejected(u8),
    /// web.db error (conflict → 409, otherwise 500).
    Store(StoreError),
}

impl From<StoreError> for ApiError {
    fn from(error: StoreError) -> Self {
        ApiError::Store(error)
    }
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (code, message) = match self {
            ApiError::Hub(error) => (StatusCode::BAD_GATEWAY, format!("hub unreachable: {error:?}")),
            ApiError::Internal(detail) => (StatusCode::INTERNAL_SERVER_ERROR, detail),
            ApiError::BadRequest(detail) => (StatusCode::BAD_REQUEST, detail),
            ApiError::HubRejected(status) => {
                (StatusCode::CONFLICT, format!("hub rejected the request (status {status})"))
            }
            ApiError::Store(StoreError::Conflict(detail)) => (StatusCode::CONFLICT, detail.to_string()),
            ApiError::Store(error) => (StatusCode::INTERNAL_SERVER_ERROR, format!("store error: {error:?}")),
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

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PinDto {
    pub agent_name: String,
    pub fingerprint_hex: String,
}

impl From<PinEntry> for PinDto {
    fn from(entry: PinEntry) -> Self {
        PinDto { agent_name: entry.agent_name, fingerprint_hex: entry.fingerprint_hex }
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

// ---------------------------------------------------------------- auth

/// Run a web.db query on a blocking task behind the store mutex.
async fn with_auth<T, F>(state: &AppState, action: F) -> Result<T, ApiError>
where
    F: FnOnce(&AuthStore) -> Result<T, StoreError> + Send + 'static,
    T: Send + 'static,
{
    let store = Arc::clone(&state.auth);
    let outcome = tokio::task::spawn_blocking(move || {
        let guard = store.lock().expect("auth mutex poisoned");
        action(&guard)
    })
    .await;
    match outcome {
        Ok(result) => result.map_err(ApiError::from),
        Err(join) => Err(ApiError::Internal(join.to_string())),
    }
}

fn cookie_value(headers: &HeaderMap, name: &str) -> Option<String> {
    let raw = headers.get(COOKIE)?.to_str().ok()?;
    raw.split(';').find_map(|pair| {
        let (key, value) = pair.trim().split_once('=')?;
        (key == name).then(|| value.to_string())
    })
}

fn permission_names(permissions: &std::collections::HashSet<Permission>) -> Vec<String> {
    let mut names: Vec<String> = permissions.iter().map(|p| p.as_str().to_string()).collect();
    names.sort();
    names
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct AuthStateDto {
    needs_bootstrap: bool,
    authenticated: bool,
    user: Option<String>,
    permissions: Vec<String>,
    /// Token the client echoes in the X-CSRF-Token header on mutating requests.
    csrf_token: Option<String>,
}

#[derive(Deserialize)]
struct Credentials {
    name: String,
    password: String,
}

async fn auth_state(State(state): State<AppState>, headers: HeaderMap) -> Result<Json<AuthStateDto>, ApiError> {
    let needs_bootstrap = with_auth(&state, |store| store.user_count()).await? == 0;
    if let Some(token) = cookie_value(&headers, SESSION_COOKIE) {
        if let Some(session) = with_auth(&state, move |store| store.validate_session(&token)).await? {
            let user_id = session.user_id;
            let name = with_auth(&state, move |store| store.user_name(user_id)).await?;
            let permissions = with_auth(&state, move |store| store.effective_permissions(user_id)).await?;
            return Ok(Json(AuthStateDto {
                needs_bootstrap,
                authenticated: true,
                user: name,
                permissions: permission_names(&permissions),
                csrf_token: Some(session.csrf_token),
            }));
        }
    }
    Ok(Json(AuthStateDto {
        needs_bootstrap,
        authenticated: false,
        user: None,
        permissions: vec![],
        csrf_token: None,
    }))
}

async fn login(
    State(state): State<AppState>,
    ConnectInfo(remote): ConnectInfo<SocketAddr>,
    Json(body): Json<Credentials>,
) -> Result<Response, ApiError> {
    let ip = remote.ip();
    {
        let mut limiter = state.login_limiter.lock().expect("limiter poisoned");
        if !limiter.allowed(ip, std::time::Instant::now()) {
            return Ok((StatusCode::TOO_MANY_REQUESTS, Json(ErrorBody { error: "too many attempts".into() }))
                .into_response());
        }
    }

    let Credentials { name, password } = body;
    let verify_name = name.clone();
    let user_id = with_auth(&state, move |store| store.verify_login(&verify_name, &password)).await?;
    let Some(user_id) = user_id else {
        state.login_limiter.lock().expect("limiter poisoned").record_failure(ip, std::time::Instant::now());
        return Ok((StatusCode::UNAUTHORIZED, Json(ErrorBody { error: "invalid credentials".into() })).into_response());
    };
    state.login_limiter.lock().expect("limiter poisoned").record_success(ip);

    let tokens = with_auth(&state, move |store| store.create_session(user_id)).await?;
    let permissions = with_auth(&state, move |store| store.effective_permissions(user_id)).await?;
    Ok(session_response(
        &state,
        &tokens.session_token,
        AuthStateDto {
            needs_bootstrap: false,
            authenticated: true,
            user: Some(name),
            permissions: permission_names(&permissions),
            csrf_token: Some(tokens.csrf_token),
        },
    ))
}

async fn logout(State(state): State<AppState>, headers: HeaderMap) -> Result<Response, ApiError> {
    if let Some(token) = cookie_value(&headers, SESSION_COOKIE) {
        with_auth(&state, move |store| store.delete_session(&token)).await?;
    }
    let cookie = format!("{SESSION_COOKIE}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
    let mut response = Json(ActionResult { ok: true }).into_response();
    response.headers_mut().insert(SET_COOKIE, HeaderValue::from_str(&cookie).expect("ascii cookie"));
    Ok(response)
}

/// First-run bootstrap: only when there are zero users, create the initial
/// admin (in an `admins` group holding every permission) and log them in.
async fn setup(State(state): State<AppState>, Json(body): Json<Credentials>) -> Result<Response, ApiError> {
    let Credentials { name, password } = body;
    let response_name = name.clone();
    let user_id = with_auth(&state, move |store| {
        if store.user_count()? != 0 {
            return Err(StoreError::Conflict("setup already completed"));
        }
        let user_id = store.create_user(&name, &password, true)?;
        let group_id = store.ensure_group("admins")?;
        store.set_group_permissions(group_id, &Permission::ALL)?;
        store.add_user_to_group(user_id, group_id)?;
        Ok(user_id)
    })
    .await?;
    let tokens = with_auth(&state, move |store| store.create_session(user_id)).await?;
    Ok(session_response(
        &state,
        &tokens.session_token,
        AuthStateDto {
            needs_bootstrap: false,
            authenticated: true,
            user: Some(response_name),
            permissions: Permission::ALL.iter().map(|p| p.as_str().to_string()).collect(),
            csrf_token: Some(tokens.csrf_token),
        },
    ))
}

fn session_response(state: &AppState, token: &str, body: AuthStateDto) -> Response {
    let secure = if state.secure_cookies { "; Secure" } else { "" };
    let cookie = format!("{SESSION_COOKIE}={token}; HttpOnly; SameSite=Strict; Path=/{secure}");
    let mut response = Json(body).into_response();
    response.headers_mut().insert(SET_COOKIE, HeaderValue::from_str(&cookie).expect("ascii cookie"));
    response
}

/// Permission gate: public paths pass; everything else needs a valid session
/// and the operation class for the path. Mutating requests additionally need a
/// matching CSRF token, and are recorded in the audit log.
async fn require_permission(State(state): State<AppState>, request: Request, next: Next) -> Response {
    let path = request.uri().path().to_string();
    if is_public(&path) {
        return next.run(request).await;
    }

    let method = request.method().clone();
    let csrf_header = request.headers().get(CSRF_HEADER).and_then(|v| v.to_str().ok()).map(str::to_string);
    let token = cookie_value(request.headers(), SESSION_COOKIE);

    let session = match token {
        Some(token) => match with_auth(&state, move |store| store.validate_session(&token)).await {
            Ok(Some(session)) => session,
            Ok(None) => return unauthorized(),
            Err(error) => return error.into_response(),
        },
        None => return unauthorized(),
    };
    let user_id = session.user_id;

    let required = required_permission(&path);
    match with_auth(&state, move |store| store.effective_permissions(user_id)).await {
        Ok(permissions) if permissions.contains(&required) => {}
        Ok(_) => return forbidden(),
        Err(error) => return error.into_response(),
    }

    let mutating = is_mutating(&method);
    if mutating && csrf_header.as_deref() != Some(session.csrf_token.as_str()) {
        return (StatusCode::FORBIDDEN, Json(ErrorBody { error: "missing or invalid CSRF token".into() }))
            .into_response();
    }

    let response = next.run(request).await;

    if mutating {
        let actor = with_auth(&state, move |store| store.user_name(user_id))
            .await
            .ok()
            .flatten()
            .unwrap_or_else(|| format!("#{user_id}"));
        let status = response.status().as_u16();
        let action = method.to_string();
        let _ = with_auth(&state, move |store| {
            store.record_audit(Some(user_id), &actor, &action, &path, status)
        })
        .await;
    }

    response
}

fn is_mutating(method: &Method) -> bool {
    matches!(*method, Method::POST | Method::PUT | Method::DELETE | Method::PATCH)
}

fn is_public(path: &str) -> bool {
    matches!(path, "/healthz" | "/api/auth/state" | "/api/login" | "/api/logout" | "/api/setup")
        || !path.starts_with("/api/")
}

fn required_permission(path: &str) -> Permission {
    if path == "/api/interfaces/config" {
        Permission::InterfacesConfig
    } else if path.starts_with("/api/pins") {
        Permission::PinsManage
    } else if path.starts_with("/api/acls") {
        Permission::AclManage
    } else if (path.starts_with("/api/peers/") || path.starts_with("/api/agents/")) && path.ends_with("/kick") {
        Permission::PeersKick
    } else if path.starts_with("/api/users")
        || path.starts_with("/api/groups")
        || path.starts_with("/api/audit")
        || path == "/api/permissions"
    {
        Permission::UsersManage
    } else {
        Permission::ViewsRead
    }
}

fn unauthorized() -> Response {
    (StatusCode::UNAUTHORIZED, Json(ErrorBody { error: "authentication required".into() })).into_response()
}

fn forbidden() -> Response {
    (StatusCode::FORBIDDEN, Json(ErrorBody { error: "insufficient permission".into() })).into_response()
}

// ---------------------------------------------------- user/group management

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct UserDto {
    id: i64,
    name: String,
    enabled: bool,
    group_ids: Vec<i64>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct GroupDto {
    id: i64,
    name: String,
    permissions: Vec<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct CreateUserBody {
    name: String,
    password: String,
    #[serde(default = "default_true")]
    enabled: bool,
}

fn default_true() -> bool {
    true
}

#[derive(Deserialize)]
struct EnabledBody {
    enabled: bool,
}

#[derive(Deserialize)]
struct NameBody {
    name: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct MembershipBody {
    group_id: i64,
}

#[derive(Deserialize)]
struct PermissionsBody {
    permissions: Vec<String>,
}

async fn list_permissions() -> Json<Vec<&'static str>> {
    Json(Permission::ALL.iter().map(|p| p.as_str()).collect())
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct AuditDto {
    at: i64,
    actor: String,
    action: String,
    target: String,
    status: u16,
}

async fn list_audit_log(State(state): State<AppState>) -> Result<Json<Vec<AuditDto>>, ApiError> {
    let entries = with_auth(&state, |store| store.list_audit(200)).await?;
    Ok(Json(
        entries
            .into_iter()
            .map(|e| AuditDto { at: e.at, actor: e.actor, action: e.action, target: e.target, status: e.status })
            .collect(),
    ))
}

async fn list_users(State(state): State<AppState>) -> Result<Json<Vec<UserDto>>, ApiError> {
    let users = with_auth(&state, |store| {
        let summaries = store.list_users()?;
        let mut result = Vec::with_capacity(summaries.len());
        for summary in summaries {
            let group_ids = store.user_group_ids(summary.id)?;
            result.push(UserDto { id: summary.id, name: summary.name, enabled: summary.enabled, group_ids });
        }
        Ok(result)
    })
    .await?;
    Ok(Json(users))
}

async fn create_user(State(state): State<AppState>, Json(body): Json<CreateUserBody>) -> ActionResponse {
    with_auth(&state, move |store| store.create_user(&body.name, &body.password, body.enabled)).await?;
    Ok(Json(ActionResult { ok: true }))
}

async fn delete_user(State(state): State<AppState>, Path(id): Path<i64>) -> ActionResponse {
    with_auth(&state, move |store| store.delete_user(id)).await?;
    Ok(Json(ActionResult { ok: true }))
}

async fn set_user_enabled(
    State(state): State<AppState>,
    Path(id): Path<i64>,
    Json(body): Json<EnabledBody>,
) -> ActionResponse {
    with_auth(&state, move |store| store.set_user_enabled(id, body.enabled)).await?;
    Ok(Json(ActionResult { ok: true }))
}

async fn add_membership(
    State(state): State<AppState>,
    Path(id): Path<i64>,
    Json(body): Json<MembershipBody>,
) -> ActionResponse {
    with_auth(&state, move |store| store.add_user_to_group(id, body.group_id)).await?;
    Ok(Json(ActionResult { ok: true }))
}

async fn remove_membership(State(state): State<AppState>, Path((id, group_id)): Path<(i64, i64)>) -> ActionResponse {
    with_auth(&state, move |store| store.remove_user_from_group(id, group_id)).await?;
    Ok(Json(ActionResult { ok: true }))
}

async fn list_groups(State(state): State<AppState>) -> Result<Json<Vec<GroupDto>>, ApiError> {
    let groups = with_auth(&state, |store| store.list_groups()).await?;
    Ok(Json(
        groups
            .into_iter()
            .map(|g| GroupDto { id: g.id, name: g.name, permissions: g.permissions })
            .collect(),
    ))
}

async fn create_group(State(state): State<AppState>, Json(body): Json<NameBody>) -> ActionResponse {
    with_auth(&state, move |store| store.create_group(&body.name)).await?;
    Ok(Json(ActionResult { ok: true }))
}

async fn delete_group(State(state): State<AppState>, Path(id): Path<i64>) -> ActionResponse {
    with_auth(&state, move |store| store.delete_group(id)).await?;
    Ok(Json(ActionResult { ok: true }))
}

async fn set_group_permissions(
    State(state): State<AppState>,
    Path(id): Path<i64>,
    Json(body): Json<PermissionsBody>,
) -> ActionResponse {
    let mut permissions = Vec::with_capacity(body.permissions.len());
    for name in &body.permissions {
        match Permission::from_str(name) {
            Some(permission) => permissions.push(permission),
            None => return Err(ApiError::BadRequest(format!("unknown permission: {name}"))),
        }
    }
    with_auth(&state, move |store| store.set_group_permissions(id, &permissions)).await?;
    Ok(Json(ActionResult { ok: true }))
}
