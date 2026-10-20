//! Real-time telemetry: one poll loop over the hub admin counters turns
//! successive cumulative samples into rates and broadcasts them to all
//! WebSocket subscribers (one poller for N browsers). The hub stays pull-only;
//! no hub-side metrics worker exists.
//!
//! The rate arithmetic is a pure function over two samples so it is unit
//! testable without any async or socket; the poll loop and the broadcast are
//! the thin async shell around it.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};

use serde::Serialize;
use tokio::sync::broadcast;

use crate::protocol::admin::{InterfaceEntry, PeerEntry, Status, TransactionEntry};

/// Cumulative hub counters lifted out of a `Status` sample.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Counters {
    pub received: u64,
    pub forwarded: u64,
    pub dropped: u64,
    pub unroutable: u64,
}

impl From<&Status> for Counters {
    fn from(status: &Status) -> Self {
        Counters {
            received: status.frames_received,
            forwarded: status.frames_forwarded,
            dropped: status.frames_dropped,
            unroutable: status.frames_unroutable,
        }
    }
}

/// Per-second rates between two cumulative samples.
#[derive(Debug, Clone, Copy, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Rates {
    pub received_per_s: f64,
    pub forwarded_per_s: f64,
    pub dropped_per_s: f64,
    pub unroutable_per_s: f64,
}

/// Rate of one cumulative counter over `dt_secs`. A non-positive interval or a
/// counter that went backwards (hub restart resets it) yields 0.
fn rate(previous: u64, current: u64, dt_secs: f64) -> f64 {
    if dt_secs <= 0.0 || current < previous {
        return 0.0;
    }
    (current - previous) as f64 / dt_secs
}

