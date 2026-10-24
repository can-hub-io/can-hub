"""python-can backend over libcanhub: can.Bus(interface="canhub", ...)."""

import ctypes
import os
import threading
import time
from typing import Callable, Iterable, List, Optional, Tuple

from can import BusABC, CanInitializationError, CanOperationError, Message

from . import _native as native
from . import settings as interface_settings
from .settings import InterfaceSettings
from .transaction import Keepalive, RosterResult, Transaction, TransactionProgress, TransactionResult
from . import transaction as transactions

DEFAULT_TIMEOUT_MS = 5000
FINGERPRINT_HEX_LENGTH = 64
RECEIVE_SLICE_MS = 10


class CanHubMessage(Message):
    """A received frame. ``bridged`` is true when the hub injected it into
    this bus along a bridge route or group, rather than a node on the bus
    sending it."""

    __slots__ = ("bridged",)

    def __init__(self, *args, bridged: bool = False, **kwargs):
        super().__init__(*args, **kwargs)
        self.bridged = bridged


def _is_fingerprint(text: str) -> bool:
    if len(text) != FINGERPRINT_HEX_LENGTH:
        return False
    return all(character in "0123456789abcdef" for character in text.lower())


class CanHubBus(BusABC):
    """One hub interface as a python-can bus.

    :param channel: namespaced interface name ``agent/iface`` or numeric id.
    :param url: hub url (``quic://host:port``, ``tls://``, ``tcp://``,
        ``unix:///path``); ``None`` connects to the local hub unix socket.
    :param identity_cert: path to the client certificate (PEM). Together
        with ``identity_key`` it injects an explicit identity (the
        fingerprint the hub ACLs refer to) instead of the state dir one.
    :param identity_key: path to the client private key (PEM).
    :param hub_fingerprint: expected hub fingerprint (64 hex). When given,
        the connection is rejected unless the hub presents exactly this
        certificate — no TOFU, no pin store on disk.
    :param state_dir: directory holding the client TLS identity and the
        TOFU pin store (tls/quic only); ``None`` uses the can-hub default.
    :param receive_own_messages: also receive the frames this bus sends.
    :param ignore_rx_error_frames: do not receive the bus error frames, which
        python-can's socketcan interface delivers by default.
    """

    def __init__(
        self,
        channel: str,
        url: Optional[str] = None,
        identity_cert: Optional[str] = None,
        identity_key: Optional[str] = None,
        hub_fingerprint: Optional[str] = None,
        state_dir: Optional[str] = None,
        receive_own_messages: bool = False,
        reliable: bool = False,
        ignore_rx_error_frames: bool = False,
        **kwargs,
    ):
        self._lock = _FairLock()
        self._session = None
        self._writable = False
        self._channel_name = str(channel)

        config = self._build_connect_config(
            url, identity_cert, identity_key, hub_fingerprint, state_dir, DEFAULT_TIMEOUT_MS
        )

        self._session = native.lib.canhub_connect(ctypes.byref(config))
        if not self._session:
            detail = native.last_error(None)
            raise CanInitializationError(f"could not connect to {url or 'the local can-hub socket'}: {detail}")

        self._open(str(channel), self._open_flags(receive_own_messages, reliable, ignore_rx_error_frames))
        self.channel_info = f"canhub {channel} via {url or 'unix socket'}"
        super().__init__(channel=channel, **kwargs)

    @classmethod
    def list_interfaces(
        cls,
        url: Optional[str] = None,
        identity_cert: Optional[str] = None,
        identity_key: Optional[str] = None,
        hub_fingerprint: Optional[str] = None,
        state_dir: Optional[str] = None,
        timeout: float = DEFAULT_TIMEOUT_MS / 1000,
    ) -> List[dict]:
        """List the interfaces a hub exports, ready to splat into ``can.Bus``.

        Connects to ``url`` (or the local hub unix socket when ``None``), asks the
        hub for its interface directory and disconnects. Each entry is a python-can
        config dict whose keys ``can.Bus(**entry)`` consumes directly. Connection
        parameters mirror :class:`CanHubBus`.
        """
        timeout_ms = max(0, int(timeout * 1000))
        config = cls._build_connect_config(
            url, identity_cert, identity_key, hub_fingerprint, state_dir, timeout_ms
        )

        session = native.lib.canhub_connect(ctypes.byref(config))
        if not session:
            detail = native.last_error(None)
            raise CanInitializationError(f"could not connect to {url or 'the local can-hub socket'}: {detail}")

        try:
            interfaces = native.list_interfaces(session, timeout_ms)
        except OSError as error:
            raise CanOperationError(str(error)) from error
        finally:
            native.lib.canhub_close(session)

        connection = {
            "url": url,
            "identity_cert": identity_cert,
            "identity_key": identity_key,
            "hub_fingerprint": hub_fingerprint,
            "state_dir": state_dir,
        }
        present = {key: value for key, value in connection.items() if value is not None}
        return [cls._to_config(info, present) for info in interfaces]

    @classmethod
    def _detect_available_configs(cls) -> List[dict]:
        try:
            return cls.list_interfaces(
                url=os.environ.get("CANHUB_URL"),
                identity_cert=os.environ.get("CANHUB_IDENTITY_CERT"),
                identity_key=os.environ.get("CANHUB_IDENTITY_KEY"),
                hub_fingerprint=os.environ.get("CANHUB_HUB_FINGERPRINT"),
                state_dir=os.environ.get("CANHUB_STATE_DIR"),
            )
        except Exception:
            return []

    def _recv_internal(self, timeout: Optional[float]) -> Tuple[Optional[Message], bool]:
        frame = native.CanHubFrame()
        deadline = None if timeout is None else time.monotonic() + max(0.0, timeout)

        while True:
            with self._lock:
                result = native.lib.canhub_recv(self._session, ctypes.byref(frame), _slice_ms(deadline))
            if result == native.RECEIVED:
                return self._to_message(frame), self._filters_applied
            if result != native.ERR_TIMEOUT:
                raise CanOperationError(native.last_error(self._session))
            if deadline is not None and time.monotonic() >= deadline:
                return None, self._filters_applied

    def send(self, msg: Message, timeout: Optional[float] = None) -> None:
        frame = native.CanHubFrame()

        if not self._writable:
            raise CanOperationError("bus is read-only (write denied by the hub ACL)")
        if msg.timestamp:
            frame.timestamp_us = int(msg.timestamp * 1_000_000)
        frame.id.value = msg.arbitration_id
        frame.id.extended = msg.is_extended_id
        if msg.is_remote_frame:
            frame.flags |= native.FRAME_FLAG_RTR
        if msg.is_fd:
            frame.flags |= native.FRAME_FLAG_FD
        if msg.bitrate_switch:
            frame.flags |= native.FRAME_FLAG_BRS
        data = bytes(msg.data or b"")
        frame.length = len(data)
        frame.payload[: len(data)] = data

        with self._lock:
            result = native.lib.canhub_send(self._session, ctypes.byref(frame))
        if result != native.OK:
            raise CanOperationError(native.last_error(self._session))

    def configure(self, **settings) -> InterfaceSettings:
        """Reconfigure this bus's interface on its agent, all in one step.

        Takes the keywords of :mod:`canhub.settings` (``bitrate=500000,
        sample_point=0.875, fd=True, data_bitrate=2000000`` ...). The hub
        allows it only with an ``rwa`` ACL grant (unix and plain tcp clients
        are trusted). Returns the settings the device holds afterwards.
        """
        try:
            requested = interface_settings.to_native(**settings)
        except ValueError as error:
            raise CanOperationError(str(error)) from error

        try:
            with self._lock:
                effective = native.configure(self._session, self._configurable_name(), requested, DEFAULT_TIMEOUT_MS)
        except OSError as error:
            raise CanOperationError(str(error)) from error
        return interface_settings.from_native(effective)

    def interface_settings(self) -> InterfaceSettings:
        """The settings this bus's interface holds now on its agent."""
        try:
            with self._lock:
                effective = native.query(self._session, self._configurable_name(), DEFAULT_TIMEOUT_MS)
        except OSError as error:
            raise CanOperationError(str(error)) from error
        return interface_settings.from_native(effective)

    def transaction(
        self,
        exclusive: bool = False,
        idle_timeout: Optional[float] = None,
        keepalive: Optional[Keepalive] = None,
        bitrate_switch: bool = False,
        on_roster: Optional[Callable[[RosterResult], None]] = None,
    ) -> Transaction:
        """A transaction on this bus (open it with ``reliable=True``); nothing
        is sent until its first step.

        ``exclusive`` drops other clients' frames to this interface until the
        transaction ends. ``idle_timeout`` is how long the agent waits for
        steps once it ran out of them (default 5 s), sending ``keepalive``
        meanwhile. ``bitrate_switch`` sends the CAN FD frames of the steps
        with bit rate switch. ``on_roster`` gets a :class:`RosterResult` for
        every step waiting for nodes that moves on without all of them.
        See :mod:`canhub.transaction`.
        """
        if not self._writable:
            raise CanOperationError("bus is read-only (write denied by the hub ACL)")
        try:
            options = transactions.Options(exclusive, idle_timeout, keepalive, bitrate_switch, on_roster)
            return Transaction(self, options)
        except ValueError as error:
            raise CanOperationError(str(error)) from error

    def run_txn_script(
        self,
        text: str,
        base_directory: Optional[str] = None,
        progress: Optional[Callable[[TransactionProgress, int], Optional[bool]]] = None,
        on_roster: Optional[Callable[[RosterResult], None]] = None,
    ) -> TransactionResult:
        """Run a transaction script (doc/transaction-scripts.md) given as text
        on this bus, opened with ``reliable=True``; files it reads resolve
        against ``base_directory`` (default: the working directory).
        ``progress(progress, total_steps)`` runs a few times a second and
        cancels the transaction by returning ``False``; it and ``on_roster``
        must not use the bus. Raises :class:`ScriptError` for a script that
        does not compile and :class:`TransactionAborted` with the report.
        """
        if not self._writable:
            raise CanOperationError("bus is read-only (write denied by the hub ACL)")
        return transactions.run_script(self, text, None, base_directory, progress, on_roster)

    def run_txn_script_file(
        self,
        path: str,
        progress: Optional[Callable[[TransactionProgress, int], Optional[bool]]] = None,
        on_roster: Optional[Callable[[RosterResult], None]] = None,
    ) -> TransactionResult:
        """:meth:`run_txn_script` for a script file; files it reads resolve
        against its directory."""
        if not self._writable:
            raise CanOperationError("bus is read-only (write denied by the hub ACL)")
        return transactions.run_script(self, None, path, None, progress, on_roster)

    def drop_counters(self) -> dict:
        with self._lock:
            return native.drop_counters(self._session)

    def shutdown(self) -> None:
        super().shutdown()
        with self._lock:
            if self._session:
                native.lib.canhub_close(self._session)
                self._session = None

    @property
    def _filters_applied(self) -> bool:
        return getattr(self, "_hardware_filtered", False)

    def _apply_filters(self, filters) -> None:
        native_filters = _native_filters(filters or [])
        if not filters or len(native_filters) > native.FILTERS_MAX:
            with self._lock:
                native.lib.canhub_set_filters(self._session, None, 0)
            self._hardware_filtered = False
            return

        array = (native.CanHubFilter * len(native_filters))(*native_filters)
        with self._lock:
            result = native.lib.canhub_set_filters(self._session, array, len(native_filters))
        self._hardware_filtered = result == native.OK

    def _configurable_name(self) -> str:
        if "/" not in self._channel_name:
            raise CanOperationError("configuring needs the bus opened by its agent/iface name")
        return self._channel_name

    def _open(self, channel: str, flags: int) -> None:
        result = native.lib.canhub_open(self._session, channel.encode(), flags, DEFAULT_TIMEOUT_MS)
        if result == native.ERR_WRITE_DENIED:
            result = native.lib.canhub_open(self._session, channel.encode(), flags & ~native.OPEN_FLAG_WRITE, DEFAULT_TIMEOUT_MS)
        else:
            self._writable = result == native.OK

        if result != native.OK:
            detail = native.last_error(self._session)
            native.lib.canhub_close(self._session)
            self._session = None
            raise CanInitializationError(f"could not open {channel}: {detail}")

    @staticmethod
    def _open_flags(receive_own_messages: bool, reliable: bool, ignore_rx_error_frames: bool) -> int:
        flags = native.OPEN_FLAG_WRITE
        if not receive_own_messages:
            flags |= native.OPEN_FLAG_NO_ECHO
        if reliable:
            flags |= native.OPEN_FLAG_RELIABLE
        if not ignore_rx_error_frames:
            flags |= native.OPEN_FLAG_ERROR_FRAMES
        return flags

    @staticmethod
    def _build_connect_config(
        url: Optional[str],
        identity_cert: Optional[str],
        identity_key: Optional[str],
        hub_fingerprint: Optional[str],
        state_dir: Optional[str],
        timeout_ms: int,
    ) -> "native.CanHubConnectConfig":
        if hub_fingerprint is not None:
            hub_fingerprint = str(hub_fingerprint)
            if not _is_fingerprint(hub_fingerprint):
                raise CanInitializationError(
                    "hub_fingerprint must be 64 hex characters (it may have been "
                    "mangled by python-can config value casting; pass it as a string)"
                )

        config = native.CanHubConnectConfig()
        config.struct_size = ctypes.sizeof(config)
        config.url = url.encode() if url else None
        config.state_directory = state_dir.encode() if state_dir else None
        config.certificate_path = identity_cert.encode() if identity_cert else None
        config.key_path = identity_key.encode() if identity_key else None
        config.hub_fingerprint = hub_fingerprint.encode() if hub_fingerprint else None
        config.connect_timeout_ms = timeout_ms
        return config

    @staticmethod
    def _to_config(info: native.CanHubInterfaceInfo, connection: dict) -> dict:
        agent = info.agent.decode("utf-8", errors="replace")
        name = info.interface_name.decode("utf-8", errors="replace")
        return {"interface": "canhub", "channel": f"{agent}/{name}", **connection}

    @staticmethod
    def _to_message(frame: native.CanHubFrame) -> CanHubMessage:
        return CanHubMessage(
            timestamp=frame.timestamp_us / 1_000_000,
            arbitration_id=frame.id.value,
            is_extended_id=frame.id.extended,
            is_remote_frame=bool(frame.flags & native.FRAME_FLAG_RTR),
            is_error_frame=bool(frame.flags & native.FRAME_FLAG_ERROR),
            is_fd=bool(frame.flags & native.FRAME_FLAG_FD),
            bitrate_switch=bool(frame.flags & native.FRAME_FLAG_BRS),
            dlc=frame.length,
            data=bytes(frame.payload[: frame.length]),
            bridged=bool(frame.flags & native.FRAME_FLAG_BRIDGED),
        )


