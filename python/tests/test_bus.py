import pytest
from can import CanInitializationError, CanOperationError

import canhub._native as native
from canhub.bus import CanHubBus

SESSION = 0x1234
DETECT_RESULT = [{"interface": "canhub", "channel": "agent/iface"}]


class TestBuildConnectConfig:
    def test_it_rejects_a_malformed_hub_fingerprint(self):
        with pytest.raises(CanInitializationError):
            CanHubBus._build_connect_config(None, None, None, "nothex", None, 5000)

    def test_it_encodes_every_field(self):
        fingerprint = "a" * 64

        config = CanHubBus._build_connect_config(
            "tcp://h:1", "cert.pem", "key.pem", fingerprint, "/state", 1234
        )

        assert config.url == b"tcp://h:1"
        assert config.certificate_path == b"cert.pem"
        assert config.key_path == b"key.pem"
        assert config.hub_fingerprint == fingerprint.encode()
        assert config.state_directory == b"/state"
        assert config.connect_timeout_ms == 1234


class TestToConfig:
    def test_it_namespaces_agent_and_interface_into_a_channel(self):
        config = CanHubBus._to_config(make_interface("can-agent", "vcan0"), {"url": "tcp://h:1"})

        assert config == {"interface": "canhub", "channel": "can-agent/vcan0", "url": "tcp://h:1"}


class TestListInterfaces:
    def test_it_returns_round_trip_config_dicts(self, monkeypatch):
        closed = given_connected_hub(monkeypatch, [make_interface("agent", "i0")])

        configs = CanHubBus.list_interfaces(url="tcp://h:1")

        assert configs == [{"interface": "canhub", "channel": "agent/i0", "url": "tcp://h:1"}]
        assert closed == [SESSION]

    def test_it_omits_connection_keys_that_are_none(self, monkeypatch):
        given_connected_hub(monkeypatch, [make_interface("agent", "i0")])

        configs = CanHubBus.list_interfaces()

        assert configs == [{"interface": "canhub", "channel": "agent/i0"}]

    def test_it_raises_when_the_connection_fails(self, monkeypatch):
        given_unreachable_hub(monkeypatch)

        with pytest.raises(CanInitializationError):
            CanHubBus.list_interfaces(url="tcp://h:1")

    def test_it_closes_the_session_when_listing_fails(self, monkeypatch):
        closed = given_listing_fails(monkeypatch, "nope")

        with pytest.raises(CanOperationError, match="nope"):
            CanHubBus.list_interfaces()

        assert closed == [SESSION]


class TestDetectAvailableConfigs:
    def test_it_reads_the_connection_from_the_environment(self, monkeypatch):
        captured = given_detect_captures_arguments(monkeypatch)
        monkeypatch.setenv("CANHUB_URL", "quic://h:7227")
        monkeypatch.setenv("CANHUB_STATE_DIR", "/state")
        clear_identity_environment(monkeypatch)

        configs = CanHubBus._detect_available_configs()

        assert configs == DETECT_RESULT
        assert captured["url"] == "quic://h:7227"
        assert captured["state_dir"] == "/state"
        assert captured["identity_cert"] is None

    def test_it_falls_back_to_the_local_socket(self, monkeypatch):
        captured = given_detect_captures_arguments(monkeypatch)
        monkeypatch.delenv("CANHUB_URL", raising=False)
        monkeypatch.delenv("CANHUB_STATE_DIR", raising=False)
        clear_identity_environment(monkeypatch)

        CanHubBus._detect_available_configs()

        assert captured["url"] is None

    def test_it_returns_empty_when_listing_raises(self, monkeypatch):
        given_detect_raises(monkeypatch)

        assert CanHubBus._detect_available_configs() == []


def make_interface(agent, name):
    info = native.CanHubInterfaceInfo()
    info.agent = agent.encode()
    info.interface_name = name.encode()
    return info


def given_connected_hub(monkeypatch, interfaces):
    closed = []
    monkeypatch.setattr(native.lib, "canhub_connect", lambda config: SESSION)
    monkeypatch.setattr(native, "list_interfaces", lambda session, timeout_ms: interfaces)
    monkeypatch.setattr(native.lib, "canhub_close", lambda session: closed.append(session))
    return closed


def given_unreachable_hub(monkeypatch):
    monkeypatch.setattr(native.lib, "canhub_connect", lambda config: None)


def given_listing_fails(monkeypatch, message):
    closed = []

    def raise_oserror(session, timeout_ms):
        raise OSError(message)

    monkeypatch.setattr(native.lib, "canhub_connect", lambda config: SESSION)
    monkeypatch.setattr(native, "list_interfaces", raise_oserror)
    monkeypatch.setattr(native.lib, "canhub_close", lambda session: closed.append(session))
    return closed


def given_detect_captures_arguments(monkeypatch):
    captured = {}

    def capture(cls, **kwargs):
        captured.update(kwargs)
        return DETECT_RESULT

    monkeypatch.setattr(CanHubBus, "list_interfaces", classmethod(capture))
    return captured


def given_detect_raises(monkeypatch):
    def raise_runtime(cls, **kwargs):
        raise RuntimeError("hub down")

    monkeypatch.setattr(CanHubBus, "list_interfaces", classmethod(raise_runtime))


def clear_identity_environment(monkeypatch):
    for name in ("CANHUB_IDENTITY_CERT", "CANHUB_IDENTITY_KEY", "CANHUB_HUB_FINGERPRINT"):
        monkeypatch.delenv(name, raising=False)
