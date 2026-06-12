//! In-memory login rate limiter: bounds failed login attempts per client IP
//! within a sliding window. Time is injected so the policy is unit testable.

use std::collections::HashMap;
use std::net::IpAddr;
use std::time::{Duration, Instant};

const DEFAULT_MAX_FAILURES: u32 = 5;
const DEFAULT_WINDOW: Duration = Duration::from_secs(60);

pub struct LoginLimiter {
    max_failures: u32,
    window: Duration,
    failures: HashMap<IpAddr, Vec<Instant>>,
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

    /// Whether a login attempt from `ip` is allowed: true unless the recent
    /// failures within the window have reached the limit.
    pub fn allowed(&mut self, ip: IpAddr, now: Instant) -> bool {
        self.prune(ip, now);
        self.failures.get(&ip).map_or(0, Vec::len) < self.max_failures as usize
    }

    pub fn record_failure(&mut self, ip: IpAddr, now: Instant) {
        self.prune(ip, now);
        self.failures.entry(ip).or_default().push(now);
    }

    /// A successful login clears the IP's failure history.
    pub fn record_success(&mut self, ip: IpAddr) {
        self.failures.remove(&ip);
    }

    fn prune(&mut self, ip: IpAddr, now: Instant) {
        if let Some(times) = self.failures.get_mut(&ip) {
            times.retain(|&at| now.duration_since(at) < self.window);
            if times.is_empty() {
                self.failures.remove(&ip);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::Ipv4Addr;

    fn ip() -> IpAddr {
        IpAddr::V4(Ipv4Addr::LOCALHOST)
    }

    #[test]
    fn blocks_after_max_failures() {
        let mut limiter = LoginLimiter::new(3, Duration::from_secs(60));
        let now = Instant::now();
        assert!(limiter.allowed(ip(), now));
        for _ in 0..3 {
            limiter.record_failure(ip(), now);
        }
        assert!(!limiter.allowed(ip(), now));
    }

    #[test]
    fn window_expiry_reallows() {
        let mut limiter = LoginLimiter::new(2, Duration::from_secs(60));
        let start = Instant::now();
        limiter.record_failure(ip(), start);
        limiter.record_failure(ip(), start);
        assert!(!limiter.allowed(ip(), start));
        let later = start + Duration::from_secs(61);
        assert!(limiter.allowed(ip(), later));
    }

    #[test]
    fn success_clears_failures() {
        let mut limiter = LoginLimiter::new(2, Duration::from_secs(60));
        let now = Instant::now();
        limiter.record_failure(ip(), now);
        limiter.record_failure(ip(), now);
        assert!(!limiter.allowed(ip(), now));
        limiter.record_success(ip());
        assert!(limiter.allowed(ip(), now));
    }
}
