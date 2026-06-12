//! Built SPA embedded into the binary (feature `embed-ui`), so a release is
//! self-contained and needs no `--assets` directory. Unknown non-asset paths
//! fall back to index.html for client-side routing.

use axum::body::Body;
use axum::http::{header, StatusCode, Uri};
use axum::response::{IntoResponse, Response};
use rust_embed::{Embed, EmbeddedFile};

#[derive(Embed)]
#[folder = "../ui/dist"]
struct Assets;

pub async fn serve(uri: Uri) -> Response {
    let path = uri.path().trim_start_matches('/');
    let candidate = if path.is_empty() { "index.html" } else { path };
    if let Some(file) = Assets::get(candidate) {
        return file_response(file);
    }
    match Assets::get("index.html") {
        Some(file) => file_response(file),
        None => (StatusCode::NOT_FOUND, "not found").into_response(),
    }
}

fn file_response(file: EmbeddedFile) -> Response {
    let mime = file.metadata.mimetype().to_string();
    ([(header::CONTENT_TYPE, mime)], Body::from(file.data.into_owned())).into_response()
}
