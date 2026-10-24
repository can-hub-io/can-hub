import ctypes

import pytest
from can import CanOperationError

import canhub._native as native
from canhub.bus import CanHubBus, _FairLock
from canhub.transaction import (
    IsotpLink,
    Keepalive,
    Masked,
    Match,
    Response,
    ScriptError,
    TransactionAborted,
    from_native_report,
    to_native_link,
    to_native_match,
    to_native_options,
    to_native_response,
    Options,
)

SESSION = 0x1234
TRANSACTION = 0x5678
REASON_NONE = 0
REASON_TIMEOUT = 1
SCRIPT_TEXT = "send 0x123 data 01\n"
SCRIPT_PATH = "flash.txn"
SCRIPT_TOTAL_STEPS = 40


class TestToNativeMatch:
    def test_it_compares_the_given_bytes_whole_or_under_their_mask(self):
        match = Match(0x7E8, data={1: 0x76, 2: Masked(0xF0, 0x30)})

        native_match = to_native_match(match)

        assert native_match.id.value == 0x7E8
        assert not native_match.id.extended
        assert native_match.id_ignored_bits == 0
        assert (native_match.bytes[1].mask, native_match.bytes[1].value) == (0xFF, 0x76)
        assert (native_match.bytes[2].mask, native_match.bytes[2].value) == (0xF0, 0x30)
        assert native_match.bytes[0].mask == 0

    def test_it_carries_an_extended_id_and_its_ignored_bits(self):
        native_match = to_native_match(Match(0x18DAF100, ignored_bits=0xFF, extended=True))

        assert native_match.id.extended
        assert native_match.id_ignored_bits == 0xFF

    def test_it_refuses_a_byte_beyond_the_eighth(self):
        with pytest.raises(ValueError):
            to_native_match(Match(0x7E8, data={8: 0x01}))


class TestToNativeResponse:
    def test_it_converts_the_times_and_flags(self):
        response = Response(
            Match(0x7E8, data={1: 0x76}),
            timeout=0.05,
            pending=Match(0x7E8, data={1: 0x7F, 3: 0x78}),
            timeout_after_pending=5.0,
            continue_on_timeout=True,
            on_mismatch_abort=True,
        )

        native_response = to_native_response(response, [])

        assert native_response.timeout_ms == 50
        assert native_response.timeout_after_pending_ms == 5000
        assert native_response.pending.contents.bytes[3].value == 0x78
        assert not native_response.failure
        assert native_response.continue_on_timeout
        assert native_response.on_mismatch_abort

    def test_it_carries_the_nodes_told_by_a_data_byte(self):
        response = Response(Match(0x650), timeout=1.0, nodes=range(32), allowed_missing=8, node_byte=2)

        native_response = to_native_response(response, [])

        assert native_response.node_count == 32
        assert native_response.nodes[31] == 31
        assert native_response.allowed_missing == 8
        assert native_response.node_from == native.TXN_NODE_IN_BYTE
        assert native_response.node_byte == 2

    def test_it_tells_nodes_by_the_id_when_no_byte_is_given(self):
        response = Response(Match(0x700, ignored_bits=0x0F), timeout=0.5, nodes=range(16))

        native_response = to_native_response(response, [])

        assert native_response.node_from == native.TXN_NODE_IN_ID


class TestToNativeLink:
    def test_it_carries_the_isotp_parameters(self):
        link = IsotpLink(0x7E0, 0x7E8, tx_address=0x10, rx_address=0xF1, padding=0xCC, fd=True, block_size=8, st_min=2)

        native_link = to_native_link(link)

        assert (native_link.tx_id.value, native_link.rx_id.value) == (0x7E0, 0x7E8)
        assert native_link.address_byte
        assert (native_link.tx_address, native_link.rx_address) == (0x10, 0xF1)
        assert native_link.padded
        assert native_link.padding == 0xCC
        assert native_link.fd
        assert native_link.fd_data_length == native.FRAME_PAYLOAD_MAX
        assert (native_link.block_size, native_link.st_min) == (8, 2)

    def test_it_uses_one_address_both_ways_for_mixed_addressing(self):
        native_link = to_native_link(IsotpLink(0x7E0, 0x7E8, tx_address=0x42))

        assert (native_link.tx_address, native_link.rx_address) == (0x42, 0x42)

    def test_it_pads_nothing_unless_told(self):
        native_link = to_native_link(IsotpLink(0x7E0, 0x7E8))

        assert not native_link.padded
        assert not native_link.address_byte


