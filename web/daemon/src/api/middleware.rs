//! The permission gate: session validation, permission check, CSRF and audit
//! logging that wrap every non-public route group, plus the small request
//! helpers (cookie parsing, rate-limiter key) they rely on.

use std::net::SocketAddr;

use axum::extract::{Request, State};
use axum::http::header::COOKIE;
use axum::http::{HeaderMap, Method, StatusCode};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use axum::Json;

use crate::auth::Permission;

use super::error::ErrorBody;
use super::{with_auth, AppState};

pub(crate) const SESSION_COOKIE: &str = "canhub_session";
const CSRF_HEADER: &str = "x-csrf-token";
const FORWARDED_FOR_HEADER: &str = "x-forwarded-for";

/// The authenticated user id, injected into request extensions by the gate so
/// handlers can enforce actor-aware rules (anti-lockout) without re-validating.
#[derive(Debug, Clone, Copy)]
pub(crate) struct CurrentUser(pub i64);

/// Permission gate for one route group: requires a valid session whose user
/// holds `required`. Mutating requests additionally need a matching CSRF token,
/// and are recorded in the audit log.
pub(crate) async fn require_permission(
    State((state, required)): State<(AppState, Permission)>,
    mut request: Request,
    next: Next,
) -> Response {
    let path = request.uri().path().to_string();
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

    match with_auth(&state, move |store| store.effective_permissions(user_id)).await {
        Ok(permissions) if permissions.contains(&required) => {}
        Ok(_) => return forbidden(),
        Err(error) => return error.into_response(),
    }

    let mutating = is_mutating(&method);
    if mutating && !csrf_matches(csrf_header.as_deref(), &session.csrf_token) {
        return (StatusCode::FORBIDDEN, Json(ErrorBody { error: "missing or invalid CSRF token".into() }))
            .into_response();
    }

    request.extensions_mut().insert(CurrentUser(user_id));
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

pub(crate) fn is_mutating(method: &Method) -> bool {
    matches!(*method, Method::POST | Method::PUT | Method::DELETE | Method::PATCH)
}

/// Compare the supplied CSRF token to the session's in constant time, so a
/// timing side channel cannot leak the expected token byte by byte.
fn csrf_matches(provided: Option<&str>, expected: &str) -> bool {
    match provided {
        Some(value) => constant_time_eq(value.as_bytes(), expected.as_bytes()),
        None => false,
    }
}

fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut difference = 0u8;
    for (left, right) in a.iter().zip(b) {
        difference |= left ^ right;
    }
    difference == 0
}

pub(crate) fn cookie_value(headers: &HeaderMap, name: &str) -> Option<String> {
    let raw = headers.get(COOKIE)?.to_str().ok()?;
    raw.split(';').find_map(|pair| {
        let (key, value) = pair.trim().split_once('=')?;
        (key == name).then(|| value.to_string())
    })
}

/// The rate-limiter key for the login client. Behind a trusted proxy the real
/// client is the first `X-Forwarded-For` hop; otherwise the header is ignored
/// (client-controlled) and the transport peer address is used.
pub(crate) fn client_limiter_key(trusted_proxy: bool, headers: &HeaderMap, remote: SocketAddr) -> String {
    if trusted_proxy {
        if let Some(forwarded) = headers.get(FORWARDED_FOR_HEADER).and_then(|value| value.to_str().ok()) {
            let first = forwarded.split(',').next().unwrap_or("").trim();
            if !first.is_empty() {
                return format!("ip:{first}");
            }
        }
    }
    format!("ip:{}", remote.ip())
}

pub(crate) fn unauthorized() -> Response {
    (StatusCode::UNAUTHORIZED, Json(ErrorBody { error: "authentication required".into() })).into_response()
}

pub(crate) fn forbidden() -> Response {
    (StatusCode::FORBIDDEN, Json(ErrorBody { error: "insufficient permission".into() })).into_response()
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::http::HeaderValue;

    fn remote() -> SocketAddr {
        "203.0.113.7:54321".parse().unwrap()
    }

    fn headers_with_forwarded(value: &str) -> HeaderMap {
        let mut headers = HeaderMap::new();
        headers.insert(FORWARDED_FOR_HEADER, HeaderValue::from_str(value).unwrap());
        headers
    }

    #[test]
    fn forwarded_for_ignored_without_trusted_proxy() {
        let headers = headers_with_forwarded("10.0.0.1, 192.168.0.1");
        assert_eq!(client_limiter_key(false, &headers, remote()), "ip:203.0.113.7");
    }

    #[test]
    fn forwarded_for_used_behind_trusted_proxy() {
        let headers = headers_with_forwarded("10.0.0.1, 192.168.0.1");
        assert_eq!(client_limiter_key(true, &headers, remote()), "ip:10.0.0.1");
    }

    #[test]
    fn trusted_proxy_without_header_falls_back_to_peer() {
        assert_eq!(client_limiter_key(true, &HeaderMap::new(), remote()), "ip:203.0.113.7");
    }
}
