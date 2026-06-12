//! The request handlers: read views, mutating actions, the telemetry
//! WebSocket, authentication, and user/group management.

use std::collections::HashSet;
use std::net::SocketAddr;

use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::{ConnectInfo, Path, State};
use axum::http::header::SET_COOKIE;
use axum::http::{HeaderMap, HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::{Extension, Json};
use tokio::sync::broadcast;

use crate::auth::Permission;
use crate::protocol::admin::{AclLevel, IfconfigOp};
use crate::telemetry::TelemetryFrame;

use super::dtos::*;
use super::error::{ApiError, ErrorBody};
use super::middleware::{client_limiter_key, cookie_value, CurrentUser, SESSION_COOKIE};
use super::{with_admin, with_auth, AppState};

pub(crate) async fn healthz() -> &'static str {
    "ok"
}

// ------------------------------------------------------------ read views

pub(crate) async fn status(State(state): State<AppState>) -> Result<Json<StatusDto>, ApiError> {
    let status = with_admin(&state, |client| client.status()).await?;
    Ok(Json(StatusDto::from(status)))
}

pub(crate) async fn peers(State(state): State<AppState>) -> Result<Json<Vec<PeerDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.peers()).await?;
    Ok(Json(entries.into_iter().map(PeerDto::from).collect()))
}

pub(crate) async fn agents(State(state): State<AppState>) -> Result<Json<Vec<AgentDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.agents("")).await?;
    Ok(Json(entries.into_iter().map(AgentDto::from).collect()))
}

pub(crate) async fn clients(State(state): State<AppState>) -> Result<Json<Vec<ClientDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.clients("")).await?;
    Ok(Json(entries.into_iter().map(ClientDto::from).collect()))
}

pub(crate) async fn interfaces(State(state): State<AppState>) -> Result<Json<Vec<InterfaceDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.interfaces()).await?;
    Ok(Json(entries.into_iter().map(InterfaceDto::from).collect()))
}

pub(crate) async fn acls(State(state): State<AppState>) -> Result<Json<Vec<AclDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.acl_list()).await?;
    Ok(Json(entries.into_iter().map(AclDto::from).collect()))
}

pub(crate) async fn pins(State(state): State<AppState>) -> Result<Json<Vec<PinDto>>, ApiError> {
    let entries = with_admin(&state, |client| client.pins()).await?;
    Ok(Json(entries.into_iter().map(PinDto::from).collect()))
}

// --------------------------------------------------------------- actions

pub(crate) async fn kick_peer(State(state): State<AppState>, Path(id): Path<String>) -> ActionResponse {
    let peer_id = parse_peer_id(&id)?;
    action(with_admin(&state, move |client| client.kick_peer(peer_id)).await?)
}

pub(crate) async fn kick_agent(State(state): State<AppState>, Path(name): Path<String>) -> ActionResponse {
    action(with_admin(&state, move |client| client.kick_agent(&name)).await?)
}

pub(crate) async fn pin_add(State(state): State<AppState>, Json(body): Json<PinAddBody>) -> ActionResponse {
    action(with_admin(&state, move |client| client.pin_add(&body.agent_name, &body.fingerprint_hex)).await?)
}

pub(crate) async fn pin_delete(State(state): State<AppState>, Path(name): Path<String>) -> ActionResponse {
    action(with_admin(&state, move |client| client.pin_delete(&name)).await?)
}

pub(crate) async fn acl_set(State(state): State<AppState>, Json(body): Json<AclBody>) -> ActionResponse {
    let level = parse_level(&body.level)?;
    action(
        with_admin(&state, move |client| {
            client.acl_set(&body.fingerprint_hex, &body.agent_name, &body.interface_name, level)
        })
        .await?,
    )
}

pub(crate) async fn acl_revoke(State(state): State<AppState>, Json(body): Json<AclBody>) -> ActionResponse {
    action(
        with_admin(&state, move |client| {
            client.acl_revoke(&body.fingerprint_hex, &body.agent_name, &body.interface_name)
        })
        .await?,
    )
}

