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

// Vite emits content-hashed files under /assets/, so they may be cached
// forever; index.html (the fallback) must be revalidated so a new build is
// picked up.
const IMMUTABLE: &str = "public, max-age=31536000, immutable";
const NO_CACHE: &str = "no-cache";

pub async fn serve(uri: Uri) -> Response {
    let path = uri.path().trim_start_matches('/');
    let candidate = if path.is_empty() { "index.html" } else { path };
    if let Some(file) = Assets::get(candidate) {
        return file_response(file, cache_control(candidate));
    }
    match Assets::get("index.html") {
        Some(file) => file_response(file, NO_CACHE),
        None => (StatusCode::NOT_FOUND, "not found").into_response(),
    }
}

fn cache_control(path: &str) -> &'static str {
    if path.starts_with("assets/") {
        IMMUTABLE
    } else {
        NO_CACHE
    }
}

fn file_response(file: EmbeddedFile, cache: &'static str) -> Response {
    let mime = file.metadata.mimetype().to_string();
    (
        [(header::CONTENT_TYPE, mime), (header::CACHE_CONTROL, cache.to_string())],
        Body::from(file.data.into_owned()),
    )
        .into_response()
}
