//! Router-level tests: the security-critical layer (session → permission →
//! CSRF) exercised end-to-end with `tower::ServiceExt::oneshot` over an
//! in-memory store, with no real hub. A request that clears the gate but needs
//! the hub fails with 502 (the socket does not exist), which is how these tests
//! tell "passed the gate" from "blocked by it" (401/403).

use std::net::SocketAddr;
use std::sync::{Arc, Mutex};

use axum::body::Body;
use axum::extract::ConnectInfo;
use axum::http::{Method, Request, StatusCode};
use tower::ServiceExt;

use crate::auth::store::{AuthStore, SessionTokens};
use crate::auth::Permission;
use crate::login_limiter::LoginLimiter;

use super::{router, AppState};

fn make_state() -> AppState {
    AppState {
        socket_path: Arc::from("/tmp/canhub-no-such-hub.sock"),
        telemetry: crate::telemetry::channel(),
        auth: Arc::new(Mutex::new(AuthStore::open_in_memory().unwrap())),
        login_limiter: Arc::new(Mutex::new(LoginLimiter::default())),
        secure_cookies: false,
        trusted_proxy: false,
    }
}

/// Seed a user in its own group holding `permissions`, return a live session.
fn seed_session(state: &AppState, name: &str, permissions: &[Permission]) -> SessionTokens {
    let store = state.auth.lock().unwrap();
    let user = store.create_user(name, "password1", true).unwrap();
    let group = store.create_group(&format!("g-{name}")).unwrap();
    store.set_group_permissions(group, permissions).unwrap();
    store.add_user_to_group(user, group).unwrap();
    store.create_session(user).unwrap()
}

fn request(method: Method, path: &str, session: Option<&SessionTokens>, csrf: bool) -> Request<Body> {
    build_request(method, path, session, csrf, None)
}

fn json_request(method: Method, path: &str, body: &str) -> Request<Body> {
    build_request(method, path, None, false, Some(body.to_string()))
}

fn build_request(
    method: Method,
    path: &str,
    session: Option<&SessionTokens>,
    csrf: bool,
    json: Option<String>,
) -> Request<Body> {
    let mut builder = Request::builder().method(method).uri(path);
    if let Some(session) = session {
        builder = builder.header("cookie", format!("canhub_session={}", session.session_token));
        if csrf {
            builder = builder.header("x-csrf-token", session.csrf_token.clone());
        }
    }
    let body = match json {
        Some(json) => {
            builder = builder.header("content-type", "application/json");
            Body::from(json)
        }
        None => Body::empty(),
    };
    let mut request = builder.body(body).unwrap();
    request.extensions_mut().insert(ConnectInfo("127.0.0.1:9000".parse::<SocketAddr>().unwrap()));
    request
}

async fn status_of(state: AppState, request: Request<Body>) -> StatusCode {
    router(state, None).oneshot(request).await.unwrap().status()
}

