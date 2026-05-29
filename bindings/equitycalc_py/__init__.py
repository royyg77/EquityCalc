"""EquityCalc Python bindings.

Imports from the compiled C++ extension module `_equitycalc`. The extension is
built via CMake (see ../CMakeLists.txt) and installed by setuptools as part of
this package.
"""

from _equitycalc import version  # noqa: F401

__all__ = ["version"]
__version__ = "0.1.0-dev"
