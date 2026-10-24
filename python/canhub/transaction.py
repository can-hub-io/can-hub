"""Transactions: request/response sequences the agent runs at the bus.

A transaction moves the gating loop to the agent, so each exchange costs a
bus round trip instead of a network one (UDS flashing, broadcast upgrades;
see doc/libcanhub.md "Transactions"). The vocabulary is the C API's and the
scripts' (doc/transaction-scripts.md). Open the bus with ``reliable=True``::

    positive = Response(Match(0x7E8, data={1: 0x50}), timeout=0.05)

    with bus.transaction() as transaction:
        transaction.send(0x7E0, b"\\x02\\x10\\x03", positive)
    result = transaction.result     # TransactionAborted is raised otherwise

or describe it in a script and run that::

    result = bus.run_txn_script_file("flash.txn")

A match compares the id, except the bits in ``ignored_bits``, and the first 8
data bytes given in ``data`` (index to value, or to :class:`Masked`).
"""

import ctypes
import os
import re
import time
from dataclasses import dataclass, field
from typing import Callable, FrozenSet, Mapping, Optional, Sequence, Union

from can import CanOperationError, Message

from . import _native as native

MATCH_BYTES = 8
SEND_SLICE_MS = 50
WAIT_SLICE_MS = 50
CLOSE_WAIT_MS = 1000
MICROSECONDS_PER_SECOND = 1_000_000
MILLISECONDS_PER_SECOND = 1000
BYTE_MASK = 0xFF
SCRIPT_ERROR_PATTERN = re.compile(r"^.*?:(\d+): (.*)$")


@dataclass(frozen=True)
class Masked:
    """A data byte compared under ``mask``: ``byte & mask == value``."""

    mask: int
    value: int


@dataclass(frozen=True)
class Match:
    """A frame whose id equals ``id`` except in ``ignored_bits`` (0: the exact
    id) and whose first 8 data bytes meet ``data``. A standard and an
    extended id never match each other."""

    id: int
    data: Mapping[int, Union[int, Masked]] = field(default_factory=dict)
    ignored_bits: int = 0
    extended: bool = False


@dataclass(frozen=True)
class Response:
    """What a step waits for: ``success`` completes it, ``failure`` aborts the
    transaction, ``pending`` (a "busy, wait" answer, checked first) restarts
    the wait with ``timeout_after_pending``. With ``nodes``, the step waits for
    an answer from each (``allowed_missing`` may stay silent); a node is told
    by the bits ``success.ignored_bits`` leaves free, or by data byte
    ``node_byte``. ``continue_on_timeout`` moves on when the time runs out
    instead of aborting. Times are in seconds."""

    success: Match
    timeout: float
    failure: Optional[Match] = None
    pending: Optional[Match] = None
    timeout_after_pending: Optional[float] = None
    nodes: Sequence[int] = ()
    allowed_missing: int = 0
    node_byte: Optional[int] = None
    continue_on_timeout: bool = False
    on_mismatch_abort: bool = False


@dataclass(frozen=True)
class IsotpLink:
    """Two ends of ISO 15765-2: the agent sends each message on ``tx_id``,
    segmented, and reassembles the answer on ``rx_id``. ``tx_address`` and
    ``rx_address`` start every frame for extended or mixed addressing;
    ``padding`` fills short frames; ``fd`` sends CAN FD frames of
    ``fd_data_length`` bytes; ``block_size`` and ``st_min`` (raw ISO 15765-2
    value) go in the flow control the agent answers with."""

    tx_id: int
    rx_id: int
    extended: bool = False
    tx_address: Optional[int] = None
    rx_address: Optional[int] = None
    padding: Optional[int] = None
    fd: bool = False
    bitrate_switch: bool = False
    fd_data_length: int = native.FRAME_PAYLOAD_MAX
    block_size: int = 0
    st_min: int = 0


@dataclass(frozen=True)
class Keepalive:
    """A frame the agent sends every ``every`` seconds while it waits for
    steps, for instance to keep a device session open across a slow link."""

    id: int
    data: bytes
    every: float
    extended: bool = False


@dataclass(frozen=True)
class RosterResult:
    """A step waiting for nodes that ended without all of them: ``step`` is
    its position in the transaction, ``missing`` the nodes that had not
    answered."""

    step: int
    missing: FrozenSet[int]


@dataclass(frozen=True)
class TransactionProgress:
    sent_steps: int
    completed_steps: int
    bytes_in_flight: int
    window_bytes: int


