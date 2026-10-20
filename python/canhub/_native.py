"""ctypes bindings over the libcanhub C ABI (include/canhub.h, version 1).

The shared library is resolved in order: the CANHUB_LIBRARY environment
variable, the copy bundled inside this package, the system library path.
"""

import ctypes
import os
import sys
from ctypes import POINTER, Structure, c_char, c_char_p, c_int32, c_size_t, c_uint8, c_uint16, c_uint32, c_uint64, c_void_p

FRAME_PAYLOAD_MAX = 64
AGENT_NAME_MAX = 128
INTERFACE_NAME_MAX = 16
FILTERS_MAX = 16

CAN_ID_MASK = 0x1FFFFFFF
CAN_ID_FLAG_ERR = 1 << 29
CAN_ID_FLAG_RTR = 1 << 30
CAN_ID_FLAG_EFF = 1 << 31

FRAME_FLAG_FD = 1 << 0
FRAME_FLAG_BRS = 1 << 1
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
TXN_TEMPLATES_MAX = 16
TXN_PATTERNS = 3
TXN_FLAG_EXCLUSIVE = 1 << 0
TXN_ORIGIN_NONE = 0
TXN_ORIGIN_CAN_ID = 1
TXN_ORIGIN_PAYLOAD = 2
TXN_TEMPLATE_SOFT = 1 << 0
TXN_TEMPLATE_MISMATCH_ABORT = 1 << 1
TXN_PATTERN_ADVANCE = 0
TXN_PATTERN_ABORT = 1
TXN_PATTERN_EXTEND = 2
TXN_STEP_EXPECT_VALUE = 1 << 0
TXN_STEP_EXPECT_ONLY = 1 << 1
TXN_DONE = 0

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


class CanHubFrame(Structure):
    _fields_ = [
        ("timestamp_us", c_uint64),
        ("can_id", c_uint32),
        ("flags", c_uint8),
        ("length", c_uint8),
        ("reserved", c_uint8 * 2),
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
        ("can_id", c_uint32),
        ("can_mask", c_uint32),
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


class CanHubTxnBegin(Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("flags", c_uint8),
        ("origin_source", c_uint8),
        ("origin_shift", c_uint8),
        ("origin_payload_offset", c_uint8),
        ("origin_mask", c_uint32),
        ("roster_count", c_uint32),
        ("roster", c_uint32 * TXN_ROSTER_MAX),
    ]


class CanHubTxnPattern(Structure):
    _fields_ = [
        ("can_id", c_uint32),
        ("can_mask", c_uint32),
        ("payload_mask", c_uint64),
        ("payload_value", c_uint64),
    ]


class CanHubTxnTemplate(Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("template_id", c_uint8),
        ("advance_mode", c_uint8),
        ("flags", c_uint8),
        ("count_k", c_uint8),
        ("patterns_present", c_uint8),
        ("reserved", c_uint8 * 3),
        ("timeout_ms", c_uint32),
        ("gap_us", c_uint32),
        ("patterns", CanHubTxnPattern * TXN_PATTERNS),
    ]


class CanHubTxnStep(Structure):
    _fields_ = [
        ("can_id", c_uint32),
        ("template_id", c_uint8),
        ("flags", c_uint8),
        ("frame_flags", c_uint8),
        ("length", c_uint8),
        ("expect_value", c_uint64),
        ("payload", c_uint8 * FRAME_PAYLOAD_MAX),
    ]


class CanHubTxnProgress(Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("sent_steps", c_uint32),
        ("completed_steps", c_uint32),
        ("bytes_in_flight", c_uint32),
        ("window_bytes", c_uint32),
    ]


class CanHubTxnResult(Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("state", c_uint8),
        ("reason", c_uint8),
        ("has_rx_frame", c_uint8),
        ("reserved", c_uint8),
        ("failing_seq", c_uint32),
        ("completed_steps", c_uint32),
        ("offending_origin", c_uint32),
        ("seen_bitmap", c_uint64),
        ("expected", c_uint64),
        ("got", c_uint64),
        ("rx_frame", CanHubFrame),
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

lib.canhub_txn_begin.restype = c_int32
lib.canhub_txn_begin.argtypes = [c_void_p, POINTER(CanHubTxnBegin), c_int32]

lib.canhub_txn_template.restype = c_int32
lib.canhub_txn_template.argtypes = [c_void_p, POINTER(CanHubTxnTemplate), c_int32]

lib.canhub_txn_send.restype = c_int32
lib.canhub_txn_send.argtypes = [c_void_p, POINTER(CanHubTxnStep), c_uint32, c_int32]

lib.canhub_txn_end.restype = c_int32
lib.canhub_txn_end.argtypes = [c_void_p, c_int32]

lib.canhub_txn_wait.restype = c_int32
lib.canhub_txn_wait.argtypes = [c_void_p, POINTER(CanHubTxnResult), c_int32]

lib.canhub_txn_progress.restype = c_int32
lib.canhub_txn_progress.argtypes = [c_void_p, POINTER(CanHubTxnProgress)]

lib.canhub_txn_abort.restype = c_int32
lib.canhub_txn_abort.argtypes = [c_void_p]


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
