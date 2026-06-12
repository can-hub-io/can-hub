//! can-hub-web: web admin panel daemon.
//!
//! The daemon is a sibling consumer of the hub admin plane: it speaks the
//! binary admin protocol (message family 0x10..=0x2B) over the hub unix
//! socket and exposes a REST API plus a React UI to browsers. The protocol
//! codecs stay dependency-free and unit-testable on their own, below the
//! async/HTTP layers.

pub mod admin_client;
pub mod api;
pub mod auth;
#[cfg(feature = "embed-ui")]
pub mod embedded;
pub mod hub_socket;
pub mod login_limiter;
pub mod protocol;
pub mod telemetry;
