"""ctypes bindings over the libcanhub C ABI (include/canhub.h, version 2).

The shared library is resolved in order: the CANHUB_LIBRARY environment
variable, the copy bundled inside this package, the system library path.
"""

import ctypes
import os
import sys
from ctypes import POINTER, Structure, c_bool, c_char, c_char_p, c_int, c_int32, c_size_t, c_uint8, c_uint16, c_uint32, c_uint64, c_void_p

FRAME_PAYLOAD_MAX = 64
FRAME_PAYLOAD_MAX_CLASSIC = 8
AGENT_NAME_MAX = 128
INTERFACE_NAME_MAX = 16
FILTERS_MAX = 16

STANDARD_ID_MASK = 0x7FF
EXTENDED_ID_MASK = 0x1FFFFFFF

FRAME_FLAG_FD = 1 << 0
FRAME_FLAG_BRS = 1 << 1
FRAME_FLAG_RTR = 1 << 2
FRAME_FLAG_ERROR = 1 << 3
FRAME_FLAG_BRIDGED = 1 << 7

OPEN_FLAG_NO_ECHO = 1 << 0
OPEN_FLAG_WRITE = 1 << 1
OPEN_FLAG_RELIABLE = 1 << 2
OPEN_FLAG_ERROR_FRAMES = 1 << 3

OK = 0
RECEIVED = 1
ERR_TIMEOUT = -1
ERR_DISCONNECTED = -2
ERR_NOT_FOUND = -3
ERR_OPEN_REJECTED = -4
ERR_WRITE_DENIED = -5
ERR_READ_DENIED = -6
ERR_ARGUMENT = -7
ERR_STATE = -8
ERR_TRANSPORT = -9
ERR_HUB = -10
ERR_RELIABLE_UNSUPPORTED = -11
ERR_RATE_LIMITED = -12
ERR_CONFIGURE_DENIED = -13
ERR_APPLY_FAILED = -14
ERR_AGENT_UNREACHABLE = -15
ERR_LEASED = -16
ERR_TXN_BUSY = -17
ERR_TXN_UNSUPPORTED = -18
ERR_TXN_ABORTED = -19

TXN_ROSTER_MAX = 64
TXN_MATCH_BYTES = 8
TXN_ANY_ID_BITS = 0xFFFFFFFF
ISOTP_MESSAGE_MAX = 4095
TXN_NODE_IN_ID = 0
TXN_NODE_IN_BYTE = 1
FOREVER = -1

SETTING_BITRATE = 1 << 0
SETTING_SAMPLE_POINT = 1 << 1
SETTING_DATA_BITRATE = 1 << 2
SETTING_DATA_SAMPLE_POINT = 1 << 3
SETTING_CTRLMODE = 1 << 4
SETTING_RESTART_MS = 1 << 5
SETTING_TXQUEUELEN = 1 << 6
SETTING_LINK = 1 << 7
SETTING_STATE = 1 << 8
SETTING_BIT_TIMING = 1 << 9
SETTING_DATA_BIT_TIMING = 1 << 10
SETTING_TERMINATION = 1 << 11

CTRLMODE_LOOPBACK = 1 << 0
CTRLMODE_LISTEN_ONLY = 1 << 1
CTRLMODE_TRIPLE_SAMPLING = 1 << 2
CTRLMODE_ONE_SHOT = 1 << 3
CTRLMODE_BERR_REPORTING = 1 << 4
CTRLMODE_FD = 1 << 5


class CanHubCanId(Structure):
    _fields_ = [
        ("value", c_uint32),
        ("extended", c_bool),
    ]


class CanHubFrame(Structure):
    _fields_ = [
        ("timestamp_us", c_uint64),
        ("id", CanHubCanId),
        ("flags", c_uint8),
        ("length", c_uint8),
        ("payload", c_uint8 * FRAME_PAYLOAD_MAX),
    ]


class CanHubInterfaceInfo(Structure):
    _fields_ = [
        ("interface_id", c_uint32),
        ("agent", c_char * AGENT_NAME_MAX),
        ("interface_name", c_char * INTERFACE_NAME_MAX),
    ]


class CanHubFilter(Structure):
    _fields_ = [
        ("id", CanHubCanId),
        ("id_ignored_bits", c_uint32),
    ]


class CanHubStats(Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("frames_rate_limited", c_uint64),
        ("frames_ring_dropped", c_uint64),
    ]


class CanHubBitTiming(Structure):
    _fields_ = [
        ("tq", c_uint32),
        ("prop_seg", c_uint32),
        ("phase_seg1", c_uint32),
        ("phase_seg2", c_uint32),
        ("sjw", c_uint32),
        ("brp", c_uint32),
    ]


