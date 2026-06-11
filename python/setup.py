from setuptools import setup
from setuptools.dist import Distribution

from wheel.bdist_wheel import bdist_wheel


class BinaryDistribution(Distribution):
    def has_ext_modules(self):
        return True


class PlatformWheelWithoutAbi(bdist_wheel):
    def get_tag(self):
        _, _, platform_tag = super().get_tag()
        return "py3", "none", platform_tag


setup(
    distclass=BinaryDistribution,
    cmdclass={"bdist_wheel": PlatformWheelWithoutAbi},
)