@dataclass(frozen=True)
class TransactionResult:
    """How the transaction ended. ``missing`` names who had not answered when
    the failing step waited for nodes; ``expected`` and ``got`` are the data
    bytes of a value mismatch; ``frame`` is the frame that ended it, if any."""

    done: bool
    reason: str
    failing_step: int
    completed_steps: int
    missing: FrozenSet[int]
    offending_node: int
    expected: bytes
    got: bytes
    frame: Optional[Message]


class TransactionAborted(CanOperationError):
    """The transaction ended before its last step; ``result`` says why."""

    def __init__(self, result: TransactionResult):
        super().__init__(f"transaction aborted at step {result.failing_step}: {result.reason}")
        self.result = result


class ScriptError(CanOperationError):
    """The script does not compile or cannot be read; ``line`` is 0 when the
    failure is not tied to a line."""

    def __init__(self, message: str, line: int):
        super().__init__(message)
        self.line = line


class Transaction:
    """A transaction on one bus; use it as a context manager, which finishes
    it (raising :class:`TransactionAborted`) or cancels it when the block
    raises. Nothing is sent until the first step. ``on_roster`` runs inside
    the transaction's calls and must not use the bus."""

    def __init__(self, bus, options: "Options"):
        self._bus = bus
        self._keep = []
        self._result = None
        self._on_roster = options.on_roster
        self._roster_callback = native.RosterCallback(self._report_roster)
        native_options = to_native_options(options, self._roster_callback, self._keep)
        with bus._lock:
            self._handle = native.lib.canhub_txn_create(bus._session, ctypes.byref(native_options))
        if not self._handle:
            raise CanOperationError(native.last_error(bus._session))

    def __enter__(self) -> "Transaction":
        return self

    def __exit__(self, exception_type, exception, traceback) -> None:
        if self._handle is None:
            return
        if exception_type is None:
            self.finish()
            return
        self.abort()
        self._release()

    @property
    def result(self) -> Optional[TransactionResult]:
        return self._result

    def send(
        self,
        id: int,
        data: bytes,
        response: Optional[Response] = None,
        *,
        where: Optional[Match] = None,
        gap: float = 0.0,
        extended: bool = False,
    ) -> None:
        """Send a frame (more than 8 bytes is CAN FD) and wait as ``response``
        says, ``where`` adding to it for this step only; ``gap`` keeps at
        least that long before the next frame."""
        keep = []
        self._step(native_step(id, extended, bytes(data), None, response, where, gap, keep))

    def send_message(
        self,
        link: IsotpLink,
        data: bytes,
        response: Optional[Response] = None,
        *,
        where: Optional[Match] = None,
        gap: float = 0.0,
    ) -> None:
        """Send an ISO-TP message (1-4095 bytes) over ``link``; ``response``
        matches the reassembled answer."""
        keep = []
        self._step(native_step(link.tx_id, link.extended, bytes(data), link, response, where, gap, keep))

    def expect(self, response: Response, *, where: Optional[Match] = None, link: Optional[IsotpLink] = None) -> None:
        """Wait as ``response`` says without sending."""
        keep = []
        tx_id, extended = (link.tx_id, link.extended) if link is not None else (0, False)
        self._step(native_step(tx_id, extended, None, link, response, where, 0.0, keep))

    def progress(self) -> TransactionProgress:
        progress = native.CanHubTxnProgress()
        with self._bus._lock:
            native.lib.canhub_txn_progress(self._handle, ctypes.byref(progress))
        return from_native_progress(progress)

    def abort(self) -> None:
        """Cancel at the agent; finishing then reports the reason cancelled."""
        native.lib.canhub_txn_abort(self._handle)

    def finish(self) -> TransactionResult:
        """Wait for the outcome; raises :class:`TransactionAborted` with the
        report when the transaction did not complete."""
        while True:
            with self._bus._lock:
                status = native.lib.canhub_txn_wait(self._handle, WAIT_SLICE_MS)
            if status != native.ERR_TIMEOUT:
                break
        report = native.CanHubTxnReport()
        with self._bus._lock:
            status = native.lib.canhub_txn_finish(self._handle, ctypes.byref(report))
            error = native.last_error(self._bus._session)
        self._handle = None
        self._result = from_native_report(report)
        if status == native.ERR_TXN_ABORTED:
            raise TransactionAborted(self._result)
        if status != native.OK:
            raise CanOperationError(error)
        return self._result

    def _step(self, step: "native.CanHubTxnStep") -> None:
        while True:
            with self._bus._lock:
                status = native.lib.canhub_txn_try_step(self._handle, ctypes.byref(step), SEND_SLICE_MS)
                error = native.last_error(self._bus._session) if status not in (native.OK, native.ERR_TIMEOUT) else ""
            if status == native.OK:
                return
            if status == native.ERR_TIMEOUT:
                continue
            if status == native.ERR_TXN_ABORTED:
                self.finish()
            raise CanOperationError(error)

    def _report_roster(self, context, result) -> None:
        if self._on_roster is not None:
            self._on_roster(from_native_roster(result.contents))

    def _release(self) -> None:
        report = native.CanHubTxnReport()
        with self._bus._lock:
            native.lib.canhub_txn_wait(self._handle, CLOSE_WAIT_MS)
            native.lib.canhub_txn_finish(self._handle, ctypes.byref(report))
        self._handle = None
        self._result = from_native_report(report)


