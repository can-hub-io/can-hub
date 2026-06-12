//! can-hub-web entry point.
//!
//! Phase 1 skeleton: parse the hub socket path and listen address, serve the
//! REST API over axum, and shut down cleanly on SIGINT/SIGTERM. Auth, the
//! embedded SPA assets and the WebSocket telemetry stream land in later phases.

use std::net::SocketAddr;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use can_hub_web::api::{self, AppState};
use can_hub_web::auth::store::AuthStore;
use can_hub_web::auth::Permission;
use can_hub_web::login_limiter::LoginLimiter;
use can_hub_web::telemetry;

const TELEMETRY_INTERVAL: Duration = Duration::from_secs(1);

const DEFAULT_SOCKET_PATH: &str = "/run/can-hub/hub.sock";
const DEFAULT_LISTEN: &str = "127.0.0.1:8080";
const DEFAULT_DB_PATH: &str = "web.db";

struct Options {
    socket_path: String,
    listen: String,
    assets_dir: Option<String>,
    db_path: String,
    add_user: Option<String>,
    secure_cookies: bool,
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

    let store = match AuthStore::open(&options.db_path) {
        Ok(store) => Arc::new(Mutex::new(store)),
        Err(error) => {
            tracing::error!(db = %options.db_path, ?error, "failed to open web.db");
            std::process::exit(1);
        }
    };

    if let Some(name) = options.add_user {
        run_add_user(&store, &name);
        return;
    }

    let socket_path: Arc<str> = Arc::from(options.socket_path.as_str());
    let telemetry_sender = telemetry::channel();
    tokio::spawn(telemetry::run(
        Arc::clone(&socket_path),
        telemetry_sender.clone(),
        TELEMETRY_INTERVAL,
    ));

    let state = AppState {
        socket_path,
        telemetry: telemetry_sender,
        auth: store,
        login_limiter: Arc::new(Mutex::new(LoginLimiter::default())),
        secure_cookies: options.secure_cookies,
    };
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

    let service = app.into_make_service_with_connect_info::<SocketAddr>();
    if let Err(error) = axum::serve(listener, service).with_graceful_shutdown(shutdown_signal()).await {
        tracing::error!(%error, "server error");
        std::process::exit(1);
    }
}

fn parse_options() -> Result<Options, String> {
    let mut socket_path = DEFAULT_SOCKET_PATH.to_string();
    let mut listen = DEFAULT_LISTEN.to_string();
    let mut assets_dir = None;
    let mut db_path = DEFAULT_DB_PATH.to_string();
    let mut add_user = None;
    let mut secure_cookies = false;
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
            "--db" => {
                db_path = args.next().ok_or("--db needs a path")?;
            }
            "--add-user" => {
                add_user = Some(args.next().ok_or("--add-user needs a name")?);
            }
            "--secure-cookies" => {
                secure_cookies = true;
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

    Ok(Options { socket_path, listen, assets_dir, db_path, add_user, secure_cookies })
}

fn print_usage() {
    eprintln!(
        "usage: can-hub-web [--connect <hub.sock>] [--listen <host:port>] [--assets <dir>] [--db <web.db>] [--secure-cookies]\n\
         \x20      can-hub-web --add-user <name> [--db <web.db>]   (password from CANHUB_WEB_PASSWORD or stdin)\n\
         defaults: --connect {DEFAULT_SOCKET_PATH} --listen {DEFAULT_LISTEN} --db {DEFAULT_DB_PATH}\n\
         --assets serves a built SPA (web/ui/dist); omit it in dev (Vite proxy)\n\
         --secure-cookies marks the session cookie Secure (use behind a TLS reverse proxy)"
    );
}

/// Headless admin provisioning: create a user, ensure an `admins` group with
/// every permission, and add the user to it. Password comes from
/// CANHUB_WEB_PASSWORD or, failing that, a line on stdin.
fn run_add_user(store: &Mutex<AuthStore>, name: &str) {
    let password = match std::env::var("CANHUB_WEB_PASSWORD") {
        Ok(value) => value,
        Err(_) => {
            eprint!("password for {name}: ");
            use std::io::Write;
            let _ = std::io::stderr().flush();
            let mut line = String::new();
            if std::io::stdin().read_line(&mut line).is_err() {
                eprintln!("failed to read password");
                std::process::exit(1);
            }
            line.trim_end_matches(['\n', '\r']).to_string()
        }
    };
    if password.is_empty() {
        eprintln!("empty password");
        std::process::exit(1);
    }

    let store = store.lock().unwrap();
    let result = (|| {
        let user_id = store.create_user(name, &password, true)?;
        let group_id = store.ensure_group("admins")?;
        store.set_group_permissions(group_id, &Permission::ALL)?;
        store.add_user_to_group(user_id, group_id)?;
        Ok::<(), can_hub_web::auth::store::StoreError>(())
    })();

    match result {
        Ok(()) => println!("created admin user '{name}'"),
        Err(error) => {
            eprintln!("failed to add user: {error:?}");
            std::process::exit(1);
        }
    }
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
