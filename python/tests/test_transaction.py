import ctypes

import pytest
from can import CanOperationError

import canhub._native as native
from canhub.bus import CanHubBus, _FairLock
from canhub.transaction import (
    SEND_CHUNK_STEPS,
    Advance,
    Origin,
    Pattern,
    Step,
    Template,
    TransactionAborted,
    from_native_result,
    to_native_begin,
    to_native_step,
    to_native_template,
)

SESSION = 0x1234
STANDARD_ID_PATTERN_MASK = 0x7FF | native.CAN_ID_FLAG_EFF
REASON_TIMEOUT = 1


class TestToNativeTemplate:
    def test_it_packs_pattern_bytes_little_endian_under_their_mask(self):
        template = Template(advance_on=Pattern(0x7E8, data=b"\x00\x76", data_mask=b"\x00\xff"), mode=Advance.COUNT)

        definition = to_native_template(3, template)
        advance = definition.patterns[native.TXN_PATTERN_ADVANCE]

        assert definition.template_id == 3
        assert definition.patterns_present == 1 << native.TXN_PATTERN_ADVANCE
        assert advance.can_id == 0x7E8
        assert advance.can_mask == STANDARD_ID_PATTERN_MASK
        assert advance.payload_value == 0x7600
        assert advance.payload_mask == 0xFF00

    def test_it_compares_every_given_byte_when_no_mask_is_given(self):
        template = Template(abort_on=Pattern(0x7E8, data=b"\x03\x7f"))

        definition = to_native_template(0, template)

        assert definition.patterns[native.TXN_PATTERN_ABORT].payload_mask == 0xFFFF

    def test_it_converts_timeout_and_gap_and_flags(self):
        template = Template(mode=Advance.COUNT, timeout=0.05, gap=0.0015, soft=True, abort_on_mismatch=True)

        definition = to_native_template(0, template)

        assert definition.timeout_ms == 50
        assert definition.gap_us == 1500
        assert definition.flags == native.TXN_TEMPLATE_SOFT | native.TXN_TEMPLATE_MISMATCH_ABORT

    def test_it_rejects_an_id_beyond_the_table(self):
        with pytest.raises(ValueError):
            to_native_template(native.TXN_TEMPLATES_MAX, Template())


class TestToNativeStep:
    def test_it_sends_a_frame_with_its_payload(self):
        native_step = to_native_step(Step(template=1, arbitration_id=0x18DA10F1, data=b"\x02\x36\x01", is_extended_id=True))

        assert native_step.template_id == 1
        assert native_step.can_id == 0x18DA10F1 | native.CAN_ID_FLAG_EFF
        assert native_step.length == 3
        assert bytes(native_step.payload[:3]) == b"\x02\x36\x01"
        assert native_step.flags == 0

    def test_it_only_waits_when_the_step_has_no_frame(self):
        native_step = to_native_step(Step(template=2, expect=b"\x01\xde\xc0"))

        assert native_step.flags == native.TXN_STEP_EXPECT_ONLY | native.TXN_STEP_EXPECT_VALUE
        assert native_step.expect_value == 0xC0DE01


class TestToNativeBegin:
    def test_it_reads_origins_from_the_payload_when_an_offset_is_given(self):
        request = to_native_begin((1, 2), Origin(mask=0xFF, payload_offset=2), True)

        assert request.flags == native.TXN_FLAG_EXCLUSIVE
        assert request.origin_source == native.TXN_ORIGIN_PAYLOAD
        assert request.origin_payload_offset == 2
        assert request.roster_count == 2
        assert list(request.roster[:2]) == [1, 2]

    def test_it_needs_an_origin_for_a_roster(self):
        with pytest.raises(ValueError):
            to_native_begin((1, 2), None, False)


class TestFromNativeResult:
    def test_it_names_who_answered_and_who_is_missing(self):
        report = native.CanHubTxnResult()
        report.state = 1
        report.reason = REASON_TIMEOUT
        report.failing_seq = 16
        report.seen_bitmap = 0b101

        result = from_native_result(report, (10, 11, 12, 13))

        assert not result.done
        assert result.reason == "timeout"
        assert result.failing_step == 16
        assert result.answered == frozenset({10, 12})
        assert result.missing == frozenset({11, 13})


