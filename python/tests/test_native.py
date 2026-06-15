import pytest

import canhub._native as native


class TestListInterfaces:
    def test_it_decodes_each_interface(self, monkeypatch):
        given_hub_lists(monkeypatch, 3)

        interfaces = native.list_interfaces(object(), timeout_ms=1000, capacity=8)

        expect_interface_ids(interfaces, [0, 1, 2])
        assert interfaces[0].agent == b"agent0"
        assert interfaces[0].interface_name == b"if0"

    def test_it_grows_the_buffer_when_truncated(self, monkeypatch):
        given_hub_lists(monkeypatch, 5)

        interfaces = native.list_interfaces(object(), timeout_ms=1000, capacity=2)

        expect_interface_ids(interfaces, [0, 1, 2, 3, 4])

    def test_it_raises_on_hub_error(self, monkeypatch):
        given_hub_errors(monkeypatch, b"boom")

        with pytest.raises(OSError, match="boom"):
            native.list_interfaces(object(), timeout_ms=1000)


def given_hub_lists(monkeypatch, total):
    def fake_canhub_list(session, buffer, capacity, timeout_ms):
        count = min(total, capacity)
        for index in range(count):
            buffer[index].interface_id = index
            buffer[index].agent = f"agent{index}".encode()
            buffer[index].interface_name = f"if{index}".encode()
        return count

    monkeypatch.setattr(native.lib, "canhub_list", fake_canhub_list)


def given_hub_errors(monkeypatch, message):
    monkeypatch.setattr(native.lib, "canhub_list", lambda *arguments: -3)
    monkeypatch.setattr(native.lib, "canhub_last_error", lambda session: message)


def expect_interface_ids(interfaces, ids):
    assert [info.interface_id for info in interfaces] == ids
