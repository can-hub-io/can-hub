import pytest

import canhub._native as native
from canhub.settings import BitTiming, InterfaceSettings, from_native, to_native


class TestToNative:
    def test_it_sets_only_the_fields_it_is_given(self):
        settings = to_native(bitrate=500000, sample_point=0.875)

        assert settings.fields == native.SETTING_BITRATE | native.SETTING_SAMPLE_POINT
        assert settings.bitrate == 500000
        assert settings.sample_point == 875

    def test_it_collects_the_modes_into_one_mask(self):
        settings = to_native(fd=True, listen_only=False)

        assert settings.fields == native.SETTING_CTRLMODE
        assert settings.ctrlmode_mask == native.CTRLMODE_FD | native.CTRLMODE_LISTEN_ONLY
        assert settings.ctrlmode_flags == native.CTRLMODE_FD

    def test_it_takes_the_link_state(self):
        settings = to_native(up=False)

        assert settings.fields == native.SETTING_LINK
        assert settings.link_up == 0

    def test_it_rejects_an_unknown_setting(self):
        with pytest.raises(ValueError, match="unknown setting presume_ack"):
            to_native(presume_ack=True)

    def test_it_sets_the_raw_timing_members_it_is_given(self):
        settings = to_native(bit_timing=BitTiming(tq=125, prop_seg=6, phase_seg1=7, phase_seg2=2))

        assert settings.fields == native.SETTING_BIT_TIMING
        assert settings.bit_timing.tq == 125
        assert settings.bit_timing.phase_seg2 == 2
        assert settings.bit_timing.sjw == 0

    def test_it_sets_the_termination(self):
        settings = to_native(termination=120)

        assert settings.fields == native.SETTING_TERMINATION
        assert settings.termination == 120

    def test_it_rejects_a_sample_point_outside_zero_and_one(self):
        with pytest.raises(ValueError):
            to_native(sample_point=87.5)

    def test_it_rejects_an_empty_request(self):
        with pytest.raises(ValueError, match="nothing to configure"):
            to_native()


class TestFromNative:
    def test_it_reports_only_the_fields_the_device_reports(self):
        reported = native.new_settings()
        reported.fields = native.SETTING_LINK | native.SETTING_TXQUEUELEN
        reported.link_up = 1
        reported.txqueuelen = 10

        settings = from_native(reported)

        assert settings == InterfaceSettings(up=True, txqueuelen=10)

    def test_it_names_the_state_and_the_modes(self):
        reported = native.new_settings()
        reported.fields = native.SETTING_STATE | native.SETTING_CTRLMODE | native.SETTING_SAMPLE_POINT
        reported.can_state = 3
        reported.ctrlmode_mask = native.CTRLMODE_FD | native.CTRLMODE_LOOPBACK
        reported.ctrlmode_flags = native.CTRLMODE_FD
        reported.sample_point = 875

        settings = from_native(reported)

        assert settings.state == "bus-off"
        assert settings.modes == {"fd": True, "loopback": False}
        assert settings.sample_point == 0.875

    def test_it_reports_the_timing_the_kernel_settled_on(self):
        reported = native.new_settings()
        reported.fields = native.SETTING_BIT_TIMING
        reported.bit_timing.tq = 125
        reported.bit_timing.brp = 5

        settings = from_native(reported)

        assert settings.bit_timing == BitTiming(tq=125, prop_seg=0, phase_seg1=0, phase_seg2=0, sjw=0, brp=5)
