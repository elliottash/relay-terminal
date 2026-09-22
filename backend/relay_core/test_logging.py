# SPDX-License-Identifier: AGPL-3.0-or-later
"""Explicit test-runner isolation. Importing this module changes no environment."""
from contextlib import contextmanager
import os
import tempfile
import uuid


@contextmanager
def environment():
    """Yield a child environment; retain an explicitly marked test-owned XDG directory."""
    env = dict(os.environ)
    if env.get("RELAY_LOG_ORIGIN") == "test" and env.get("XDG_DATA_HOME"):
        yield env
        return
    with tempfile.TemporaryDirectory(prefix="relay-test-data-") as directory:
        env.update(XDG_DATA_HOME=directory, RELAY_LOG_ORIGIN="test",
                   RELAY_LOG_RUN_ID=uuid.uuid4().hex)
        yield env


@contextmanager
def runner_environment():
    """Scope isolation to the test runner; child workers inherit it and callers recover theirs."""
    keys = ("XDG_DATA_HOME", "RELAY_LOG_ORIGIN", "RELAY_LOG_RUN_ID")
    previous = {key: os.environ.get(key) for key in keys}
    with environment() as env:
        for key in keys:
            if key in env:
                os.environ[key] = env[key]
        try:
            yield
        finally:
            for key, value in previous.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value
