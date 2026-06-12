//! can-hub-web entry point.
//!
//! Phase 1 skeleton: parse the hub socket path and listen address, serve the
//! REST API over axum, and shut down cleanly on SIGINT/SIGTERM. Auth, the
//! embedded SPA assets and the WebSocket telemetry stream land in later phases.

use std::sync::Arc;

use can_hub_web::api::{self, AppState};

const DEFAULT_SOCKET_PATH: &str = "/run/can-hub/hub.sock";
const DEFAULT_LISTEN: &str = "127.0.0.1:8080";

struct Options {
    socket_path: String,
    listen: String,
    assets_dir: Option<String>,
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    let options = match parse_options() {
        Ok(options) => options,
        Err(message) => {
            eprintln!("{message}");
            print_usage();
            std::process::exit(2);
        }
    };

    let state = AppState { socket_path: Arc::from(options.socket_path.as_str()) };
    let app = api::router(state, options.assets_dir.map(std::path::PathBuf::from));

    let listener = match tokio::net::TcpListener::bind(&options.listen).await {
        Ok(listener) => listener,
        Err(error) => {
            tracing::error!(listen = %options.listen, %error, "failed to bind");
            std::process::exit(1);
        }
    };

    tracing::info!(
        listen = %options.listen,
        socket = %options.socket_path,
        "can-hub-web {} serving",
        env!("CARGO_PKG_VERSION")
    );

    if let Err(error) = axum::serve(listener, app).with_graceful_shutdown(shutdown_signal()).await {
        tracing::error!(%error, "server error");
        std::process::exit(1);
    }
}

fn parse_options() -> Result<Options, String> {
    let mut socket_path = DEFAULT_SOCKET_PATH.to_string();
    let mut listen = DEFAULT_LISTEN.to_string();
    let mut assets_dir = None;
    let mut args = std::env::args().skip(1);

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--connect" => {
                socket_path = args.next().ok_or("--connect needs a path")?;
            }
            "--listen" => {
                listen = args.next().ok_or("--listen needs host:port")?;
            }
            "--assets" => {
                assets_dir = Some(args.next().ok_or("--assets needs a directory")?);
            }
            "--version" => {
                println!("can-hub-web {}", env!("CARGO_PKG_VERSION"));
                std::process::exit(0);
            }
            "--help" | "-h" => {
                print_usage();
                std::process::exit(0);
            }
            other => return Err(format!("unknown argument: {other}")),
        }
    }

    Ok(Options { socket_path, listen, assets_dir })
}

fn print_usage() {
    eprintln!(
        "usage: can-hub-web [--connect <hub.sock>] [--listen <host:port>] [--assets <dir>]\n\
         defaults: --connect {DEFAULT_SOCKET_PATH} --listen {DEFAULT_LISTEN}\n\
         --assets serves a built SPA (web/ui/dist); omit it in dev (Vite proxy)"
    );
}

async fn shutdown_signal() {
    let ctrl_c = async {
        tokio::signal::ctrl_c().await.expect("install ctrl_c handler");
    };

    #[cfg(unix)]
    let terminate = async {
        tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
            .expect("install SIGTERM handler")
            .recv()
            .await;
    };

    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        _ = ctrl_c => {},
        _ = terminate => {},
    }

    tracing::info!("shutting down");
}