/// Classic CAN tops out at 1 Mbit/s; anything above is a typo or an attempt to
/// wedge the interface, and 0 would silently mean "leave it unset".
const MAX_CLASSIC_CAN_BITRATE: u32 = 1_000_000;

fn checked_bitrate(op: IfconfigOp, bitrate: u32) -> Result<u32, ApiError> {
    if op == IfconfigOp::SetBitrate && (bitrate == 0 || bitrate > MAX_CLASSIC_CAN_BITRATE) {
        return Err(ApiError::BadRequest(format!("bitrate must be 1..={MAX_CLASSIC_CAN_BITRATE}, got {bitrate}")));
    }
    Ok(bitrate)
}

pub(crate) async fn interface_config(
    State(state): State<AppState>,
    Json(body): Json<InterfaceConfigBody>,
) -> ActionResponse {
    let op = parse_ifconfig_op(&body.op)?;
    let bitrate = checked_bitrate(op, body.bitrate)?;
    action(
        with_admin(&state, move |client| {
            client.ifconfig(&body.agent_name, &body.interface_name, op, bitrate)
        })
        .await?,
    )
}

pub(crate) type ActionResponse = Result<Json<ActionResult>, ApiError>;

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

// ------------------------------------------------------------- telemetry

/// Telemetry WebSocket: subscribe to the shared broadcast and forward each
/// frame as JSON. One poll loop fills the broadcast for every subscriber.
pub(crate) async fn telemetry_ws(ws: WebSocketUpgrade, State(state): State<AppState>) -> Response {
    let receiver = state.telemetry.subscribe();
    ws.on_upgrade(move |socket| telemetry_stream(socket, receiver))
}

