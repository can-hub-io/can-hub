//! In-memory login rate limiter: bounds failed login attempts per key within a
//! sliding window. The login handler limits on two independent keys — the
//! client address and the attempted user name — so neither a single source can
//! lock out everyone nor a single account be brute-forced from many sources.
//! Time is injected so the policy is unit testable.

use std::collections::HashMap;
use std::time::{Duration, Instant};

const DEFAULT_MAX_FAILURES: u32 = 5;
const DEFAULT_WINDOW: Duration = Duration::from_secs(60);

pub struct LoginLimiter {
    max_failures: u32,
    window: Duration,
    failures: HashMap<String, Vec<Instant>>,
}

impl Default for LoginLimiter {
    fn default() -> Self {
        LoginLimiter::new(DEFAULT_MAX_FAILURES, DEFAULT_WINDOW)
    }
}

impl LoginLimiter {
    pub fn new(max_failures: u32, window: Duration) -> Self {
        LoginLimiter { max_failures, window, failures: HashMap::new() }
    }

    /// Whether a login attempt for `key` is allowed: true unless the recent
    /// failures within the window have reached the limit.
    pub fn allowed(&mut self, key: &str, now: Instant) -> bool {
        self.prune(key, now);
        self.failures.get(key).map_or(0, Vec::len) < self.max_failures as usize
    }

    pub fn record_failure(&mut self, key: &str, now: Instant) {
        self.prune(key, now);
        self.failures.entry(key.to_string()).or_default().push(now);
    }

    /// A successful login clears the key's failure history.
    pub fn record_success(&mut self, key: &str) {
        self.failures.remove(key);
    }

    fn prune(&mut self, key: &str, now: Instant) {
        if let Some(times) = self.failures.get_mut(key) {
            times.retain(|&at| now.duration_since(at) < self.window);
            if times.is_empty() {
                self.failures.remove(key);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blocks_after_max_failures() {
        let mut limiter = LoginLimiter::new(3, Duration::from_secs(60));
        let now = Instant::now();
        assert!(limiter.allowed("ip:127.0.0.1", now));
        for _ in 0..3 {
            limiter.record_failure("ip:127.0.0.1", now);
        }
        assert!(!limiter.allowed("ip:127.0.0.1", now));
    }

    #[test]
    fn window_expiry_reallows() {
        let mut limiter = LoginLimiter::new(2, Duration::from_secs(60));
        let start = Instant::now();
        limiter.record_failure("ip:127.0.0.1", start);
        limiter.record_failure("ip:127.0.0.1", start);
        assert!(!limiter.allowed("ip:127.0.0.1", start));
        let later = start + Duration::from_secs(61);
        assert!(limiter.allowed("ip:127.0.0.1", later));
    }

    #[test]
    fn success_clears_failures() {
        let mut limiter = LoginLimiter::new(2, Duration::from_secs(60));
        let now = Instant::now();
        limiter.record_failure("ip:127.0.0.1", now);
        limiter.record_failure("ip:127.0.0.1", now);
        assert!(!limiter.allowed("ip:127.0.0.1", now));
        limiter.record_success("ip:127.0.0.1");
        assert!(limiter.allowed("ip:127.0.0.1", now));
    }

    #[test]
    fn keys_are_independent() {
        let mut limiter = LoginLimiter::new(1, Duration::from_secs(60));
        let now = Instant::now();
        limiter.record_failure("user:alice", now);
        assert!(!limiter.allowed("user:alice", now));
        // A different key (another user, or the client address) is unaffected.
        assert!(limiter.allowed("user:bob", now));
        assert!(limiter.allowed("ip:127.0.0.1", now));
    }
}
