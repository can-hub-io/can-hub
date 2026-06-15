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

use crate::protocol::admin::{InterfaceEntry, Status};

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
    let mut previous: Option<(Instant, Counters, HashMap<u32, u64>)> = None;

    loop {
        ticker.tick().await;
        let sample = sample_hub(Arc::clone(&socket_path)).await;
        let (status, interfaces) = match sample {
            Ok(sample) => sample,
            Err(error) => {
                tracing::debug!(?error, "telemetry sample failed");
                continue;
            }
        };

        let now = Instant::now();
        let counters = Counters::from(&status);
        let interface_totals: HashMap<u32, u64> =
            interfaces.iter().map(|entry| (entry.interface_id, entry.frames_received)).collect();

        if let Some((previous_at, previous_counters, previous_interfaces)) = &previous {
            let dt = now.duration_since(*previous_at).as_secs_f64();
            let frame = TelemetryFrame {
                received: counters.received,
                forwarded: counters.forwarded,
                dropped: counters.dropped,
                unroutable: counters.unroutable,
                rates: compute_rates(*previous_counters, counters, dt),
                interfaces: compute_interface_rates(previous_interfaces, &interfaces, dt),
            };
            // Ignore send errors: no subscribers right now is not a failure.
            let _ = sender.send(Arc::new(frame));
        }

        previous = Some((now, counters, interface_totals));
    }
}

async fn sample_hub(
    socket_path: Arc<str>,
) -> Result<(Status, Vec<InterfaceEntry>), crate::admin_client::AdminClientError> {
    let joined = tokio::task::spawn_blocking(move || {
        let mut client = crate::hub_socket::connect(&socket_path)?;
        let status = client.status()?;
        let interfaces = client.interfaces()?;
        Ok((status, interfaces))
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

    #[test]
    fn interface_rates_diff_by_id() {
        let mut previous = HashMap::new();
        previous.insert(1u32, 100u64);
        let current = vec![
            InterfaceEntry {
                interface_id: 1,
                subscriber_count: 1,
                frames_received: 300,
                tx_dropped: 0,
                agent_name: "truck42".into(),
                interface_name: "can0".into(),
            },
            // id 2 unseen before: rate 0, not a spike from zero.
            InterfaceEntry {
                interface_id: 2,
                subscriber_count: 0,
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
