"""Interface settings: the python view of what can-hub can configure on a bus.

Keyword names follow ``ip link set ... type can``: ``bitrate``,
``sample_point`` (a fraction, 0.875), ``data_bitrate``, ``data_sample_point``,
the control modes ``fd``, ``listen_only``, ``loopback``, ``one_shot``,
``triple_sampling`` and ``berr_reporting`` (booleans), ``restart_ms``,
``txqueuelen``, ``termination`` (ohms, 0 = off) and ``up``. Raw timing goes
in ``bit_timing`` / ``data_bit_timing`` as a :class:`BitTiming`, instead of
a bitrate (``sjw`` alone may go with one). Everything passed is applied in
one step.
"""

from dataclasses import dataclass, field
from typing import Mapping, Optional

from . import _native as native

SAMPLE_POINT_SCALE = 1000

MODES = {
    "fd": native.CTRLMODE_FD,
    "listen_only": native.CTRLMODE_LISTEN_ONLY,
    "loopback": native.CTRLMODE_LOOPBACK,
    "one_shot": native.CTRLMODE_ONE_SHOT,
    "triple_sampling": native.CTRLMODE_TRIPLE_SAMPLING,
    "berr_reporting": native.CTRLMODE_BERR_REPORTING,
}

STATES = ("error-active", "error-warning", "error-passive", "bus-off", "stopped", "sleeping")


TIMING_MEMBERS = ("tq", "prop_seg", "phase_seg1", "phase_seg2", "sjw", "brp")


@dataclass(frozen=True)
class BitTiming:
    """Time quantum in ns and segment lengths in quanta; ``None`` = not given."""

    tq: Optional[int] = None
    prop_seg: Optional[int] = None
    phase_seg1: Optional[int] = None
    phase_seg2: Optional[int] = None
    sjw: Optional[int] = None
    brp: Optional[int] = None


@dataclass(frozen=True)
class InterfaceSettings:
    """The settings a device reports; ``None`` where it reports nothing."""

    up: Optional[bool] = None
    state: Optional[str] = None
    bitrate: Optional[int] = None
    sample_point: Optional[float] = None
    data_bitrate: Optional[int] = None
    data_sample_point: Optional[float] = None
    modes: Mapping[str, bool] = field(default_factory=dict)
    restart_ms: Optional[int] = None
    txqueuelen: Optional[int] = None
    termination: Optional[int] = None
    bit_timing: Optional[BitTiming] = None
    data_bit_timing: Optional[BitTiming] = None


def to_native(
    bitrate: Optional[int] = None,
    sample_point: Optional[float] = None,
    data_bitrate: Optional[int] = None,
    data_sample_point: Optional[float] = None,
    restart_ms: Optional[int] = None,
    txqueuelen: Optional[int] = None,
    termination: Optional[int] = None,
    bit_timing: Optional[BitTiming] = None,
    data_bit_timing: Optional[BitTiming] = None,
    up: Optional[bool] = None,
    **modes: bool,
) -> "native.CanHubInterfaceSettings":
    unknown = sorted(set(modes) - set(MODES))
    if unknown:
        raise ValueError(f"unknown setting {unknown[0]}")

    settings = native.new_settings()
    _set_number(settings, native.SETTING_BITRATE, "bitrate", bitrate)
    _set_number(settings, native.SETTING_DATA_BITRATE, "data_bitrate", data_bitrate)
    _set_number(settings, native.SETTING_RESTART_MS, "restart_ms", restart_ms)
    _set_number(settings, native.SETTING_TXQUEUELEN, "txqueuelen", txqueuelen)
    _set_number(settings, native.SETTING_TERMINATION, "termination", termination)
    _set_timing(settings.bit_timing, bit_timing)
    _set_timing(settings.data_bit_timing, data_bit_timing)
    if bit_timing is not None:
        settings.fields |= native.SETTING_BIT_TIMING
    if data_bit_timing is not None:
        settings.fields |= native.SETTING_DATA_BIT_TIMING
    _set_number(settings, native.SETTING_SAMPLE_POINT, "sample_point", _scaled(sample_point))
    _set_number(settings, native.SETTING_DATA_SAMPLE_POINT, "data_sample_point", _scaled(data_sample_point))
    if up is not None:
        settings.fields |= native.SETTING_LINK
        settings.link_up = 1 if up else 0
    for name, enabled in modes.items():
        settings.fields |= native.SETTING_CTRLMODE
        settings.ctrlmode_mask |= MODES[name]
        if enabled:
            settings.ctrlmode_flags |= MODES[name]

    if settings.fields == 0:
        raise ValueError("nothing to configure")
    return settings


def from_native(settings: "native.CanHubInterfaceSettings") -> InterfaceSettings:
    fields = settings.fields
    modes = {}
    if fields & native.SETTING_CTRLMODE:
        modes = {
            name: bool(settings.ctrlmode_flags & flag)
            for name, flag in MODES.items()
            if settings.ctrlmode_mask & flag
        }

    return InterfaceSettings(
        up=bool(settings.link_up) if fields & native.SETTING_LINK else None,
        state=_state_name(settings.can_state) if fields & native.SETTING_STATE else None,
        bitrate=settings.bitrate if fields & native.SETTING_BITRATE else None,
        sample_point=_fraction(settings.sample_point) if fields & native.SETTING_SAMPLE_POINT else None,
        data_bitrate=settings.data_bitrate if fields & native.SETTING_DATA_BITRATE else None,
        data_sample_point=_fraction(settings.data_sample_point) if fields & native.SETTING_DATA_SAMPLE_POINT else None,
        modes=modes,
        restart_ms=settings.restart_ms if fields & native.SETTING_RESTART_MS else None,
        txqueuelen=settings.txqueuelen if fields & native.SETTING_TXQUEUELEN else None,
        termination=settings.termination if fields & native.SETTING_TERMINATION else None,
        bit_timing=_timing(settings.bit_timing) if fields & native.SETTING_BIT_TIMING else None,
        data_bit_timing=_timing(settings.data_bit_timing) if fields & native.SETTING_DATA_BIT_TIMING else None,
    )


def _set_number(settings, flag: int, name: str, value: Optional[int]) -> None:
    if value is None:
        return
    settings.fields |= flag
    setattr(settings, name, value)


def _set_timing(native_timing, timing: Optional[BitTiming]) -> None:
    if timing is None:
        return
    for member in TIMING_MEMBERS:
        value = getattr(timing, member)
        if value is not None:
            setattr(native_timing, member, value)


def _timing(native_timing) -> BitTiming:
    return BitTiming(**{member: getattr(native_timing, member) for member in TIMING_MEMBERS})


def _scaled(sample_point: Optional[float]) -> Optional[int]:
    if sample_point is None:
        return None
    if not 0 < sample_point < 1:
        raise ValueError(f"sample point {sample_point} is not a fraction between 0 and 1")
    return round(sample_point * SAMPLE_POINT_SCALE)


def _fraction(scaled: int) -> float:
    return scaled / SAMPLE_POINT_SCALE


def _state_name(can_state: int) -> str:
    return STATES[can_state] if can_state < len(STATES) else "unknown"