@dataclass(frozen=True)
class Options:
    exclusive: bool = False
    idle_timeout: Optional[float] = None
    keepalive: Optional[Keepalive] = None
    bitrate_switch: bool = False
    on_roster: Optional[Callable[[RosterResult], None]] = None


def run_script(
    bus,
    text: Optional[str],
    path: Optional[str],
    base_directory: Optional[str],
    progress: Optional[Callable[[TransactionProgress, int], Optional[bool]]],
    on_roster: Optional[Callable[[RosterResult], None]],
) -> TransactionResult:
    interruption = []

    def report_progress(context, native_progress, total_steps):
        if progress is None:
            return 1
        try:
            keep_going = progress(from_native_progress(native_progress.contents), total_steps) is not False
        except BaseException as error:
            interruption.append(error)
            return 0
        return 1 if keep_going else 0

    def report_roster(context, result):
        if on_roster is not None:
            on_roster(from_native_roster(result.contents))

    options = native.CanHubTxnScriptOptions()
    options.on_progress = native.ProgressCallback(report_progress)
    options.on_roster = native.RosterCallback(report_roster)
    report = native.CanHubTxnReport()
    with bus._lock:
        if path is not None:
            status = native.lib.canhub_txn_run_script_file(bus._session, os.fsencode(path), ctypes.byref(options), ctypes.byref(report))
        else:
            directory = os.fsencode(base_directory) if base_directory is not None else None
            status = native.lib.canhub_txn_run_script(bus._session, text.encode(), directory, ctypes.byref(options), ctypes.byref(report))
        error = "" if status in (native.OK, native.ERR_TXN_ABORTED) else native.last_error(bus._session)
    if interruption:
        raise interruption[0]

    result = from_native_report(report)
    if status == native.OK:
        return result
    if status == native.ERR_TXN_ABORTED:
        raise TransactionAborted(result)
    if status == native.ERR_ARGUMENT:
        raise ScriptError(error, _script_error_line(error))
    raise CanOperationError(error)


def to_native_options(options: Options, roster_callback, keep: list) -> "native.CanHubTxnOptions":
    native_options = native.CanHubTxnOptions()
    native_options.exclusive = options.exclusive
    native_options.idle_timeout_ms = _milliseconds(options.idle_timeout) if options.idle_timeout is not None else 5000
    native_options.bitrate_switch = options.bitrate_switch
    native_options.on_roster = roster_callback
    if options.keepalive is not None:
        keepalive = to_native_keepalive(options.keepalive)
        keep.append(keepalive)
        native_options.keepalive = ctypes.pointer(keepalive)
    return native_options


def to_native_keepalive(keepalive: Keepalive) -> "native.CanHubTxnKeepalive":
    data = bytes(keepalive.data)
    if len(data) > native.FRAME_PAYLOAD_MAX:
        raise ValueError(f"a keepalive frame holds at most {native.FRAME_PAYLOAD_MAX} bytes")
    native_keepalive = native.CanHubTxnKeepalive()
    native_keepalive.id = to_native_id(keepalive.id, keepalive.extended)
    native_keepalive.length = len(data)
    native_keepalive.data[: len(data)] = data
    native_keepalive.every_ms = _milliseconds(keepalive.every)
    return native_keepalive


def native_step(
    id: int,
    extended: bool,
    data: Optional[bytes],
    link: Optional[IsotpLink],
    response: Optional[Response],
    where: Optional[Match],
    gap: float,
    keep: list,
) -> "native.CanHubTxnStep":
    step = native.CanHubTxnStep()
    step.id = to_native_id(id, extended)
    if data is not None:
        buffer = ctypes.create_string_buffer(data, max(len(data), 1))
        keep.append(buffer)
        step.data = ctypes.cast(buffer, ctypes.c_void_p)
        step.length = len(data)
    if link is not None:
        native_link = to_native_link(link)
        keep.append(native_link)
        step.isotp = ctypes.pointer(native_link)
    if response is not None:
        native_response = to_native_response(response, keep)
        keep.append(native_response)
        step.response = ctypes.pointer(native_response)
    if where is not None:
        native_where = to_native_match(where)
        keep.append(native_where)
        step.where = ctypes.pointer(native_where)
    step.gap_us = int(round(gap * MICROSECONDS_PER_SECOND))
    return step


