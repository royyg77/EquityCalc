"""Smoke test for the compiled bindings."""

import equitycalc_py


def test_version_string():
    v = equitycalc_py.version()
    assert isinstance(v, str)
    assert len(v) > 0


def test_package_version():
    assert equitycalc_py.__version__