async fn telemetry_stream(mut socket: WebSocket, mut receiver: broadcast::Receiver<std::sync::Arc<TelemetryFrame>>) {
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

// ------------------------------------------------------------------ auth

fn permission_names(permissions: &HashSet<Permission>) -> Vec<String> {
    let mut names: Vec<String> = permissions.iter().map(|p| p.as_str().to_string()).collect();
    names.sort();
    names
}

pub(crate) async fn auth_state(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<AuthStateDto>, ApiError> {
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

pub(crate) async fn login(
    State(state): State<AppState>,
    ConnectInfo(remote): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(body): Json<Credentials>,
) -> Result<Response, ApiError> {
    let Credentials { name, password } = body;
    let client_key = client_limiter_key(state.trusted_proxy, &headers, remote);
    let user_key = format!("user:{name}");
    let now = std::time::Instant::now();
    let blocked = {
        let mut limiter = state.login_limiter.lock().expect("limiter poisoned");
        !limiter.allowed(&client_key, now) || !limiter.allowed(&user_key, now)
    };
    if blocked {
        record_audit(&state, None, &name, "LOGIN", "/api/login", 429).await;
        return Ok((StatusCode::TOO_MANY_REQUESTS, Json(ErrorBody { error: "too many attempts".into() }))
            .into_response());
    }

    let verify_name = name.clone();
    let user_id = with_auth(&state, move |store| store.verify_login(&verify_name, &password)).await?;
    let Some(user_id) = user_id else {
        {
            let mut limiter = state.login_limiter.lock().expect("limiter poisoned");
            limiter.record_failure(&client_key, now);
            limiter.record_failure(&user_key, now);
        }
        record_audit(&state, None, &name, "LOGIN", "/api/login", 401).await;
        return Ok((StatusCode::UNAUTHORIZED, Json(ErrorBody { error: "invalid credentials".into() })).into_response());
    };
    {
        let mut limiter = state.login_limiter.lock().expect("limiter poisoned");
        limiter.record_success(&client_key);
        limiter.record_success(&user_key);
    }
    record_audit(&state, Some(user_id), &name, "LOGIN", "/api/login", 200).await;

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

pub(crate) async fn logout(State(state): State<AppState>, headers: HeaderMap) -> Result<Response, ApiError> {
    if let Some(token) = cookie_value(&headers, SESSION_COOKIE) {
        let resolve = token.clone();
        if let Ok(Some(session)) = with_auth(&state, move |store| store.validate_session(&resolve)).await {
            let user_id = session.user_id;
            let actor = with_auth(&state, move |store| store.user_name(user_id))
                .await
                .ok()
                .flatten()
                .unwrap_or_else(|| format!("#{user_id}"));
            record_audit(&state, Some(user_id), &actor, "LOGOUT", "/api/logout", 200).await;
        }
        with_auth(&state, move |store| store.delete_session(&token)).await?;
    }
    let cookie = format!("{SESSION_COOKIE}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
    let mut response = Json(ActionResult { ok: true }).into_response();
    response.headers_mut().insert(SET_COOKIE, HeaderValue::from_str(&cookie).expect("ascii cookie"));
    Ok(response)
}

/// First-run bootstrap: only when there are zero users, create the initial
/// admin (in an `admins` group holding every permission) and log them in.
pub(crate) async fn setup(State(state): State<AppState>, Json(body): Json<Credentials>) -> Result<Response, ApiError> {
    let Credentials { name, password } = body;
    let response_name = name.clone();
    let user_id = with_auth(&state, move |store| store.bootstrap_admin(&name, &password, true)).await?;
    record_audit(&state, Some(user_id), &response_name, "SETUP", "/api/setup", 200).await;
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

/// Fire-and-forget audit record for the public auth endpoints (the gate
/// middleware audits everything else). A failure to record must not fail login.
async fn record_audit(state: &AppState, actor_id: Option<i64>, actor: &str, action: &str, target: &str, status: u16) {
    let actor = actor.to_string();
    let action = action.to_string();
    let target = target.to_string();
    let _ = with_auth(state, move |store| store.record_audit(actor_id, &actor, &action, &target, status)).await;
}

fn session_response(state: &AppState, token: &str, body: AuthStateDto) -> Response {
    let secure = if state.secure_cookies { "; Secure" } else { "" };
    let cookie = format!("{SESSION_COOKIE}={token}; HttpOnly; SameSite=Strict; Path=/{secure}");
    let mut response = Json(body).into_response();
    response.headers_mut().insert(SET_COOKIE, HeaderValue::from_str(&cookie).expect("ascii cookie"));
    response
}

// ---------------------------------------------------- user/group management

pub(crate) async fn list_permissions() -> Json<Vec<&'static str>> {
    Json(Permission::ALL.iter().map(|p| p.as_str()).collect())
}

pub(crate) async fn list_audit_log(State(state): State<AppState>) -> Result<Json<Vec<AuditDto>>, ApiError> {
    let entries = with_auth(&state, |store| store.list_audit(200)).await?;
    Ok(Json(
        entries
            .into_iter()
            .map(|e| AuditDto { at: e.at, actor: e.actor, action: e.action, target: e.target, status: e.status })
            .collect(),
    ))
}

pub(crate) async fn list_users(State(state): State<AppState>) -> Result<Json<Vec<UserDto>>, ApiError> {
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

pub(crate) async fn create_user(State(state): State<AppState>, Json(body): Json<CreateUserBody>) -> ActionResponse {
    with_auth(&state, move |store| store.create_user(&body.name, &body.password, body.enabled)).await?;
    Ok(Json(ActionResult { ok: true }))
}

pub(crate) async fn delete_user(
    State(state): State<AppState>,
    Extension(actor): Extension<CurrentUser>,
    Path(id): Path<i64>,
) -> ActionResponse {
    with_auth(&state, move |store| store.delete_user(actor.0, id)).await?;
    Ok(Json(ActionResult { ok: true }))
}

pub(crate) async fn set_user_enabled(
    State(state): State<AppState>,
    Extension(actor): Extension<CurrentUser>,
    Path(id): Path<i64>,
    Json(body): Json<EnabledBody>,
) -> ActionResponse {
    with_auth(&state, move |store| store.set_user_enabled(actor.0, id, body.enabled)).await?;
    Ok(Json(ActionResult { ok: true }))
}

/// Admin password reset for another user: drops all that user's sessions so
/// they must log in again with the new password.
pub(crate) async fn reset_user_password(
    State(state): State<AppState>,
    Path(id): Path<i64>,
    Json(body): Json<PasswordBody>,
) -> ActionResponse {
    with_auth(&state, move |store| store.update_password(id, &body.password, None)).await?;
    Ok(Json(ActionResult { ok: true }))
}

/// Self-service password change: confirms the current password, then updates
/// and keeps the caller's current session alive while dropping the others.
pub(crate) async fn change_own_password(
    State(state): State<AppState>,
    Extension(actor): Extension<CurrentUser>,
    headers: HeaderMap,
    Json(body): Json<SelfPasswordBody>,
) -> Result<Json<ActionResult>, ApiError> {
    let user_id = actor.0;
    let current = body.current_password;
    let verified = with_auth(&state, move |store| store.verify_user_password(user_id, &current)).await?;
    if !verified {
        return Err(ApiError::BadRequest("current password is incorrect".into()));
    }
    let keep = cookie_value(&headers, SESSION_COOKIE);
    let new_password = body.new_password;
    with_auth(&state, move |store| store.update_password(user_id, &new_password, keep.as_deref())).await?;
    Ok(Json(ActionResult { ok: true }))
}

pub(crate) async fn add_membership(
    State(state): State<AppState>,
    Path(id): Path<i64>,
    Json(body): Json<MembershipBody>,
) -> ActionResponse {
    with_auth(&state, move |store| store.add_user_to_group(id, body.group_id)).await?;
    Ok(Json(ActionResult { ok: true }))
}

pub(crate) async fn remove_membership(
    State(state): State<AppState>,
    Path((id, group_id)): Path<(i64, i64)>,
) -> ActionResponse {
    with_auth(&state, move |store| store.remove_user_from_group(id, group_id)).await?;
    Ok(Json(ActionResult { ok: true }))
}

pub(crate) async fn list_groups(State(state): State<AppState>) -> Result<Json<Vec<GroupDto>>, ApiError> {
    let groups = with_auth(&state, |store| store.list_groups()).await?;
    Ok(Json(
        groups
            .into_iter()
            .map(|g| GroupDto { id: g.id, name: g.name, permissions: g.permissions })
            .collect(),
    ))
}

pub(crate) async fn create_group(State(state): State<AppState>, Json(body): Json<NameBody>) -> ActionResponse {
    with_auth(&state, move |store| store.create_group(&body.name)).await?;
    Ok(Json(ActionResult { ok: true }))
}

pub(crate) async fn delete_group(State(state): State<AppState>, Path(id): Path<i64>) -> ActionResponse {
    with_auth(&state, move |store| store.delete_group(id)).await?;
    Ok(Json(ActionResult { ok: true }))
}

pub(crate) async fn set_group_permissions(
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bitrate_zero_or_huge_rejected_only_for_set_bitrate() {
        assert!(checked_bitrate(IfconfigOp::SetBitrate, 0).is_err());
        assert!(checked_bitrate(IfconfigOp::SetBitrate, 2_000_000).is_err());
        assert!(checked_bitrate(IfconfigOp::SetBitrate, 250_000).is_ok());
        // bitrate is ignored for link up/down, so 0 is fine there.
        assert!(checked_bitrate(IfconfigOp::LinkUp, 0).is_ok());
        assert!(checked_bitrate(IfconfigOp::LinkDown, 0).is_ok());
    }
}
