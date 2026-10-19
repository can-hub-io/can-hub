//! The API error type and its mapping to HTTP responses.

use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde::Serialize;

use crate::admin_client::AdminClientError;
use crate::auth::store::StoreError;
use crate::protocol::admin::{
    BRIDGE_STATUS_FULL, BRIDGE_STATUS_INVALID, BRIDGE_STATUS_UNKNOWN, CONFIGURE_STATUS_AGENT_UNREACHABLE, CONFIGURE_STATUS_DENIED, CONFIGURE_STATUS_UNKNOWN_INTERFACE,
};

/// API error mapped to an HTTP status. Hub-side failures are 502 (the panel is
/// healthy, the upstream admin plane is not); join failures are 500.
pub enum ApiError {
    Hub(AdminClientError),
    Internal(String),
    BadRequest(String),
    /// The hub processed the request but returned a non-zero status (unknown
    /// agent/peer, apply failed, …).
    HubRejected(u8),
    /// CONFIGURE came back with a non-zero status, with the agent's reason.
    Configure { status: u8, detail: String },
    /// ADMIN_BRIDGE_SET/DELETE came back with a non-zero status.
    Bridge(u8),
    /// An ADMIN_GROUP_* change came back with a non-zero status.
    Group(u8),
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
            ApiError::Configure { status, detail } => configure_failure(status, detail),
            ApiError::Bridge(status) => bridge_failure(status),
            ApiError::Group(status) => group_failure(status),
            ApiError::Store(StoreError::Conflict(detail)) => (StatusCode::CONFLICT, detail.to_string()),
            ApiError::Store(error) => (StatusCode::INTERNAL_SERVER_ERROR, format!("store error: {error:?}")),
        };
        (code, Json(ErrorBody { error: message })).into_response()
    }
}

#[derive(Serialize)]
pub(crate) struct ErrorBody {
    pub error: String,
}

fn bridge_failure(status: u8) -> (StatusCode, String) {
    let (code, reason) = match status {
        BRIDGE_STATUS_INVALID => (StatusCode::BAD_REQUEST, "a bridge needs two different interfaces"),
        BRIDGE_STATUS_FULL => (StatusCode::CONFLICT, "the hub holds no more bridges"),
        BRIDGE_STATUS_UNKNOWN => (StatusCode::NOT_FOUND, "no such bridge"),
        _ => (StatusCode::INTERNAL_SERVER_ERROR, "could not store the bridge"),
    };
    (code, reason.to_string())
}

fn group_failure(status: u8) -> (StatusCode, String) {
    let (code, reason) = match status {
        BRIDGE_STATUS_INVALID => (StatusCode::BAD_REQUEST, "a group name is 1-31 of A-Z a-z 0-9 . _ -"),
        BRIDGE_STATUS_FULL => (StatusCode::CONFLICT, "the hub holds no more groups, or the group already has 16 members"),
        BRIDGE_STATUS_UNKNOWN => (StatusCode::NOT_FOUND, "no such group or member"),
        _ => (StatusCode::INTERNAL_SERVER_ERROR, "could not store the group"),
    };
    (code, reason.to_string())
}

fn configure_failure(status: u8, detail: String) -> (StatusCode, String) {
    let (code, reason) = match status {
        CONFIGURE_STATUS_UNKNOWN_INTERFACE => (StatusCode::NOT_FOUND, "unknown interface"),
        CONFIGURE_STATUS_AGENT_UNREACHABLE => (StatusCode::BAD_GATEWAY, "agent unreachable"),
        CONFIGURE_STATUS_DENIED => (StatusCode::CONFLICT, "not allowed to configure this interface"),
        _ => (StatusCode::CONFLICT, "apply failed"),
    };
    if detail.is_empty() {
        return (code, reason.to_string());
    }
    (code, format!("{reason}: {detail}"))
}
