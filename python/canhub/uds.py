"""UDS (ISO 14229) over ISO-TP transaction steps: requests whose answers follow
the UDS rules, run at the agent as one transaction::

    with bus.uds(0x7E0, Isotp(0x7E8), exclusive=True) as ecu:
        ecu.request(b"\\x10\\x02")
        ecu.request(b"\\x34\\x00\\x44" + address + size)
        ecu.transfer(firmware, block_size=4093)
        ecu.request(b"\\x37")

A positive answer starts with the service id + 0x40 (or with ``expect``,
which must); a negative one (7F <service> <code>) aborts the transaction with
:class:`UdsNegativeResponse`; 7F <service> 78 makes the request wait
``p2_extended``. A request whose sub-function carries the
suppress-positive-response bit does not wait. Built on libcanhub's
``canhub_uds_*``.
"""

import ctypes
from typing import Optional

from can import CanOperationError

from . import _native as native
from .transaction import (
    MILLISECONDS_PER_SECOND,
    SEND_SLICE_MS,
    Isotp,
    Transaction,
    TransactionAborted,
    TransactionResult,
    to_native_isotp,
)

NEGATIVE_RESPONSE = 0x7F
NEGATIVE_ANSWER_SIZE = 3
TRANSFER_DATA = 0x36
POSITIVE_OFFSET = 0x40
COUNTER_MASK = 0xFF
FIRST_COUNTER = 1
DEFAULT_BLOCK_SIZE = 4093


class UdsNegativeResponse(TransactionAborted):
    """The ECU refused a request: ``service`` and its negative response ``code``."""

    def __init__(self, result: TransactionResult):
        super().__init__(result)
        self.service = result.frame.data[1]
        self.code = result.frame.data[2]
        self.args = (f"service 0x{self.service:02X} refused at step {result.failing_step}: code 0x{self.code:02X}",)


class Uds(Transaction):
    """A UDS conversation granted on one bus; use as a context manager."""

    def __init__(self, bus, uds: "native.CanHubUds"):
        super().__init__(bus, ())
        self._uds = uds

    def request(self, data: bytes, expect: Optional[bytes] = None) -> None:
        """Send one request (1-4095 bytes); ``expect`` is the start its positive
        answer must have, up to 8 bytes, beginning with the service id + 0x40."""
        request = bytes(data)
        expected = bytes(expect) if expect is not None else b""
        request_buffer = (ctypes.c_uint8 * len(request)).from_buffer_copy(request) if request else None
        expect_buffer = (ctypes.c_uint8 * len(expected)).from_buffer_copy(expected) if expected else None
        result = native.ERR_TIMEOUT

        while result == native.ERR_TIMEOUT:
            with self._bus._lock:
                result = native.lib.canhub_uds_request(
                    self._bus._session,
                    ctypes.byref(self._uds),
                    request_buffer,
                    len(request),
                    expect_buffer,
                    len(expected),
                    SEND_SLICE_MS,
                )
        self._check(result)

    def transfer(self, data: bytes, block_size: int = DEFAULT_BLOCK_SIZE) -> int:
        """Send data as TransferData blocks (36 <counter> <block>, the counter
        from 1, wrapping after 255), each answered 76 <counter>; returns how
        many blocks."""
        image = bytes(data)
        counter = FIRST_COUNTER
        blocks = 0

        for offset in range(0, len(image), block_size):
            self.request(
                bytes([TRANSFER_DATA, counter]) + image[offset:offset + block_size],
                expect=bytes([TRANSFER_DATA + POSITIVE_OFFSET, counter]),
            )
            counter = (counter + 1) & COUNTER_MASK
            blocks += 1
        return blocks

    def wait(self, timeout: Optional[float] = None) -> TransactionResult:
        try:
            return super().wait(timeout)
        except TransactionAborted as aborted:
            raise negative_or(aborted) from None

    def _check(self, result: int) -> None:
        try:
            super()._check(result)
        except TransactionAborted as aborted:
            raise negative_or(aborted) from None


def begin(bus, request_id: int, transport: Isotp, p2: float, p2_extended: float, exclusive: bool, timeout: float) -> Uds:
    uds = to_native_uds(request_id, transport, p2, p2_extended, exclusive)
    with bus._lock:
        result = native.lib.canhub_uds_begin(bus._session, ctypes.byref(uds), int(timeout * MILLISECONDS_PER_SECOND))
    if result != native.OK:
        raise CanOperationError(native.last_error(bus._session))
    return Uds(bus, uds)


def to_native_uds(request_id: int, transport: Isotp, p2: float, p2_extended: float, exclusive: bool) -> "native.CanHubUds":
    uds = native.CanHubUds()
    uds.struct_size = ctypes.sizeof(uds)
    uds.request_id = request_id | (native.CAN_ID_FLAG_EFF if transport.is_extended_id else 0)
    uds.p2_ms = int(round(p2 * MILLISECONDS_PER_SECOND))
    uds.p2_extended_ms = int(round(p2_extended * MILLISECONDS_PER_SECOND))
    uds.flags = native.TXN_FLAG_EXCLUSIVE if exclusive else 0
    uds.transport = to_native_isotp(transport)
    return uds


def negative_or(aborted: TransactionAborted) -> TransactionAborted:
    frame = aborted.result.frame
    if aborted.result.reason != "abort_match" or frame is None:
        return aborted
    if len(frame.data) < NEGATIVE_ANSWER_SIZE or frame.data[0] != NEGATIVE_RESPONSE:
        return aborted
    return UdsNegativeResponse(aborted.result)

