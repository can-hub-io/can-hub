//! REST API: JSON resources mirroring the hub admin views.
//!
//! Each handler runs a short admin exchange against the hub on a blocking task
//! (the admin client is synchronous; the connection is opened per request for
//! now — a persistent pooled client is a later optimisation). Protocol structs
//! are mapped to camelCase DTOs so the React UI consumes stable shapes
//! decoupled from the wire layout.
//!
//! The module is split by concern: [`dtos`] (wire shapes), [`error`] (the
//! [`ApiError`] HTTP mapping), [`middleware`] (the permission gate) and
//! [`handlers`] (the route handlers). Each route is registered inside the
//! permission group whose permission it requires, so a route that is never
//! classified has no home and cannot fall through to a weaker default.

mod dtos;
mod error;
mod handlers;
mod middleware;

#[cfg(test)]
mod tests;

use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use axum::http::{header, HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{any, delete, get, post, put};
use axum::{Json, Router};
use tokio::sync::broadcast;
use tower_http::services::{ServeDir, ServeFile};
use tower_http::set_header::SetResponseHeaderLayer;

use crate::admin_client::{AdminClient, AdminClientError};
use crate::auth::store::{AuthStore, StoreError};
use crate::auth::Permission;
use crate::login_limiter::LoginLimiter;
use crate::telemetry::TelemetryFrame;

use error::ErrorBody;
use middleware::require_permission;

pub use error::ApiError;

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
    /// When set, the daemon sits behind a trusted reverse proxy and the
    /// client address for rate limiting is read from `X-Forwarded-For`. Never
    /// trust that header otherwise (it is client-controlled).
    pub trusted_proxy: bool,
}

/// Build the router. When `assets_dir` is set, the built SPA is served from it
/// with an index.html fallback so client-side routes resolve (production
/// serving; dev uses the Vite proxy instead); otherwise the embedded assets
/// serve the SPA when the `embed-ui` feature is on.
pub fn router(state: AppState, assets_dir: Option<PathBuf>) -> Router {
    // Public: no session, no permission. Everything else is registered inside a
    // permission group, so a route the developer forgets to classify simply has
    // no home — it cannot fall through to a weaker default (fail-closed).
    let public = Router::new()
        .route("/healthz", get(handlers::healthz))
        .route("/api/auth/state", get(handlers::auth_state))
        .route("/api/login", post(handlers::login))
        .route("/api/logout", post(handlers::logout))
        .route("/api/setup", post(handlers::setup));

    // Self-service: any authenticated user, no specific permission.
    let authenticated = guarded(&state, None, Router::new()
        .route("/api/auth/password", post(handlers::change_own_password)));

    let views = guarded(&state, Some(Permission::ViewsRead), Router::new()
        .route("/api/status", get(handlers::status))
        .route("/api/peers", get(handlers::peers))
        .route("/api/agents", get(handlers::agents))
        .route("/api/clients", get(handlers::clients))
        .route("/api/interfaces", get(handlers::interfaces))
        .route("/api/telemetry/ws", get(handlers::telemetry_ws)));

    let peers_kick = guarded(&state, Some(Permission::PeersKick), Router::new()
        .route("/api/peers/{id}/kick", post(handlers::kick_peer))
        .route("/api/agents/{name}/kick", post(handlers::kick_agent)));

    let interfaces_config = guarded(&state, Some(Permission::InterfacesConfig), Router::new()
        .route("/api/interfaces/config", post(handlers::interface_config)));

    let pins_manage = guarded(&state, Some(Permission::PinsManage), Router::new()
        .route("/api/pins", get(handlers::pins).post(handlers::pin_add))
        .route("/api/pins/{name}", delete(handlers::pin_delete)));

    let acl_manage = guarded(&state, Some(Permission::AclManage), Router::new()
        .route("/api/acls", get(handlers::acls).post(handlers::acl_set))
        .route("/api/acls/revoke", post(handlers::acl_revoke)));

    let users_manage = guarded(&state, Some(Permission::UsersManage), Router::new()
        .route("/api/audit", get(handlers::list_audit_log))
        .route("/api/permissions", get(handlers::list_permissions))
        .route("/api/users", get(handlers::list_users).post(handlers::create_user))
        .route("/api/users/{id}", delete(handlers::delete_user))
        .route("/api/users/{id}/enabled", post(handlers::set_user_enabled))
        .route("/api/users/{id}/password", post(handlers::reset_user_password))
        .route("/api/users/{id}/groups", post(handlers::add_membership))
        .route("/api/users/{id}/groups/{group_id}", delete(handlers::remove_membership))
        .route("/api/groups", get(handlers::list_groups).post(handlers::create_group))
        .route("/api/groups/{id}", delete(handlers::delete_group))
        .route("/api/groups/{id}/permissions", put(handlers::set_group_permissions)));

    let mut router = public
        .merge(authenticated)
        .merge(views)
        .merge(peers_kick)
        .merge(interfaces_config)
        .merge(pins_manage)
        .merge(acl_manage)
        .merge(users_manage)
        // Any unmatched /api/* path is a 404 JSON, never the SPA fallback.
        .route("/api/{*rest}", any(api_not_found))
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

    with_security_headers(router)
}

/// Baseline response hardening for every route: a self-only CSP (inline styles
/// allowed for the SPA's style props, but no remote or inline scripts), plus
/// clickjacking, MIME-sniffing and referrer protections.
fn with_security_headers(router: Router) -> Router {
    const CSP: &str = "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; \
                       img-src 'self' data:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'";
    router
        .layer(SetResponseHeaderLayer::overriding(header::CONTENT_SECURITY_POLICY, HeaderValue::from_static(CSP)))
        .layer(SetResponseHeaderLayer::overriding(header::X_FRAME_OPTIONS, HeaderValue::from_static("DENY")))
        .layer(SetResponseHeaderLayer::overriding(
            header::X_CONTENT_TYPE_OPTIONS,
            HeaderValue::from_static("nosniff"),
        ))
        .layer(SetResponseHeaderLayer::overriding(header::REFERRER_POLICY, HeaderValue::from_static("no-referrer")))
}

/// Wrap a group of routes in the session/permission/CSRF/audit gate for one
/// permission class. The middleware carries its own `(state, permission)` so
/// each group enforces exactly the permission it was declared with.
fn guarded(state: &AppState, permission: Option<Permission>, router: Router<AppState>) -> Router<AppState> {
    router.route_layer(axum::middleware::from_fn_with_state((state.clone(), permission), require_permission))
}

async fn api_not_found() -> Response {
    (StatusCode::NOT_FOUND, Json(ErrorBody { error: "no such API route".into() })).into_response()
}

/// Open a short-lived admin session and run `query` on a blocking task.
pub(crate) async fn with_admin<T, F>(state: &AppState, query: F) -> Result<T, ApiError>
where
    F: FnOnce(&mut AdminClient<UnixStream>) -> Result<T, AdminClientError> + Send + 'static,
    T: Send + 'static,
{
    let socket_path = Arc::clone(&state.socket_path);
    let outcome = tokio::task::spawn_blocking(move || {
        let mut client = crate::hub_socket::connect(&socket_path)?;
        query(&mut client)
    })
    .await;

    match outcome {
        Ok(Ok(value)) => Ok(value),
        Ok(Err(error)) => Err(ApiError::Hub(error)),
        Err(join) => Err(ApiError::Internal(join.to_string())),
    }
}

/// Run a web.db query on a blocking task behind the store mutex.
pub(crate) async fn with_auth<T, F>(state: &AppState, action: F) -> Result<T, ApiError>
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