class CanHubInterfaceSettings(Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("fields", c_uint32),
        ("bitrate", c_uint32),
        ("data_bitrate", c_uint32),
        ("sample_point", c_uint16),
        ("data_sample_point", c_uint16),
        ("ctrlmode_mask", c_uint32),
        ("ctrlmode_flags", c_uint32),
        ("restart_ms", c_uint32),
        ("txqueuelen", c_uint32),
        ("link_up", c_uint8),
        ("can_state", c_uint8),
        ("termination", c_uint16),
        ("bit_timing", CanHubBitTiming),
        ("data_bit_timing", CanHubBitTiming),
    ]


class CanHubTxnByte(Structure):
    _fields_ = [
        ("mask", c_uint8),
        ("value", c_uint8),
    ]


class CanHubTxnMatch(Structure):
    _fields_ = [
        ("id", CanHubCanId),
        ("id_ignored_bits", c_uint32),
        ("bytes", CanHubTxnByte * TXN_MATCH_BYTES),
    ]


class CanHubTxnResponse(Structure):
    _fields_ = [
        ("success", CanHubTxnMatch),
        ("failure", POINTER(CanHubTxnMatch)),
        ("pending", POINTER(CanHubTxnMatch)),
        ("nodes", POINTER(c_uint32)),
        ("node_count", c_uint32),
        ("allowed_missing", c_uint32),
        ("node_from", c_int),
        ("node_byte", c_uint8),
        ("timeout_ms", c_uint32),
        ("timeout_after_pending_ms", c_uint32),
        ("continue_on_timeout", c_bool),
        ("on_mismatch_abort", c_bool),
    ]


class CanHubTxnIsotpLink(Structure):
    _fields_ = [
        ("tx_id", CanHubCanId),
        ("rx_id", CanHubCanId),
        ("address_byte", c_bool),
        ("tx_address", c_uint8),
        ("rx_address", c_uint8),
        ("padded", c_bool),
        ("padding", c_uint8),
        ("fd", c_bool),
        ("bitrate_switch", c_bool),
        ("fd_data_length", c_uint8),
        ("block_size", c_uint8),
        ("st_min", c_uint8),
    ]


class CanHubTxnKeepalive(Structure):
    _fields_ = [
        ("id", CanHubCanId),
        ("data", c_uint8 * FRAME_PAYLOAD_MAX),
        ("length", c_uint8),
        ("every_ms", c_uint32),
    ]


class CanHubTxnRosterResult(Structure):
    _fields_ = [
        ("step", c_uint32),
        ("missing", c_uint32 * TXN_ROSTER_MAX),
        ("missing_count", c_uint32),
    ]


RosterCallback = ctypes.CFUNCTYPE(None, c_void_p, POINTER(CanHubTxnRosterResult))


class CanHubTxnOptions(Structure):
    _fields_ = [
        ("exclusive", c_bool),
        ("idle_timeout_ms", c_uint32),
        ("keepalive", POINTER(CanHubTxnKeepalive)),
        ("bitrate_switch", c_bool),
        ("on_roster", RosterCallback),
        ("context", c_void_p),
    ]


class CanHubTxnStep(Structure):
    _fields_ = [
        ("id", CanHubCanId),
        ("data", c_void_p),
        ("length", c_uint16),
        ("isotp", POINTER(CanHubTxnIsotpLink)),
        ("response", POINTER(CanHubTxnResponse)),
        ("where", POINTER(CanHubTxnMatch)),
        ("gap_us", c_uint32),
    ]


class CanHubTxnProgress(Structure):
    _fields_ = [
        ("sent_steps", c_uint32),
        ("completed_steps", c_uint32),
        ("bytes_in_flight", c_uint32),
        ("window_bytes", c_uint32),
    ]


class CanHubTxnReport(Structure):
    _fields_ = [
        ("done", c_bool),
        ("reason", c_int),
        ("failing_step", c_uint32),
        ("completed_steps", c_uint32),
        ("failing_roster", CanHubTxnRosterResult),
        ("offending_node", c_uint32),
        ("expected", c_uint8 * TXN_MATCH_BYTES),
        ("got", c_uint8 * TXN_MATCH_BYTES),
        ("has_frame", c_bool),
        ("frame", CanHubFrame),
    ]


ProgressCallback = ctypes.CFUNCTYPE(c_int32, c_void_p, POINTER(CanHubTxnProgress), c_uint32)


class CanHubTxnScriptOptions(Structure):
    _fields_ = [
        ("on_roster", RosterCallback),
        ("on_progress", ProgressCallback),
        ("context", c_void_p),
    ]


class CanHubConnectConfig(Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("url", c_char_p),
        ("state_directory", c_char_p),
        ("certificate_path", c_char_p),
        ("key_path", c_char_p),
        ("hub_fingerprint", c_char_p),
        ("connect_timeout_ms", c_int32),
    ]


def _load_library():
    bundled_name = "libcanhub.dll" if sys.platform == "win32" else "libcanhub.so"

    override = os.environ.get("CANHUB_LIBRARY")
    if override:
        return ctypes.CDLL(override)

    bundled = os.path.join(os.path.dirname(__file__), bundled_name)
    if os.path.exists(bundled):
        return ctypes.CDLL(bundled)

    if sys.platform == "win32":
        return ctypes.CDLL("libcanhub.dll")
    return ctypes.CDLL("libcanhub.so.0")


