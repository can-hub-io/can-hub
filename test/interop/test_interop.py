import re
import selectors
import subprocess
from pathlib import Path

import pytest
from suites import AES_SUITES, cpu_has_hardware_aes, offered_suites

CLIENT_TIMEOUT_SECONDS = 15
EXCHANGE_TIMEOUT_SECONDS = 5
TRANSPORTS = ["quic", "tls"]
ALPN = "canhub/0"
HELLO_AS_CLIENT = bytes([0x01, 0x00, 0x48, 0x00, 0x00, 0x02]) + bytes(70)
LIST_FROM_START = bytes([0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00])
EMPTY_LIST_REPLY = bytes([0x05, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00])
NEGOTIATED = re.compile(r"negotiated (TLS_\w+)")


class TestBaselineClientAgainstCurrentHub:
    @pytest.mark.parametrize("transport", TRANSPORTS)
    def test_it_lists_the_catalogue(self, transport, start_hub, current_bin, baseline_bin, tmp_path):
        hub = start_hub(current_bin)

        listed = list_interfaces(baseline_bin, transport, hub.port, tmp_path)

        assert listed.returncode == 0, listed.stdout
        assert negotiated_suite(hub.log()) in offered_suites()


class TestCurrentClientAgainstBaselineHub:
    @pytest.mark.parametrize("transport", TRANSPORTS)
    def test_it_lists_the_catalogue(self, transport, start_hub, current_bin, baseline_bin, tmp_path):
        hub = start_hub(baseline_bin)

        listed = list_interfaces(current_bin, transport, hub.port, tmp_path)

        assert listed.returncode == 0, listed.stdout
        assert negotiated_suite(listed.stdout) in offered_suites()


class TestOpensslPeerOnTls:
    @pytest.mark.parametrize("suite", offered_suites())
    def test_it_talks_under_every_suite_the_build_offers(self, suite, start_hub, current_bin, tmp_path):
        hub = start_hub(current_bin)

        reply = exchange_over_openssl(suite, hub.port, client_identity(tmp_path))

        assert reply == EMPTY_LIST_REPLY
        assert negotiated_suite(hub.log()) == suite

    @pytest.mark.skipif(cpu_has_hardware_aes(), reason="this CPU implements AES, so the build offers it")
    @pytest.mark.parametrize("suite", AES_SUITES)
    def test_it_refuses_aes_without_hardware_aes(self, suite, start_hub, current_bin, tmp_path):
        hub = start_hub(current_bin)

        reply = exchange_over_openssl(suite, hub.port, client_identity(tmp_path))

        assert reply == b""


def list_interfaces(binary_dir: Path, transport: str, port: int, tmp_path: Path) -> subprocess.CompletedProcess:
    argv = [
        str(binary_dir / "can-hub-client"),
        "--state-dir", str(tmp_path / f"client-{binary_dir.name}"),
        "--connect", f"{transport}://127.0.0.1:{port}",
        "list",
    ]
    return subprocess.run(
        argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=CLIENT_TIMEOUT_SECONDS
    )


def negotiated_suite(log: str) -> str:
    found = NEGOTIATED.search(log)
    return found.group(1) if found else ""


def client_identity(tmp_path: Path) -> tuple[Path, Path]:
    certificate = tmp_path / "openssl-client.crt"
    key = tmp_path / "openssl-client.key"
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "ed25519", "-nodes", "-subj", "/CN=interop",
         "-days", "1", "-keyout", str(key), "-out", str(certificate)],
        check=True, capture_output=True,
    )
    return certificate, key


def exchange_over_openssl(suite: str, port: int, identity: tuple[Path, Path]) -> bytes:
    certificate, key = identity
    peer = subprocess.Popen(
        ["openssl", "s_client", "-quiet", "-tls1_3", "-ciphersuites", suite, "-alpn", ALPN,
         "-cert", str(certificate), "-key", str(key), "-connect", f"127.0.0.1:{port}"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )
    try:
        return send_hello_and_list(peer)
    finally:
        peer.kill()
        peer.wait()


def send_hello_and_list(peer: subprocess.Popen) -> bytes:
    try:
        peer.stdin.write(HELLO_AS_CLIENT + LIST_FROM_START)
        peer.stdin.flush()
    except BrokenPipeError:
        return b""
    return read_reply(peer, len(EMPTY_LIST_REPLY))


def read_reply(peer: subprocess.Popen, size: int) -> bytes:
    reply = b""
    selector = selectors.DefaultSelector()
    selector.register(peer.stdout, selectors.EVENT_READ)
    while len(reply) < size and selector.select(timeout=EXCHANGE_TIMEOUT_SECONDS):
        chunk = peer.stdout.read1(size - len(reply))
        if not chunk:
            break
        reply += chunk
    return reply