class TestToNativeOptions:
    def test_it_carries_exclusive_the_idle_timeout_and_the_keepalive(self):
        keep = []
        options = Options(exclusive=True, idle_timeout=60.0, keepalive=Keepalive(0x7DF, b"\x02\x3e\x80", every=2.0))

        native_options = to_native_options(options, native.RosterCallback(), keep)

        assert native_options.exclusive
        assert native_options.idle_timeout_ms == 60000
        assert native_options.keepalive.contents.every_ms == 2000
        assert native_options.keepalive.contents.length == 3

    def test_it_asks_for_the_agent_default_idle_timeout(self):
        native_options = to_native_options(Options(), native.RosterCallback(), [])

        assert native_options.idle_timeout_ms == 5000


class TestFromNativeReport:
    def test_it_names_who_was_missing_at_the_failing_step(self):
        report = native.CanHubTxnReport(reason=REASON_TIMEOUT, failing_step=4)
        report.failing_roster.missing_count = 2
        report.failing_roster.missing[0] = 11
        report.failing_roster.missing[1] = 13

        result = from_native_report(report)

        assert not result.done
        assert result.reason == "timeout"
        assert result.failing_step == 4
        assert result.missing == frozenset({11, 13})

    def test_it_reads_the_frame_that_ended_the_transaction(self):
        report = native.CanHubTxnReport(has_frame=True)
        report.frame.id = native.CanHubCanId(0x18DAF110, True)
        report.frame.length = 1
        report.frame.payload[0] = 0x7F

        result = from_native_report(report)

        assert result.frame.arbitration_id == 0x18DAF110
        assert result.frame.is_extended_id
        assert result.frame.data == b"\x7f"


class TestTransaction:
    def test_it_creates_nothing_at_the_agent_until_the_first_step(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)

        given_transaction(bus)

        assert library.created
        assert library.steps == []

    def test_it_hands_a_frame_over_with_its_response(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)
        transaction = given_transaction(bus)

        transaction.send(0x7E0, b"\x02\x10\x03", Response(Match(0x7E8, data={1: 0x50}), timeout=0.05))

        assert library.steps == [(0x7E0, False, b"\x02\x10\x03", 0x7E8, 50)]

    def test_it_retries_a_step_the_window_did_not_take_in_time(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)
        transaction = given_transaction(bus)
        library.step_timeouts = 2

        transaction.send(0x123, b"\x01")

        assert library.step_calls == 3
        assert len(library.steps) == 1

    def test_it_waits_without_sending(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)
        transaction = given_transaction(bus)

        transaction.expect(Response(Match(0x7E8), timeout=0.1))

        assert library.steps == [(0, False, None, 0x7E8, 100)]

    def test_it_sends_an_isotp_message_on_the_link_tx_id(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)
        transaction = given_transaction(bus)

        transaction.send_message(IsotpLink(0x7E0, 0x7E8), bytes(500), Response(Match(0x7E8), timeout=0.05))

        assert library.steps[0][0] == 0x7E0
        assert len(library.steps[0][2]) == 500
        assert library.isotp_steps == 1

    def test_it_reports_a_finished_transaction(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)

        with given_transaction(bus) as transaction:
            transaction.send(0x123, b"\x01")

        assert library.finished
        assert transaction.result.done

    def test_it_waits_for_the_outcome_in_slices(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)
        transaction = given_transaction(bus)
        library.wait_timeouts = 2

        transaction.finish()

        assert library.wait_calls == 3

    def test_it_raises_with_the_report_when_the_agent_aborts(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)
        transaction = given_transaction(bus)
        library.abort_at_step = True

        with pytest.raises(TransactionAborted) as aborted:
            transaction.send(0x123, b"\x01")

        assert aborted.value.result.reason == "timeout"

    def test_it_raises_why_the_library_refused_a_step(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)
        transaction = given_transaction(bus)
        library.refuse_step = True

        with pytest.raises(CanOperationError) as refused:
            transaction.send(0x123, b"\x01")

        assert str(refused.value) == "step 0: a response needs timeout_ms"

    def test_it_cancels_at_the_agent_when_the_block_raises(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)

        with pytest.raises(KeyboardInterrupt):
            with given_transaction(bus):
                raise KeyboardInterrupt

        assert library.aborted
        assert library.finished

    def test_it_hands_the_roster_results_to_on_roster(self, monkeypatch):
        bus, library = given_bus_on_library(monkeypatch)
        results = []
        transaction = bus.transaction(on_roster=results.append)
        library.roster_report = (3, [5, 9])

        transaction.send(0x7FF, b"\x01")

        assert results[0].step == 3
        assert results[0].missing == frozenset({5, 9})

    def test_it_refuses_to_start_on_a_read_only_bus(self, monkeypatch):
        bus, _ = given_bus_on_library(monkeypatch)
        bus._writable = False

        with pytest.raises(CanOperationError):
            bus.transaction()