lib = _load_library()

lib.canhub_api_version.restype = c_uint32
lib.canhub_api_version.argtypes = []

lib.canhub_connect.restype = c_void_p
lib.canhub_connect.argtypes = [POINTER(CanHubConnectConfig)]

lib.canhub_close.restype = None
lib.canhub_close.argtypes = [c_void_p]

lib.canhub_last_error.restype = c_char_p
lib.canhub_last_error.argtypes = [c_void_p]

lib.canhub_list.restype = c_int32
lib.canhub_list.argtypes = [c_void_p, POINTER(CanHubInterfaceInfo), c_size_t, c_int32]

lib.canhub_open.restype = c_int32
lib.canhub_open.argtypes = [c_void_p, c_char_p, c_uint32, c_int32]

lib.canhub_set_filters.restype = c_int32
lib.canhub_set_filters.argtypes = [c_void_p, POINTER(CanHubFilter), c_uint8]

lib.canhub_recv.restype = c_int32
lib.canhub_recv.argtypes = [c_void_p, POINTER(CanHubFrame), c_int32]

lib.canhub_send.restype = c_int32
lib.canhub_send.argtypes = [c_void_p, POINTER(CanHubFrame)]

lib.canhub_stats.restype = c_int32
lib.canhub_stats.argtypes = [c_void_p, POINTER(CanHubStats)]

lib.canhub_configure.restype = c_int32
lib.canhub_configure.argtypes = [
    c_void_p, c_char_p, POINTER(CanHubInterfaceSettings), POINTER(CanHubInterfaceSettings), c_int32
]

lib.canhub_query.restype = c_int32
lib.canhub_query.argtypes = [c_void_p, c_char_p, POINTER(CanHubInterfaceSettings), c_int32]

lib.canhub_txn_create.restype = c_void_p
lib.canhub_txn_create.argtypes = [c_void_p, POINTER(CanHubTxnOptions)]

lib.canhub_txn_try_step.restype = c_int32
lib.canhub_txn_try_step.argtypes = [c_void_p, POINTER(CanHubTxnStep), c_int32]

lib.canhub_txn_progress.restype = c_int32
lib.canhub_txn_progress.argtypes = [c_void_p, POINTER(CanHubTxnProgress)]

lib.canhub_txn_abort.restype = c_int32
lib.canhub_txn_abort.argtypes = [c_void_p]

lib.canhub_txn_wait.restype = c_int32
lib.canhub_txn_wait.argtypes = [c_void_p, c_int32]

lib.canhub_txn_finish.restype = c_int32
lib.canhub_txn_finish.argtypes = [c_void_p, POINTER(CanHubTxnReport)]

lib.canhub_txn_reason_name.restype = c_char_p
lib.canhub_txn_reason_name.argtypes = [c_int]

lib.canhub_txn_run_script.restype = c_int32
lib.canhub_txn_run_script.argtypes = [c_void_p, c_char_p, c_char_p, POINTER(CanHubTxnScriptOptions), POINTER(CanHubTxnReport)]

lib.canhub_txn_run_script_file.restype = c_int32
lib.canhub_txn_run_script_file.argtypes = [c_void_p, c_char_p, POINTER(CanHubTxnScriptOptions), POINTER(CanHubTxnReport)]


def last_error(session):
    detail = lib.canhub_last_error(session)
    return detail.decode("utf-8", errors="replace") if detail else "unknown error"


def drop_counters(session):
    stats = CanHubStats()
    stats.struct_size = ctypes.sizeof(stats)
    if lib.canhub_stats(session, ctypes.byref(stats)) != OK:
        raise OSError(last_error(session))
    return {"rate_limited": stats.frames_rate_limited, "ring_dropped": stats.frames_ring_dropped}


def list_interfaces(session, timeout_ms, capacity=64):
    while True:
        buffer = (CanHubInterfaceInfo * capacity)()
        count = lib.canhub_list(session, buffer, capacity, timeout_ms)
        if count < 0:
            raise OSError(last_error(session))
        if count < capacity:
            return [buffer[index] for index in range(count)]
        capacity *= 2


def new_settings():
    settings = CanHubInterfaceSettings()
    settings.struct_size = ctypes.sizeof(settings)
    return settings


def configure(session, interface_name, requested, timeout_ms):
    effective = new_settings()
    result = lib.canhub_configure(
        session, interface_name.encode(), ctypes.byref(requested), ctypes.byref(effective), timeout_ms
    )
    if result != OK:
        raise OSError(last_error(session))
    return effective


def query(session, interface_name, timeout_ms):
    effective = new_settings()
    if lib.canhub_query(session, interface_name.encode(), ctypes.byref(effective), timeout_ms) != OK:
        raise OSError(last_error(session))
    return effective
