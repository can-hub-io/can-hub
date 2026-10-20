from can import Message

import canhub._native as native
from canhub.bus import CanHubBus, _FairLock
from canhub.transaction import Isotp, TransactionAborted, TransactionResult
from canhub.uds import Uds, UdsNegativeResponse, negative_or, to_native_uds

SESSION = 0x1234
REQUEST_ID = 0x7E0
RESPONSE_ID = 0x7E8


class TestToNativeUds:
    def test_it_carries_the_request_id_timings_and_transport(self):
        uds = to_native_uds(REQUEST_ID, Isotp(RESPONSE_ID), p2=0.15, p2_extended=5.0, exclusive=True)

        assert uds.request_id == REQUEST_ID
        assert (uds.p2_ms, uds.p2_extended_ms) == (150, 5000)
        assert uds.flags == native.TXN_FLAG_EXCLUSIVE
        assert uds.transport.response_id == RESPONSE_ID

    def test_it_extends_the_request_id_with_the_transport(self):
        uds = to_native_uds(0x18DA10F1, Isotp(0x18DAF110, is_extended_id=True), 0.15, 5.0, False)

        assert uds.request_id == 0x18DA10F1 | native.CAN_ID_FLAG_EFF


class TestUds:
    def test_it_hands_a_request_over_with_the_answer_it_expects(self, monkeypatch):
        ecu, library = given_uds_on_library(monkeypatch)

        ecu.request(b"\x31\x01\xff\x01", expect=b"\x71\x01\xff\x01")

        assert library.requests == [(b"\x31\x01\xff\x01", b"\x71\x01\xff\x01")]

    def test_it_waits_for_the_window_before_handing_a_request_over(self, monkeypatch):
        ecu, library = given_uds_on_library(monkeypatch)
        library.timeouts = 2

        ecu.request(b"\x10\x02")

        assert library.calls == 3
        assert library.requests == [(b"\x10\x02", b"")]

    def test_it_transfers_data_in_blocks_counted_from_one(self, monkeypatch):
        ecu, library = given_uds_on_library(monkeypatch)

        blocks = ecu.transfer(bytes(10), block_size=4)

        assert blocks == 3
        assert [request[:2] for request, _ in library.requests] == [b"\x36\x01", b"\x36\x02", b"\x36\x03"]
        assert [expect for _, expect in library.requests][2] == b"\x76\x03"
        assert len(library.requests[2][0]) == 4

    def test_it_wraps_the_block_counter_after_255(self, monkeypatch):
        ecu, library = given_uds_on_library(monkeypatch)

        ecu.transfer(bytes(256), block_size=1)

        assert library.requests[254][0][1] == 0xFF
        assert library.requests[255][0][1] == 0x00


class TestNegativeOr:
    def test_it_names_the_refused_service_and_its_code(self):
        aborted = TransactionAborted(result_with(reason="abort_match", data=b"\x7f\x36\x73"))

        refused = negative_or(aborted)

        assert isinstance(refused, UdsNegativeResponse)
        assert (refused.service, refused.code) == (0x36, 0x73)

    def test_it_keeps_any_other_abort_as_it_is(self):
        aborted = TransactionAborted(result_with(reason="timeout", data=b""))

        assert negative_or(aborted) is aborted


class UdsLibrary:
    def __init__(self):
        self.requests = []
        self.calls = 0
        self.timeouts = 0

    def request(self, session, uds, request, length, expect, expect_length, timeout_ms):
        self.calls += 1
        if self.timeouts > 0:
            self.timeouts -= 1
            return native.ERR_TIMEOUT
        self.requests.append((bytes(request[:length]), bytes(expect[:expect_length]) if expect else b""))
        return native.OK


def given_uds_on_library(monkeypatch):
    library = UdsLibrary()
    monkeypatch.setattr(native.lib, "canhub_uds_request", library.request)
    bus = CanHubBus.__new__(CanHubBus)
    bus._lock = _FairLock()
    bus._session = SESSION
    bus._writable = True
    uds = to_native_uds(REQUEST_ID, Isotp(RESPONSE_ID), 0.15, 5.0, False)
    return Uds(bus, uds), library


def result_with(reason, data):
    frame = Message(arbitration_id=RESPONSE_ID, data=data, is_extended_id=False) if data else None
    return TransactionResult(
        done=False,
        reason=reason,
        failing_step=3,
        completed_steps=3,
        answered=frozenset(),
        missing=frozenset(),
        offending_origin=0,
        expected=bytes(8),
        got=bytes(8),
        frame=frame,
    )
