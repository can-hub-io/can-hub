import threading
import time

import pytest
from can import CanInitializationError, CanOperationError, Message

import canhub._native as native
from canhub.bus import RECEIVE_SLICE_MS, CanHubBus, _FairLock

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


class TestOpenFlags:
    def test_it_asks_the_hub_for_error_frames_by_default(self):
        flags = CanHubBus._open_flags(False, False, False)

        assert flags & native.OPEN_FLAG_ERROR_FRAMES

    def test_it_leaves_error_frames_out_when_ignored(self):
        flags = CanHubBus._open_flags(False, False, True)

        assert not flags & native.OPEN_FLAG_ERROR_FRAMES


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

    def test_it_reports_why_the_connection_failed(self, monkeypatch):
        given_unreachable_hub(monkeypatch, "could not parse the hub url")

        with pytest.raises(CanInitializationError, match="could not parse the hub url"):
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



class TestApplyFilters:
    def test_it_installs_filters_on_the_hub(self, monkeypatch):
        bus, installed = given_bus_recording_filters(monkeypatch)

        bus._apply_filters([{"can_id": 0x123, "can_mask": 0x7FF}])

        assert installed == [(SESSION, 1)]
        assert bus._filters_applied

    def test_it_clears_the_hub_filters_when_reset(self, monkeypatch):
        bus, installed = given_bus_recording_filters(monkeypatch)

        bus._apply_filters(None)

        assert installed == [(SESSION, 0)]
        assert not bus._filters_applied

    def test_it_clears_the_hub_filters_when_there_are_too_many(self, monkeypatch):
        bus, installed = given_bus_recording_filters(monkeypatch)
        too_many = [{"can_id": slot, "can_mask": 0x7FF} for slot in range(native.FILTERS_MAX + 1)]

        bus._apply_filters(too_many)

        assert installed == [(SESSION, 0)]
        assert not bus._filters_applied


class TestConcurrentUse:
    def test_it_lets_a_send_through_while_a_receive_waits(self, monkeypatch):
        bus, hub = given_bus_on_idle_hub(monkeypatch)
        receiver = given_receiving_in_background(bus, hub, timeout=0.5)

        elapsed = send_and_time(bus)

        receiver.join()
        assert elapsed < 0.1
        assert hub.most_concurrent_calls == 1

    def test_it_waits_in_short_slices_until_the_timeout(self, monkeypatch):
        bus, hub = given_bus_on_idle_hub(monkeypatch)

        message, _ = bus._recv_internal(0.05)

        assert message is None
        assert len(hub.receive_timeouts) > 1
        assert max(hub.receive_timeouts) <= RECEIVE_SLICE_MS



class TestFairLock:
    def test_it_serves_a_waiting_thread_before_the_holder_takes_it_back(self):
        lock = _FairLock()
        order = []

        with lock:
            waiter = given_waiting_for(lock, order)
        with lock:
            order.append("holder")

        waiter.join()
        assert order == ["waiter", "holder"]


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


def given_unreachable_hub(monkeypatch, detail="could not reach the hub"):
    monkeypatch.setattr(native.lib, "canhub_connect", lambda config: None)
    monkeypatch.setattr(native.lib, "canhub_last_error", lambda session: detail.encode())


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


def given_bus_recording_filters(monkeypatch):
    installed = []

    def record(session, filters, count):
        installed.append((session, count))
        return native.OK

    bus = make_bare_bus()
    monkeypatch.setattr(native.lib, "canhub_set_filters", record)
    return bus, installed


class IdleHub:
    def __init__(self):
        self.receiving = threading.Event()
        self.receive_timeouts = []
        self.most_concurrent_calls = 0
        self._concurrent_calls = 0
        self._counter = threading.Lock()

    def recv(self, session, frame, timeout_ms):
        self._enter()
        self.receive_timeouts.append(timeout_ms)
        self.receiving.set()
        time.sleep(timeout_ms / 1000)
        self._leave()
        return native.ERR_TIMEOUT

    def send(self, session, frame):
        self._enter()
        self._leave()
        return native.OK

    def _enter(self):
        with self._counter:
            self._concurrent_calls += 1
            self.most_concurrent_calls = max(self.most_concurrent_calls, self._concurrent_calls)

    def _leave(self):
        with self._counter:
            self._concurrent_calls -= 1


def make_bare_bus():
    bus = CanHubBus.__new__(CanHubBus)
    bus._lock = _FairLock()
    bus._session = SESSION
    bus._writable = True
    return bus


def given_bus_on_idle_hub(monkeypatch):
    hub = IdleHub()
    monkeypatch.setattr(native.lib, "canhub_recv", hub.recv)
    monkeypatch.setattr(native.lib, "canhub_send", hub.send)
    return make_bare_bus(), hub


def given_receiving_in_background(bus, hub, timeout):
    receiver = threading.Thread(target=bus._recv_internal, args=(timeout,))
    receiver.start()
    hub.receiving.wait(timeout)
    return receiver


def send_and_time(bus):
    started = time.monotonic()
    bus.send(Message(arbitration_id=0x123, data=b"\x01", is_extended_id=False))
    return time.monotonic() - started


def given_waiting_for(lock, order):
    def wait_then_record():
        with lock:
            order.append("waiter")

    waiter = threading.Thread(target=wait_then_record)
    waiter.start()
    deadline = time.monotonic() + 1.0
    while lock._next_ticket < 2 and time.monotonic() < deadline:
        time.sleep(0.001)
    return waiter