class TestRunScript:
    def test_it_runs_a_script_given_as_text_against_a_base_directory(self, monkeypatch):
        bus, runner = given_bus_on_script_runner(monkeypatch)

        result = bus.run_txn_script(SCRIPT_TEXT, base_directory="/images")

        assert runner.text == SCRIPT_TEXT.encode()
        assert runner.base_directory == b"/images"
        assert result.done

    def test_it_runs_a_script_file(self, monkeypatch):
        bus, runner = given_bus_on_script_runner(monkeypatch)

        bus.run_txn_script_file(SCRIPT_PATH)

        assert runner.path == SCRIPT_PATH.encode()

    def test_it_raises_the_script_error_with_its_line(self, monkeypatch):
        bus, runner = given_bus_on_script_runner(monkeypatch)
        runner.status = native.ERR_ARGUMENT

        with pytest.raises(ScriptError) as failure:
            bus.run_txn_script_file(SCRIPT_PATH)

        assert failure.value.line == 7
        assert str(failure.value) == "flash.txn:7: no response has this name"

    def test_it_raises_with_the_report_when_the_agent_aborts(self, monkeypatch):
        bus, runner = given_bus_on_script_runner(monkeypatch)
        runner.status = native.ERR_TXN_ABORTED
        runner.reason = REASON_TIMEOUT

        with pytest.raises(TransactionAborted) as aborted:
            bus.run_txn_script(SCRIPT_TEXT)

        assert aborted.value.result.reason == "timeout"

    def test_it_passes_progress_with_the_total_steps(self, monkeypatch):
        bus, runner = given_bus_on_script_runner(monkeypatch)
        reports = []

        bus.run_txn_script(SCRIPT_TEXT, progress=lambda progress, total: reports.append((progress.completed_steps, total)))

        assert reports == [(runner.completed_steps, SCRIPT_TOTAL_STEPS)]
        assert runner.kept_going

    def test_it_cancels_when_progress_returns_false(self, monkeypatch):
        bus, runner = given_bus_on_script_runner(monkeypatch)

        bus.run_txn_script(SCRIPT_TEXT, progress=lambda progress, total: False)

        assert not runner.kept_going

    def test_it_cancels_and_reraises_what_progress_raised(self, monkeypatch):
        bus, runner = given_bus_on_script_runner(monkeypatch)

        with pytest.raises(KeyboardInterrupt):
            bus.run_txn_script(SCRIPT_TEXT, progress=interrupt)

        assert not runner.kept_going

    def test_it_hands_the_roster_results_to_on_roster(self, monkeypatch):
        bus, runner = given_bus_on_script_runner(monkeypatch)
        results = []
        runner.roster_report = (2, [3])

        bus.run_txn_script(SCRIPT_TEXT, on_roster=results.append)

        assert results[0].step == 2
        assert results[0].missing == frozenset({3})


