//! can-hub-web: web admin panel daemon.
//!
//! The daemon is a sibling consumer of the hub admin plane: it speaks the
//! binary admin protocol (message family 0x10..=0x2B) over the hub unix
//! socket and exposes a REST API plus a React UI to browsers. This crate is
//! organised so the protocol codecs stay dependency-free and unit-testable on
//! their own, ahead of the async/HTTP layers built in later phases.

pub mod admin_client;
pub mod api;
pub mod protocol;