class _FairLock:
    """Serialises the native calls on one session, first come first served.

    A plain Lock would let the receive loop take it back before a waiting
    send ever runs: it releases and re-acquires while still holding the GIL.
    """

    def __init__(self):
        self._condition = threading.Condition()
        self._next_ticket = 0
        self._serving = 0

    def __enter__(self):
        with self._condition:
            ticket = self._next_ticket
            self._next_ticket += 1
            self._condition.wait_for(lambda: self._serving == ticket)

    def __exit__(self, *exc_info):
        with self._condition:
            self._serving += 1
            self._condition.notify_all()


def _native_filters(filters) -> List["native.CanHubFilter"]:
    """python-can filters: a filter that names no ``extended`` takes both
    kinds of id, which the native one, comparing it always, says twice."""
    native_filters = []
    for can_filter in filters:
        kinds = [can_filter["extended"]] if "extended" in can_filter else [False, True]
        for extended in kinds:
            width = native.EXTENDED_ID_MASK if extended else native.STANDARD_ID_MASK
            native_filter = native.CanHubFilter()
            native_filter.id.value = can_filter["can_id"] & width
            native_filter.id.extended = extended
            native_filter.id_ignored_bits = width & ~can_filter["can_mask"]
            native_filters.append(native_filter)
    return native_filters


def _slice_ms(deadline: Optional[float]) -> int:
    if deadline is None:
        return RECEIVE_SLICE_MS
    remaining_ms = max(0, int((deadline - time.monotonic()) * 1000))
    return min(RECEIVE_SLICE_MS, remaining_ms)