#[tokio::test]
async fn unauthenticated_request_is_401() {
    let state = make_state();
    let code = status_of(state, request(Method::GET, "/api/status", None, false)).await;
    assert_eq!(code, StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn wrong_permission_is_403() {
    let state = make_state();
    let session = seed_session(&state, "viewer", &[Permission::ViewsRead]);
    let code = status_of(state, request(Method::GET, "/api/users", Some(&session), false)).await;
    assert_eq!(code, StatusCode::FORBIDDEN);
}

#[tokio::test]
async fn correct_permission_clears_the_gate() {
    let state = make_state();
    let session = seed_session(&state, "viewer", &[Permission::ViewsRead]);
    // Passes the gate, then fails reaching the (absent) hub → 502, not 401/403.
    let code = status_of(state, request(Method::GET, "/api/status", Some(&session), false)).await;
    assert_eq!(code, StatusCode::BAD_GATEWAY);
}

#[tokio::test]
async fn users_manage_route_returns_data() {
    let state = make_state();
    let session = seed_session(&state, "admin", &[Permission::UsersManage]);
    // /api/users is served from the store, not the hub, so it succeeds.
    let code = status_of(state, request(Method::GET, "/api/users", Some(&session), false)).await;
    assert_eq!(code, StatusCode::OK);
}

#[tokio::test]
async fn pins_permission_maps_to_pins_routes() {
    let state = make_state();
    let pinner = seed_session(&state, "pinner", &[Permission::PinsManage]);
    let viewer = seed_session(&state, "viewer", &[Permission::ViewsRead]);
    assert_eq!(
        status_of(state.clone(), request(Method::GET, "/api/pins", Some(&pinner), false)).await,
        StatusCode::BAD_GATEWAY
    );
    assert_eq!(
        status_of(state, request(Method::GET, "/api/pins", Some(&viewer), false)).await,
        StatusCode::FORBIDDEN
    );
}

#[tokio::test]
async fn mutation_requires_csrf_token() {
    let state = make_state();
    let session = seed_session(&state, "kicker", &[Permission::PeersKick]);
    assert_eq!(
        status_of(state.clone(), request(Method::POST, "/api/peers/1/kick", Some(&session), false)).await,
        StatusCode::FORBIDDEN
    );
    // With the CSRF header the gate passes; the absent hub then yields 502.
    assert_eq!(
        status_of(state, request(Method::POST, "/api/peers/1/kick", Some(&session), true)).await,
        StatusCode::BAD_GATEWAY
    );
}

#[tokio::test]
async fn setup_is_public_then_conflicts() {
    let state = make_state();
    let first = status_of(state.clone(), json_request(Method::POST, "/api/setup", r#"{"name":"root","password":"password1"}"#)).await;
    assert_eq!(first, StatusCode::OK);
    let second = status_of(state, json_request(Method::POST, "/api/setup", r#"{"name":"root2","password":"password1"}"#)).await;
    assert_eq!(second, StatusCode::CONFLICT);
}

#[tokio::test]
async fn unknown_api_route_is_404_not_spa() {
    let state = make_state();
    let session = seed_session(&state, "admin", &[Permission::UsersManage]);
    let code = status_of(state, request(Method::GET, "/api/does-not-exist", Some(&session), false)).await;
    assert_eq!(code, StatusCode::NOT_FOUND);
}

fn json_with_session(method: Method, path: &str, session: &SessionTokens, json: &str) -> Request<Body> {
    build_request(method, path, Some(session), true, Some(json.to_string()))
}

#[tokio::test]
async fn self_password_change_requires_authentication() {
    let state = make_state();
    let code = status_of(state, json_request(Method::POST, "/api/auth/password", r#"{"currentPassword":"x","newPassword":"newpassword"}"#)).await;
    assert_eq!(code, StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn self_password_change_checks_current_password() {
    let state = make_state();
    let session = seed_session(&state, "u", &[Permission::ViewsRead]);
    let wrong = json_with_session(Method::POST, "/api/auth/password", &session, r#"{"currentPassword":"wrong","newPassword":"newpassword"}"#);
    assert_eq!(status_of(state.clone(), wrong).await, StatusCode::BAD_REQUEST);
    let right = json_with_session(Method::POST, "/api/auth/password", &session, r#"{"currentPassword":"password1","newPassword":"newpassword"}"#);
    assert_eq!(status_of(state, right).await, StatusCode::OK);
}

#[tokio::test]
async fn admin_password_reset_needs_users_manage() {
    let state = make_state();
    let admin = seed_session(&state, "admin", &[Permission::UsersManage]);
    let viewer = seed_session(&state, "viewer", &[Permission::ViewsRead]);
    let target = {
        let store = state.auth.lock().unwrap();
        store.create_user("target", "password1", true).unwrap()
    };
    let path = format!("/api/users/{target}/password");
    let denied = json_with_session(Method::POST, &path, &viewer, r#"{"password":"newpassword"}"#);
    assert_eq!(status_of(state.clone(), denied).await, StatusCode::FORBIDDEN);
    let ok = json_with_session(Method::POST, &path, &admin, r#"{"password":"newpassword"}"#);
    assert_eq!(status_of(state, ok).await, StatusCode::OK);
}

#[tokio::test]
async fn responses_carry_security_headers() {
    let response =
        router(make_state(), None).oneshot(request(Method::GET, "/healthz", None, false)).await.unwrap();
    let headers = response.headers();
    assert_eq!(headers.get("x-frame-options").unwrap(), "DENY");
    assert_eq!(headers.get("x-content-type-options").unwrap(), "nosniff");
    assert_eq!(headers.get("referrer-policy").unwrap(), "no-referrer");
    assert!(headers.get("content-security-policy").is_some());
}

#[tokio::test]
async fn router_builds_without_route_conflicts() {
    let _ = router(make_state(), None);
}