def to_native_response(response: Response, keep: list) -> "native.CanHubTxnResponse":
    native_response = native.CanHubTxnResponse()
    native_response.success = to_native_match(response.success)
    if response.failure is not None:
        failure = to_native_match(response.failure)
        keep.append(failure)
        native_response.failure = ctypes.pointer(failure)
    if response.pending is not None:
        pending = to_native_match(response.pending)
        keep.append(pending)
        native_response.pending = ctypes.pointer(pending)
    if response.nodes:
        nodes = (ctypes.c_uint32 * len(response.nodes))(*response.nodes)
        keep.append(nodes)
        native_response.nodes = ctypes.cast(nodes, ctypes.POINTER(ctypes.c_uint32))
        native_response.node_count = len(response.nodes)
    native_response.allowed_missing = response.allowed_missing
    if response.node_byte is not None:
        native_response.node_from = native.TXN_NODE_IN_BYTE
        native_response.node_byte = response.node_byte
    native_response.timeout_ms = _milliseconds(response.timeout)
    if response.timeout_after_pending is not None:
        native_response.timeout_after_pending_ms = _milliseconds(response.timeout_after_pending)
    native_response.continue_on_timeout = response.continue_on_timeout
    native_response.on_mismatch_abort = response.on_mismatch_abort
    return native_response


def to_native_match(match: Match) -> "native.CanHubTxnMatch":
    native_match = native.CanHubTxnMatch()
    native_match.id = to_native_id(match.id, match.extended)
    native_match.id_ignored_bits = match.ignored_bits
    for index, expected in match.data.items():
        if not 0 <= index < MATCH_BYTES:
            raise ValueError(f"a match compares the first {MATCH_BYTES} data bytes")
        if isinstance(expected, Masked):
            native_match.bytes[index].mask = expected.mask
            native_match.bytes[index].value = expected.value
        else:
            native_match.bytes[index].mask = BYTE_MASK
            native_match.bytes[index].value = expected
    return native_match


def to_native_link(link: IsotpLink) -> "native.CanHubTxnIsotpLink":
    native_link = native.CanHubTxnIsotpLink()
    native_link.tx_id = to_native_id(link.tx_id, link.extended)
    native_link.rx_id = to_native_id(link.rx_id, link.extended)
    if link.tx_address is not None or link.rx_address is not None:
        native_link.address_byte = True
        native_link.tx_address = link.tx_address if link.tx_address is not None else link.rx_address
        native_link.rx_address = link.rx_address if link.rx_address is not None else link.tx_address
    if link.padding is not None:
        native_link.padded = True
        native_link.padding = link.padding
    native_link.fd = link.fd
    native_link.bitrate_switch = link.bitrate_switch
    if link.fd:
        native_link.fd_data_length = link.fd_data_length
    native_link.block_size = link.block_size
    native_link.st_min = link.st_min
    return native_link


def to_native_id(id: int, extended: bool) -> "native.CanHubCanId":
    native_id = native.CanHubCanId()
    native_id.value = id
    native_id.extended = extended
    return native_id


def from_native_progress(progress: "native.CanHubTxnProgress") -> TransactionProgress:
    return TransactionProgress(progress.sent_steps, progress.completed_steps, progress.bytes_in_flight, progress.window_bytes)


def from_native_roster(result: "native.CanHubTxnRosterResult") -> RosterResult:
    return RosterResult(result.step, frozenset(result.missing[: result.missing_count]))


def from_native_report(report: "native.CanHubTxnReport") -> TransactionResult:
    frame = None
    if report.has_frame:
        frame = Message(
            arbitration_id=report.frame.id.value,
            is_extended_id=report.frame.id.extended,
            is_fd=bool(report.frame.flags & native.FRAME_FLAG_FD),
            data=bytes(report.frame.payload[: report.frame.length]),
        )
    missing = report.failing_roster.missing[: report.failing_roster.missing_count]
    return TransactionResult(
        done=report.done,
        reason=native.lib.canhub_txn_reason_name(report.reason).decode(),
        failing_step=report.failing_step,
        completed_steps=report.completed_steps,
        missing=frozenset(missing),
        offending_node=report.offending_node,
        expected=bytes(report.expected),
        got=bytes(report.got),
        frame=frame,
    )


def _milliseconds(seconds: float) -> int:
    return int(round(seconds * MILLISECONDS_PER_SECOND))


def _script_error_line(error: str) -> int:
    matched = SCRIPT_ERROR_PATTERN.match(error)
    return int(matched.group(1)) if matched else 0