pub fn compute_rates(previous: Counters, current: Counters, dt_secs: f64) -> Rates {
    Rates {
        received_per_s: rate(previous.received, current.received, dt_secs),
        forwarded_per_s: rate(previous.forwarded, current.forwarded, dt_secs),
        dropped_per_s: rate(previous.dropped, current.dropped, dt_secs),
        unroutable_per_s: rate(previous.unroutable, current.unroutable, dt_secs),
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InterfaceRate {
    pub interface_id: u32,
    pub agent_name: String,
    pub interface_name: String,
    pub frames_received: u64,
    pub frames_per_s: f64,
}

/// Per-interface throughput between two interface snapshots, keyed by
/// interface id (a previous sample missing an id contributes 0 rate).
pub fn compute_interface_rates(
    previous: &HashMap<u32, u64>,
    current: &[InterfaceEntry],
    dt_secs: f64,
) -> Vec<InterfaceRate> {
    current
        .iter()
        .map(|entry| {
            let previous_received = previous.get(&entry.interface_id).copied().unwrap_or(entry.frames_received);
            InterfaceRate {
                interface_id: entry.interface_id,
                agent_name: entry.agent_name.clone(),
                interface_name: entry.interface_name.clone(),
                frames_received: entry.frames_received,
                frames_per_s: rate(previous_received, entry.frames_received, dt_secs),
            }
        })
        .collect()
}

/// Transaction figures for the dashboard: how many run now, the steps they
/// complete per second, the share of finished transactions that ended done,
/// and the client frames dropped because another client held the lease.
#[derive(Debug, Clone, Copy, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TransactionKpis {
    pub active: u32,
    pub steps_per_s: f64,
    pub success_ratio: Option<f64>,
    pub lease_drops: u64,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TransactionRow {
    pub interface: String,
    pub client_peer_id: u32,
    pub client_name: Option<String>,
    pub state: &'static str,
    pub exclusive: bool,
    pub completed_steps: u32,
    pub bytes_in_flight: u32,
    pub age_ms: u32,
    pub progress_age_ms: Option<u32>,
}

/// Steps completed so far: those of finished transactions (the hub counter)
/// plus the progress of the running ones, so the rate moves while they run.
pub fn steps_total(status: &Status, transactions: &[TransactionEntry]) -> u64 {
    let running: u64 = transactions.iter().map(|entry| u64::from(entry.completed_steps)).sum();
    status.transactions.steps_completed + running
}

pub fn compute_transaction_kpis(
    previous_steps: u64,
    status: &Status,
    transactions: &[TransactionEntry],
    dt_secs: f64,
) -> TransactionKpis {
    let counters = status.transactions;
    let finished = counters.completed + counters.aborted;
    TransactionKpis {
        active: transactions.len() as u32,
        steps_per_s: rate(previous_steps, steps_total(status, transactions), dt_secs),
        success_ratio: (finished > 0).then(|| counters.completed as f64 / finished as f64),
        lease_drops: counters.lease_frame_drops,
    }
}

/// The transactions in flight, their client named from the peer list when it
/// is still connected.
pub fn transaction_rows(transactions: &[TransactionEntry], peers: &[PeerEntry]) -> Vec<TransactionRow> {
    transactions
        .iter()
        .map(|entry| TransactionRow {
            interface: format!("{}/{}", entry.agent_name, entry.interface_name),
            client_peer_id: entry.client_peer_id,
            client_name: peers
                .iter()
                .find(|peer| peer.peer_id == entry.client_peer_id && entry.client_peer_id != 0)
                .map(|peer| peer.agent_name.clone()),
            state: if entry.state == TRANSACTION_ACTIVE { "active" } else { "pending" },
            exclusive: entry.exclusive,
            completed_steps: entry.completed_steps,
            bytes_in_flight: entry.bytes_in_flight,
            age_ms: entry.age_ms,
            progress_age_ms: entry.progress_age_ms,
        })
        .collect()
}

const TRANSACTION_ACTIVE: u8 = 2;

/// One telemetry broadcast: absolute counters plus the rates since the last
/// sample, for the dashboard.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TelemetryFrame {
    pub received: u64,
    pub forwarded: u64,
    pub dropped: u64,
    pub unroutable: u64,
    pub rates: Rates,
    pub interfaces: Vec<InterfaceRate>,
    pub transactions: TransactionKpis,
    pub transactions_in_flight: Vec<TransactionRow>,
}

/// What the poll loop keeps of a sample to compute the next frame's rates.
struct Sample {
    at: Instant,
    counters: Counters,
    interface_totals: HashMap<u32, u64>,
    steps_total: u64,
}

/// What one admin session reads from the hub per tick.
struct HubSample {
    status: Status,
    interfaces: Vec<InterfaceEntry>,
    transactions: Vec<TransactionEntry>,
    peers: Vec<PeerEntry>,
}

/// Capacity of the broadcast channel; a slow WebSocket that lags beyond this
/// drops intermediate frames (latest-wins is fine for a live dashboard).
const BROADCAST_CAPACITY: usize = 16;

/// Build the broadcast channel that the poll loop fills and the WebSocket
/// handler subscribes to.
pub fn channel() -> broadcast::Sender<Arc<TelemetryFrame>> {
    broadcast::channel(BROADCAST_CAPACITY).0
}

/// Poll loop: every `interval`, open a short admin session, sample status and
/// interfaces, and broadcast a frame with the rates since the previous sample.
/// Connection errors are logged and the previous sample is kept.
pub async fn run(
    socket_path: Arc<str>,
    sender: broadcast::Sender<Arc<TelemetryFrame>>,
    interval: Duration,
) {
    let mut ticker = tokio::time::interval(interval);
    let mut previous: Option<Sample> = None;

    loop {
        ticker.tick().await;
        let hub = match sample_hub(Arc::clone(&socket_path)).await {
            Ok(hub) => hub,
            Err(error) => {
                tracing::debug!(?error, "telemetry sample failed");
                continue;
            }
        };

        let current = Sample {
            at: Instant::now(),
            counters: Counters::from(&hub.status),
            interface_totals: hub.interfaces.iter().map(|entry| (entry.interface_id, entry.frames_received)).collect(),
            steps_total: steps_total(&hub.status, &hub.transactions),
        };
        if let Some(previous) = &previous {
            // Ignore send errors: no subscribers right now is not a failure.
            let _ = sender.send(Arc::new(frame_between(previous, &current, &hub)));
        }
        previous = Some(current);
    }
}

fn frame_between(previous: &Sample, current: &Sample, hub: &HubSample) -> TelemetryFrame {
    let dt = current.at.duration_since(previous.at).as_secs_f64();
    TelemetryFrame {
        received: current.counters.received,
        forwarded: current.counters.forwarded,
        dropped: current.counters.dropped,
        unroutable: current.counters.unroutable,
        rates: compute_rates(previous.counters, current.counters, dt),
        interfaces: compute_interface_rates(&previous.interface_totals, &hub.interfaces, dt),
        transactions: compute_transaction_kpis(previous.steps_total, &hub.status, &hub.transactions, dt),
        transactions_in_flight: transaction_rows(&hub.transactions, &hub.peers),
    }
}

async fn sample_hub(socket_path: Arc<str>) -> Result<HubSample, crate::admin_client::AdminClientError> {
    let joined = tokio::task::spawn_blocking(move || {
        let mut client = crate::hub_socket::connect(&socket_path)?;
        let status = client.status()?;
        let interfaces = client.interfaces()?;
        let transactions = client.transactions()?;
        // Peers only name the clients of running transactions: skip the call when none run.
        let peers = if transactions.is_empty() { Vec::new() } else { client.peers()? };
        Ok(HubSample { status, interfaces, transactions, peers })
    })
    .await;
    // A panic in the blocking task must not kill the telemetry loop: surface it
    // as an I/O error so the caller logs and keeps polling.
    match joined {
        Ok(result) => result,
        Err(join) => Err(crate::admin_client::AdminClientError::Io(std::io::Error::other(join.to_string()))),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn counters(received: u64, forwarded: u64, dropped: u64, unroutable: u64) -> Counters {
        Counters { received, forwarded, dropped, unroutable }
    }

    #[test]
    fn rates_are_delta_over_interval() {
        let previous = counters(1000, 900, 10, 5);
        let current = counters(1200, 1080, 12, 5);
        let rates = compute_rates(previous, current, 2.0);
        assert_eq!(rates.received_per_s, 100.0); // (1200-1000)/2
        assert_eq!(rates.forwarded_per_s, 90.0);
        assert_eq!(rates.dropped_per_s, 1.0);
        assert_eq!(rates.unroutable_per_s, 0.0);
    }

    #[test]
    fn counter_reset_yields_zero_not_negative() {
        let previous = counters(5000, 0, 0, 0);
        let current = counters(10, 0, 0, 0); // hub restarted
        let rates = compute_rates(previous, current, 1.0);
        assert_eq!(rates.received_per_s, 0.0);
    }

    #[test]
    fn nonpositive_interval_yields_zero() {
        let previous = counters(0, 0, 0, 0);
        let current = counters(100, 0, 0, 0);
        assert_eq!(compute_rates(previous, current, 0.0).received_per_s, 0.0);
    }

    fn status_with(completed: u64, aborted: u64, steps_completed: u64) -> Status {
        Status {
            peer_count: 0,
            agent_count: 0,
            client_count: 0,
            interface_count: 0,
            receive_overflows: 0,
            frames_received: 0,
            frames_forwarded: 0,
            frames_dropped: 0,
            frames_unroutable: 0,
            transactions: crate::protocol::admin::TransactionCounters {
                completed,
                aborted,
                steps_completed,
                lease_frame_drops: 2,
                ..Default::default()
            },
        }
    }

    fn running(client_peer_id: u32, completed_steps: u32) -> TransactionEntry {
        TransactionEntry {
            interface_id: 1,
            client_peer_id,
            agent_peer_id: 9,
            txn_id: 4,
            state: 2,
            exclusive: true,
            completed_steps,
            bytes_in_flight: 4096,
            age_ms: 1500,
            progress_age_ms: Some(20),
            agent_name: "truck42".into(),
            interface_name: "can0".into(),
        }
    }

    #[test]
    fn transaction_steps_move_while_a_transaction_runs() {
        let status = status_with(1, 0, 1000);
        let previous_steps = steps_total(&status, &[running(3, 100)]);

        let kpis = compute_transaction_kpis(previous_steps, &status, &[running(3, 500)], 2.0);

        assert_eq!(kpis.active, 1);
        assert_eq!(kpis.steps_per_s, 200.0); // (500-100)/2
        assert_eq!(kpis.lease_drops, 2);
    }

    #[test]
    fn success_ratio_counts_finished_transactions_only() {
        let none_finished = compute_transaction_kpis(0, &status_with(0, 0, 0), &[], 1.0);
        let some_finished = compute_transaction_kpis(0, &status_with(3, 1, 0), &[], 1.0);

        assert_eq!(none_finished.success_ratio, None);
        assert_eq!(some_finished.success_ratio, Some(0.75));
    }

    #[test]
    fn transaction_rows_name_the_client_while_it_is_connected() {
        let peers = vec![PeerEntry {
            peer_id: 3,
            frames_forwarded: 0,
            frames_dropped: 0,
            uptime_seconds: 0,
            role: 2,
            transport_kind: 0,
            agent_name: "flasher".into(),
            fingerprint_hex: String::new(),
            origin: String::new(),
        }];

        let rows = transaction_rows(&[running(3, 10), running(0, 10)], &peers);

        assert_eq!(rows[0].interface, "truck42/can0");
        assert_eq!(rows[0].client_name.as_deref(), Some("flasher"));
        assert_eq!(rows[0].state, "active");
        assert_eq!(rows[1].client_name, None);
    }

    #[test]
    fn interface_rates_diff_by_id() {
        let mut previous = HashMap::new();
        previous.insert(1u32, 100u64);
        let current = vec![
            InterfaceEntry {
                interface_id: 1,
                subscriber_count: 1,
                mode: 0,
                frames_received: 300,
                tx_dropped: 0,
                agent_name: "truck42".into(),
                interface_name: "can0".into(),
            },
            // id 2 unseen before: rate 0, not a spike from zero.
            InterfaceEntry {
                interface_id: 2,
                subscriber_count: 0,
                mode: 0,
                frames_received: 999,
                tx_dropped: 0,
                agent_name: "truck42".into(),
                interface_name: "can1".into(),
            },
        ];
        let rates = compute_interface_rates(&previous, &current, 2.0);
        assert_eq!(rates[0].frames_per_s, 100.0); // (300-100)/2
        assert_eq!(rates[1].frames_per_s, 0.0); // first sighting
        assert_eq!(rates[1].frames_received, 999);
    }
}
