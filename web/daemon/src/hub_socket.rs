//! Connecting the synchronous admin client to the hub unix socket with a bound
//! read/write timeout, so a wedged hub cannot strand the blocking task that
//! drives a request (or the telemetry poll loop) forever.

use std::os::unix::net::UnixStream;
use std::time::Duration;

use crate::admin_client::{AdminClient, AdminClientError};

/// How long a single admin read or write may block before it fails. A healthy
/// hub answers in microseconds; this only bounds a hung one.
pub const HUB_IO_TIMEOUT: Duration = Duration::from_secs(2);

/// Connect to the hub admin socket, arm the I/O timeout, and complete the admin
/// HELLO handshake.
pub fn connect(socket_path: &str) -> Result<AdminClient<UnixStream>, AdminClientError> {
    let stream = UnixStream::connect(socket_path)?;
    stream.set_read_timeout(Some(HUB_IO_TIMEOUT))?;
    stream.set_write_timeout(Some(HUB_IO_TIMEOUT))?;
    AdminClient::connect(stream)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;
    use std::os::unix::net::UnixListener;
    use std::thread;
    use std::time::Instant;

    #[test]
    fn wedged_hub_times_out() {
        let dir = std::env::temp_dir().join(format!("canhub-hubsock-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("hub.sock");
        let _ = std::fs::remove_file(&path);
        let listener = UnixListener::bind(&path).unwrap();

        // A hub that accepts the connection and then never answers.
        let accepted = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut sink = [0u8; 64];
            let _ = stream.read(&mut sink);
            thread::sleep(Duration::from_secs(10));
        });

        let path_string = path.to_str().unwrap().to_string();
        let started = Instant::now();
        let mut client = connect(&path_string).unwrap();
        let outcome = client.status();
        assert!(matches!(outcome, Err(AdminClientError::Io(_))));
        assert!(started.elapsed() < HUB_IO_TIMEOUT * 3, "should fail near the timeout, not hang");

        drop(accepted);
        let _ = std::fs::remove_file(&path);
    }
}
