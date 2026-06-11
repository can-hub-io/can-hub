"""python-can backend over libcanhub: can.Bus(interface="canhub", ...)."""

import ctypes
from typing import Optional, Tuple

from can import BusABC, CanInitializationError, CanOperationError, Message

from . import _native as native

DEFAULT_TIMEOUT_MS = 5000
FINGERPRINT_HEX_LENGTH = 64


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
        **kwargs,
    ):
        self._session = None
        self._writable = False

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
        config.connect_timeout_ms = DEFAULT_TIMEOUT_MS

        self._session = native.lib.canhub_connect(ctypes.byref(config))
        if not self._session:
            raise CanInitializationError(f"could not connect to {url or 'the local can-hub socket'}")

        self._open(str(channel), receive_own_messages)
        self.channel_info = f"canhub {channel} via {url or 'unix socket'}"
        super().__init__(channel=channel, **kwargs)

    def _recv_internal(self, timeout: Optional[float]) -> Tuple[Optional[Message], bool]:
        frame = native.CanHubFrame()
        timeout_ms = -1 if timeout is None else max(0, int(timeout * 1000))

        result = native.lib.canhub_recv(self._session, ctypes.byref(frame), timeout_ms)
        if result == native.RECEIVED:
            return self._to_message(frame), self._filters_applied
        if result == native.ERR_TIMEOUT:
            return None, self._filters_applied

        raise CanOperationError(native.last_error(self._session))

    def send(self, msg: Message, timeout: Optional[float] = None) -> None:
        frame = native.CanHubFrame()

        if not self._writable:
            raise CanOperationError("bus is read-only (write denied by the hub ACL)")
        if msg.timestamp:
            frame.timestamp_us = int(msg.timestamp * 1_000_000)
        frame.can_id = msg.arbitration_id & native.CAN_ID_MASK
        if msg.is_extended_id:
            frame.can_id |= native.CAN_ID_FLAG_EFF
        if msg.is_remote_frame:
            frame.can_id |= native.CAN_ID_FLAG_RTR
        if msg.is_error_frame:
            frame.can_id |= native.CAN_ID_FLAG_ERR
        if msg.is_fd:
            frame.flags |= native.FRAME_FLAG_FD
        if msg.bitrate_switch:
            frame.flags |= native.FRAME_FLAG_BRS
        data = bytes(msg.data or b"")
        frame.length = len(data)
        frame.payload[: len(data)] = data

        result = native.lib.canhub_send(self._session, ctypes.byref(frame))
        if result != native.OK:
            raise CanOperationError(native.last_error(self._session))

    def shutdown(self) -> None:
        super().shutdown()
        if self._session:
            native.lib.canhub_close(self._session)
            self._session = None

    @property
    def _filters_applied(self) -> bool:
        return getattr(self, "_hardware_filtered", False)

    def _apply_filters(self, filters) -> None:
        if not filters or len(filters) > native.FILTERS_MAX:
            self._hardware_filtered = False
            return

        native_filters = (native.CanHubFilter * len(filters))()
        for slot, can_filter in zip(native_filters, filters):
            slot.can_id = can_filter["can_id"]
            slot.can_mask = can_filter["can_mask"]
        result = native.lib.canhub_set_filters(self._session, native_filters, len(filters))
        self._hardware_filtered = result == native.OK

    def _open(self, channel: str, receive_own_messages: bool) -> None:
        flags = native.OPEN_FLAG_WRITE
        if not receive_own_messages:
            flags |= native.OPEN_FLAG_NO_ECHO

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
    def _to_message(frame: native.CanHubFrame) -> Message:
        return Message(
            timestamp=frame.timestamp_us / 1_000_000,
            arbitration_id=frame.can_id & native.CAN_ID_MASK,
            is_extended_id=bool(frame.can_id & native.CAN_ID_FLAG_EFF),
            is_remote_frame=bool(frame.can_id & native.CAN_ID_FLAG_RTR),
            is_error_frame=bool(frame.can_id & native.CAN_ID_FLAG_ERR),
            is_fd=bool(frame.flags & native.FRAME_FLAG_FD),
            bitrate_switch=bool(frame.flags & native.FRAME_FLAG_BRS),
            dlc=frame.length,
            data=bytes(frame.payload[: frame.length]),
        )