class FakeLibrary:
    def __init__(self):
        self.created = False
        self.options = None
        self.steps = []
        self.step_calls = 0
        self.step_timeouts = 0
        self.isotp_steps = 0
        self.abort_at_step = False
        self.refuse_step = False
        self.roster_report = None
        self.wait_calls = 0
        self.wait_timeouts = 0
        self.aborted = False
        self.finished = False
        self.error = b""

    def create(self, session, options):
        self.created = True
        self.options = native.CanHubTxnOptions.from_buffer_copy(ctypes.cast(options, ctypes.POINTER(native.CanHubTxnOptions)).contents)
        return TRANSACTION

    def try_step(self, transaction, step, timeout_ms):
        self.step_calls += 1
        if self.step_timeouts > 0:
            self.step_timeouts -= 1
            return native.ERR_TIMEOUT
        if self.abort_at_step:
            return native.ERR_TXN_ABORTED
        if self.refuse_step:
            self.error = b"step 0: a response needs timeout_ms"
            return native.ERR_ARGUMENT
        self.steps.append(recorded_step(ctypes.cast(step, ctypes.POINTER(native.CanHubTxnStep)).contents))
        if ctypes.cast(step, ctypes.POINTER(native.CanHubTxnStep)).contents.isotp:
            self.isotp_steps += 1
        if self.roster_report is not None:
            report_roster(self.options.on_roster, *self.roster_report)
        return native.OK

    def progress(self, transaction, progress):
        return native.OK

    def abort(self, transaction):
        self.aborted = True
        return native.OK

    def wait(self, transaction, timeout_ms):
        self.wait_calls += 1
        if self.wait_timeouts > 0:
            self.wait_timeouts -= 1
            return native.ERR_TIMEOUT
        return native.OK

    def finish(self, transaction, report):
        self.finished = True
        contents = ctypes.cast(report, ctypes.POINTER(native.CanHubTxnReport)).contents
        if self.abort_at_step or self.aborted:
            contents.reason = REASON_TIMEOUT
            return native.ERR_TXN_ABORTED
        contents.done = True
        return native.OK

    def last_error(self, session):
        return self.error


class ScriptRunner:
    def __init__(self):
        self.status = native.OK
        self.reason = REASON_NONE
        self.completed_steps = 12
        self.text = None
        self.base_directory = None
        self.path = None
        self.kept_going = None
        self.roster_report = None

    def run(self, session, text, base_directory, options, report):
        self.text = text
        self.base_directory = base_directory
        return self.report(options, report)

    def run_file(self, session, path, options, report):
        self.path = path
        return self.report(options, report)

    def report(self, options, report):
        callbacks = ctypes.cast(options, ctypes.POINTER(native.CanHubTxnScriptOptions)).contents
        contents = ctypes.cast(report, ctypes.POINTER(native.CanHubTxnReport)).contents
        progress = native.CanHubTxnProgress(completed_steps=self.completed_steps)
        self.kept_going = callbacks.on_progress(None, ctypes.pointer(progress), SCRIPT_TOTAL_STEPS) != 0
        if self.roster_report is not None:
            report_roster(callbacks.on_roster, *self.roster_report)
        contents.done = self.status == native.OK
        contents.reason = self.reason
        return self.status

    def last_error(self, session):
        return b"flash.txn:7: no response has this name"


def recorded_step(step):
    data = ctypes.string_at(step.data, step.length) if step.data else None
    response = step.response.contents if step.response else None
    return (
        step.id.value,
        step.id.extended,
        data,
        response.success.id.value if response else None,
        response.timeout_ms if response else None,
    )


def report_roster(callback, step, missing):
    result = native.CanHubTxnRosterResult(step=step, missing_count=len(missing))
    result.missing[: len(missing)] = missing
    callback(None, ctypes.pointer(result))


def given_bus_on_library(monkeypatch):
    library = FakeLibrary()
    monkeypatch.setattr(native.lib, "canhub_txn_create", library.create)
    monkeypatch.setattr(native.lib, "canhub_txn_try_step", library.try_step)
    monkeypatch.setattr(native.lib, "canhub_txn_progress", library.progress)
    monkeypatch.setattr(native.lib, "canhub_txn_abort", library.abort)
    monkeypatch.setattr(native.lib, "canhub_txn_wait", library.wait)
    monkeypatch.setattr(native.lib, "canhub_txn_finish", library.finish)
    monkeypatch.setattr(native.lib, "canhub_last_error", library.last_error)
    return given_bus(), library


def given_bus_on_script_runner(monkeypatch):
    runner = ScriptRunner()
    monkeypatch.setattr(native.lib, "canhub_txn_run_script", runner.run)
    monkeypatch.setattr(native.lib, "canhub_txn_run_script_file", runner.run_file)
    monkeypatch.setattr(native.lib, "canhub_last_error", runner.last_error)
    return given_bus(), runner


def given_bus():
    bus = CanHubBus.__new__(CanHubBus)
    bus._lock = _FairLock()
    bus._session = SESSION
    bus._writable = True
    return bus


def given_transaction(bus):
    return bus.transaction()


def interrupt(progress, total_steps):
    raise KeyboardInterrupt
