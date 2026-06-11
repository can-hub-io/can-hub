"""ctypes bindings over the libcanhub C ABI (include/canhub.h, version 1).

The shared library is resolved in order: the CANHUB_LIBRARY environment
variable, the copy bundled inside this package, the system library path.
"""

import ctypes
import os
from ctypes import POINTER, Structure, c_char, c_char_p, c_int32, c_size_t, c_uint8, c_uint32, c_uint64, c_void_p

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

OPEN_FLAG_NO_ECHO = 1 << 0
OPEN_FLAG_WRITE = 1 << 1

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
        ("interface", c_char * INTERFACE_NAME_MAX),
    ]


class CanHubFilter(Structure):
    _fields_ = [
        ("can_id", c_uint32),
        ("can_mask", c_uint32),
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
    override = os.environ.get("CANHUB_LIBRARY")
    if override:
        return ctypes.CDLL(override)

    bundled = os.path.join(os.path.dirname(__file__), "libcanhub.so")
    if os.path.exists(bundled):
        return ctypes.CDLL(bundled)

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


def last_error(session):
    detail = lib.canhub_last_error(session)
    return detail.decode("utf-8", errors="replace") if detail else "unknown error"
