# SPDX-License-Identifier: MIT
"""Shared pytest setup: import paths, testbed selection, failure artifacts."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests" / "framework"), str(ROOT / "src" / "hotspot")]

from tcu_testkit import load  # noqa: E402


def pytest_addoption(parser):
    g = parser.getgroup("tcu")
    g.addoption("--testbed", default="sim",
                help="'sim' or path to a testbed JSON (tests/testbeds/*.json)")
    g.addoption("--stress-iterations", type=int, default=25,
                help="iterations per stress case (nightly uses more)")


def pytest_configure(config):
    for m in ("connect", "roam", "hotspot", "stress", "bt"):
        config.addinivalue_line("markers", f"{m}: {m} test cases")


@pytest.fixture(scope="session")
def testbed(request):
    return load(request.config.getoption("--testbed"))


@pytest.fixture(scope="session")
def stress_iterations(request):
    return request.config.getoption("--stress-iterations")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    """On failure attach the DUT journal so the Jenkins report is self-contained."""
    outcome = yield
    rep = outcome.get_result()
    if rep.when == "call" and rep.failed and "testbed" in item.funcargs:
        tb = item.funcargs["testbed"]
        try:
            logs = tb.dut.collect_logs("-5min")
        except Exception as e:  # never mask the real failure
            logs = f"log collection failed: {e}"
        rep.sections.append(("DUT journal (last 5 min)", logs[-20000:]))
