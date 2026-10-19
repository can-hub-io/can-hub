//! The API error type and its mapping to HTTP responses.

use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde::Serialize;

use crate::admin_client::AdminClientError;
use crate::auth::store::StoreError;
use crate::protocol::admin::{
    CONFIGURE_STATUS_AGENT_UNREACHABLE, CONFIGURE_STATUS_DENIED, CONFIGURE_STATUS_UNKNOWN_INTERFACE,
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