class TestTransaction:
    def test_it_hands_every_step_over_in_chunks(self, monkeypatch):
        bus, hub = given_bus_on_transaction_hub(monkeypatch)
        transaction = given_transaction(bus)

        sent = transaction.send(Step(arbitration_id=0x100 + index) for index in range(SEND_CHUNK_STEPS + 10))

        assert sent == SEND_CHUNK_STEPS + 10
        assert hub.sent_ids[0] == 0x100
        assert hub.sent_ids[-1] == 0x100 + SEND_CHUNK_STEPS + 9
        assert len(hub.sent_ids) == SEND_CHUNK_STEPS + 10

    def test_it_waits_for_credit_when_the_library_takes_only_part(self, monkeypatch):
        bus, hub = given_bus_on_transaction_hub(monkeypatch)
        transaction = given_transaction(bus)
        hub.steps_per_call = 3

        sent = transaction.send([Step(arbitration_id=0x100 + index) for index in range(10)])

        assert sent == 10
        assert hub.sent_ids == [0x100 + index for index in range(10)]

    def test_it_raises_with_the_report_when_the_agent_aborts(self, monkeypatch):
        bus, hub = given_bus_on_transaction_hub(monkeypatch)
        transaction = given_transaction(bus)
        hub.abort_at_send = True

        with pytest.raises(TransactionAborted) as aborted:
            transaction.send([Step(arbitration_id=0x100)])

        assert aborted.value.result.reason == "timeout"

    def test_it_ends_and_reports_a_finished_transaction(self, monkeypatch):
        bus, hub = given_bus_on_transaction_hub(monkeypatch)
        transaction = given_transaction(bus)

        result = transaction.wait()

        assert hub.ended
        assert result.done

    def test_it_cancels_at_the_agent_when_the_block_raises(self, monkeypatch):
        bus, hub = given_bus_on_transaction_hub(monkeypatch)

        with pytest.raises(KeyboardInterrupt):
            with given_transaction(bus):
                raise KeyboardInterrupt

        assert hub.aborted

    def test_it_refuses_to_start_on_a_read_only_bus(self, monkeypatch):
        bus, _ = given_bus_on_transaction_hub(monkeypatch)
        bus._writable = False

        with pytest.raises(CanOperationError):
            bus.transaction()


class TransactionHub:
    def __init__(self):
        self.sent_ids = []
        self.steps_per_call = None
        self.abort_at_send = False
        self.ended = False
        self.aborted = False

    def begin(self, session, request, timeout_ms):
        return native.OK

    def send(self, session, steps, count, timeout_ms):
        if self.abort_at_send:
            return native.ERR_TXN_ABORTED
        taken = count if self.steps_per_call is None else min(count, self.steps_per_call)
        self.sent_ids.extend(steps[index].can_id for index in range(taken))
        return taken

    def end(self, session, timeout_ms):
        self.ended = True
        return native.OK

    def wait(self, session, report, timeout_ms):
        if self.abort_at_send:
            ctypes.cast(report, ctypes.POINTER(native.CanHubTxnResult)).contents.reason = REASON_TIMEOUT
            return native.ERR_TXN_ABORTED
        return native.OK

    def abort(self, session):
        self.aborted = True
        return native.OK


def given_bus_on_transaction_hub(monkeypatch):
    hub = TransactionHub()
    monkeypatch.setattr(native.lib, "canhub_txn_begin", hub.begin)
    monkeypatch.setattr(native.lib, "canhub_txn_send", hub.send)
    monkeypatch.setattr(native.lib, "canhub_txn_end", hub.end)
    monkeypatch.setattr(native.lib, "canhub_txn_wait", hub.wait)
    monkeypatch.setattr(native.lib, "canhub_txn_abort", hub.abort)
    bus = CanHubBus.__new__(CanHubBus)
    bus._lock = _FairLock()
    bus._session = SESSION
    bus._writable = True
    return bus, hub


def given_transaction(bus):
    return bus.transaction()
