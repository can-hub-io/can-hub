//! End-to-end telemetry pipe: a mock hub on a unix socket answers the admin
//! samples, `telemetry::run` polls it, and a broadcast subscriber receives a
//! frame with non-zero rates. Exercises the admin client, the poll loop and
//! the rate arithmetic over a real socket (the WebSocket forwarding on top is
//! a thin broadcast→send and is covered by the unit tests).

use std::sync::Arc;
use std::time::Duration;

use can_hub_web::telemetry;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::UnixListener;

const HELLO_SIZE: usize = 76;
const HEADER_SIZE: usize = 4;
const ADMIN_STATUS: u8 = 0x10;
const ADMIN_INTERFACES: u8 = 0x20;

fn status_reply(received: u64) -> Vec<u8> {
    let mut buffer = vec![0u8; 48];
    buffer[0] = 0x11; // ADMIN_STATUS_REPLY
    buffer[2..4].copy_from_slice(&44u16.to_le_bytes());
    buffer[4..6].copy_from_slice(&1u16.to_le_bytes()); // peer_count
    buffer[16..24].copy_from_slice(&received.to_le_bytes()); // frames_received
    buffer
}

fn interfaces_reply(frames_received: u64) -> Vec<u8> {
    let mut buffer = vec![0u8; 8 + 160];
    buffer[0] = 0x21; // ADMIN_INTERFACES_REPLY
    buffer[2..4].copy_from_slice(&164u16.to_le_bytes());
    buffer[4] = 1; // count
    let base = 8;
    buffer[base..base + 4].copy_from_slice(&1u32.to_le_bytes()); // interface_id
    buffer[base + 8..base + 16].copy_from_slice(&frames_received.to_le_bytes());
    let name = b"truck42";
    buffer[base + 16..base + 16 + name.len()].copy_from_slice(name);
    let iface = b"can0";
    buffer[base + 144..base + 144 + iface.len()].copy_from_slice(iface);
    buffer
}

/// Serve one admin connection: read HELLO, then answer each request by type
/// with replies parameterised by `received` (so successive connections report
/// growing counters and the poll loop sees a positive rate).
async fn serve_connection(listener: &UnixListener, received: u64) {
    let (mut stream, _) = listener.accept().await.unwrap();
    let mut hello = [0u8; HELLO_SIZE];
    stream.read_exact(&mut hello).await.unwrap();

    // The poll loop sends ADMIN_STATUS then ADMIN_INTERFACES per tick.
    for _ in 0..2 {
        let mut header = [0u8; HEADER_SIZE];
        if stream.read_exact(&mut header).await.is_err() {
            return;
        }
        let reply = match header[0] {
            ADMIN_STATUS => status_reply(received),
            ADMIN_INTERFACES => interfaces_reply(received),
            other => panic!("unexpected admin request type {other:#x}"),
        };
        stream.write_all(&reply).await.unwrap();
    }
}

#[tokio::test]
async fn poll_loop_broadcasts_rates() {
    let dir = std::env::temp_dir().join(format!("can-hub-web-test-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let socket_path = dir.join("hub.sock");
    let _ = std::fs::remove_file(&socket_path);
    let listener = UnixListener::bind(&socket_path).unwrap();

    // Mock hub: first sample reports 1000 frames, second reports 1100, so the
    // ~100ms interval yields a clearly positive rate.
    let server = tokio::spawn(async move {
        serve_connection(&listener, 1000).await;
        serve_connection(&listener, 1100).await;
    });

    let sender = telemetry::channel();
    let mut receiver = sender.subscribe();
    let path: Arc<str> = Arc::from(socket_path.to_str().unwrap());
    let poller = tokio::spawn(telemetry::run(path, sender, Duration::from_millis(100)));

    let frame = tokio::time::timeout(Duration::from_secs(5), receiver.recv())
        .await
        .expect("a telemetry frame within 5s")
        .expect("broadcast delivered");

    assert_eq!(frame.received, 1100);
    assert!(frame.rates.received_per_s > 0.0, "expected positive rate, got {}", frame.rates.received_per_s);
    assert_eq!(frame.interfaces.len(), 1);
    assert_eq!(frame.interfaces[0].interface_name, "can0");
    assert!(frame.interfaces[0].frames_per_s > 0.0);

    poller.abort();
    server.await.unwrap();
    let _ = std::fs::remove_file(&socket_path);
}
