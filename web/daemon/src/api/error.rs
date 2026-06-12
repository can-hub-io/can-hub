//! The API error type and its mapping to HTTP responses.

use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde::Serialize;

use crate::admin_client::AdminClientError;
use crate::auth::store::StoreError;

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
pub(crate) struct ErrorBody {
    pub error: String,
}
