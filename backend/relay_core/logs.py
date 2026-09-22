# SPDX-License-Identifier: AGPL-3.0-or-later
"""Rotating diagnostic log for the worker (and anything else in the backend).

Relay used to write only to stderr, which is thrown away when it is started from a desktop
launcher: a stalled turn left nothing to look at afterwards (issue SQAM, 2026-09-17). This module
writes `$XDG_DATA_HOME/relay/logs/worker.log` (default `~/.local/share/relay/logs/`), 5 MiB × 3
rotation, mode 0600 in a 0700 directory.

**What must never be written here**: prompts, model answers, reasoning text, tool arguments, tool
output, file contents, terminal output, API keys, and anything typed in password mode. Log
identifiers, names, counts, durations, event types and error *types*. `scrub()` is a second line of
defence, not a licence: it masks things that look like keys in any message that does get logged.

The `verbose` level is the one exception and it is opt-in: with it, `prompt()` records prompt text.
Nothing else changes, and tool output and file contents are never logged at any level.

Several workers share one file, so each record is written under an advisory lock and the handler
reopens the file when another process has rotated it.
"""
from __future__ import annotations

import faulthandler
import hashlib
from functools import lru_cache
import logging
import os
import re
import sys
import threading
import uuid
import time
from logging.handlers import RotatingFileHandler
from pathlib import Path

_RUN_ID = uuid.uuid4().hex


def _identity(value: str) -> str:
    # Identity fields are labels, never paths, arbitrary messages or credentials.
    return value if re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", value) else "unknown"


@lru_cache(maxsize=1)
def _build_id() -> str:
    """Fingerprint the loaded backend source tree when no packaged build ID is supplied."""
    try:
        root = Path(__file__).resolve().parent
        digest = hashlib.sha256()
        for path in sorted(root.rglob("*.py")) + [root.parent / "worker.py"]:
            digest.update(str(path.relative_to(root.parent)).encode())
            digest.update(path.read_bytes())
        return "source-" + digest.hexdigest()[:20]
    except OSError:
        return "unknown"


def context() -> dict[str, str]:
    origin = os.environ.get("RELAY_LOG_ORIGIN", "interactive")
    return {"origin": origin if origin in ("interactive", "test", "qa") else "unknown",
            "run_id": _identity(os.environ.get("RELAY_LOG_RUN_ID", _RUN_ID)),
            "build_id": _identity(os.environ.get("RELAY_BUILD_ID") or _build_id())}

MAX_BYTES = 5 * 1024 * 1024
BACKUPS = 3
DIR_MODE = 0o700
FILE_MODE = 0o600

# off < error < info < debug < verbose. "verbose" is "debug plus prompt text" (opt-in).
LEVELS = ("off", "error", "info", "debug", "verbose")
_LEVEL_NUMBERS = {"off": logging.CRITICAL + 10, "error": logging.ERROR, "info": logging.INFO,
                  "debug": logging.DEBUG, "verbose": logging.DEBUG}
DEFAULT_LEVEL = "info"

_lock = threading.Lock()
_state = {"level": DEFAULT_LEVEL, "pane": "", "configured": False}
# Until configure() runs (tests, library use) nothing is written anywhere, not even to stderr.
logging.getLogger("relay").addHandler(logging.NullHandler())

# Things that must never reach the file even by accident. Checked against the formatted message.
_SECRETS = (
    re.compile(r"\bsk-[A-Za-z0-9_\-]{12,}"),                                   # OpenAI/OpenRouter/Anthropic
    re.compile(r"\bgsk_[A-Za-z0-9]{12,}"),                                     # Groq
    re.compile(r"\bxai-[A-Za-z0-9]{12,}"),                                     # xAI
    re.compile(r"(?i)\bbearer\s+[A-Za-z0-9._\-]{8,}"),                         # Authorization values
    re.compile(r"(?i)\b(api[_-]?key|authorization|token|secret|password)\s*[=:]\s*\S+"),
    re.compile(r"\b[A-Za-z0-9_\-]{16,}\.[A-Za-z0-9_\-]{16,}\.[A-Za-z0-9_\-]{16,}\b"),  # JWTs
)


def validate_level(value) -> str:
    if not isinstance(value, str) or value.strip().lower() not in LEVELS:
        raise ValueError("Log level must be one of: " + ", ".join(LEVELS) + ".")
    return value.strip().lower()


def level() -> str:
    with _lock:
        return _state["level"]


def verbose() -> bool:
    """Whether prompt text may be logged. Opt-in; documented in README and ARCHITECTURE."""
    return level() == "verbose"


def scrub(text) -> str:
    """Mask anything that looks like a credential. Applied to every record that is written."""
    out = str(text)
    for pattern in _SECRETS:
        out = pattern.sub(lambda m: m.group(0).split("=")[0].split(":")[0] + "=<redacted>"
                          if "=" in m.group(0) or ":" in m.group(0) else "<redacted>", out)
    return out


def log_dir() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return Path(base) / "relay" / "logs"


class _Formatter(logging.Formatter):
    """ISO-8601 UTC timestamps; the pane id travels on the record."""
    converter = time.gmtime

    def formatTime(self, record, datefmt=None):
        return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(record.created)) + f".{int(record.msecs):03d}Z"

    def format(self, record):
        record.pane = getattr(record, "pane", None) or _state["pane"] or "-"
        for key, value in context().items():
            setattr(record, key, value)
        return scrub(super().format(record))


class _SharedRotatingHandler(RotatingFileHandler):
    """RotatingFileHandler that several processes may share.

    Every record is written while holding an advisory lock on a sidecar file, and the handler
    reopens its stream when another process rotated the file under it (otherwise it would keep
    appending to an already-rotated inode). New files are created 0600.
    """

    def __init__(self, filename):
        super().__init__(filename, maxBytes=MAX_BYTES, backupCount=BACKUPS, encoding="utf-8", delay=True)
        # Hidden, so the folder the "Open log folder" action opens shows only log files.
        name = Path(filename).name
        self._lock_path = str(Path(filename).with_name(f".{name}.lock"))

    def _open(self):
        stream = super()._open()
        try:
            os.chmod(self.baseFilename, FILE_MODE)
        except OSError:
            pass
        return stream

    def _inode(self):
        try:
            return os.stat(self.baseFilename).st_ino
        except OSError:
            return None

    def emit(self, record):
        from . import filelock as fcntl
        handle = None
        try:
            fd = os.open(self._lock_path, os.O_CREAT | os.O_RDWR, FILE_MODE)
            handle = os.fdopen(fd, "r+b")
            fcntl.flock(handle, fcntl.LOCK_EX)
            if self.stream is not None and self._inode() != getattr(self, "_opened_inode", None):
                self.close()                      # another worker rotated the file; follow it
            super().emit(record)
            self._opened_inode = self._inode()
            if os.name == "nt":
                # Windows cannot rename another worker's open log during rotation.
                # The lock covers opening, writing and closing on this platform.
                self.close()
        except OSError:
            pass
        finally:
            if handle is not None:
                try:
                    handle.close()                # releases the flock
                except OSError:
                    pass


# The file faulthandler writes into, kept open for the life of the process: it writes from the
# signal handler, to a file descriptor, and a closed one would be the last thing it ever did.
_faults_handle = None


def enable_fault_reports(component: str = "worker") -> Path | None:
    """A fatal signal in this process leaves a Python traceback behind. Returns the file, or None.

    Nothing else catches one. A segfault never reaches the logging module, and the GUI reads the
    worker's stderr and throws it away on purpose (no provider error bodies in the log), so a
    worker that dies this way leaves nothing at all — which is what about 1,200 processes a day on
    this machine do: signal 11 against `backend/worker.py` in /var/log/apport.log, not one line in
    worker.log, and `worker_exit ... crashed=0` in the GUI's. faulthandler writes every thread's
    Python stack straight to the descriptor from the handler itself, which is the one thing that
    still works at that point.

    Its own file rather than worker.log: the rotating handler would move the file out from under a
    descriptor that can no longer be reopened, and a raw traceback is not a log line. Rotated at a
    megabyte, which is hundreds of reports.
    """
    global _faults_handle
    if _faults_handle is not None:
        return Path(_faults_handle.name)
    try:
        directory = log_dir()
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / f"{component}-faults.log"
        if path.exists() and path.stat().st_size > 1024 * 1024:
            path.replace(directory / f"{component}-faults.log.1")
        handle = open(path, "a", buffering=1, encoding="utf-8", errors="replace")
        try:
            os.chmod(path, FILE_MODE)
        except OSError:
            pass
        faulthandler.enable(file=handle, all_threads=True)
        _faults_handle = handle
        return path
    except (OSError, ValueError, RuntimeError):
        return None   # diagnostics are never the reason a worker fails to start


def configure(component: str = "worker", *, pane: str | None = None, level: str | None = None) -> logging.Logger:
    """Install the rotating handler for this process. Safe to call more than once.

    ``component`` names the file (worker → worker.log). ``pane`` is recorded on every line.
    ``level`` defaults to $RELAY_LOG_LEVEL, then "info"; an unknown value falls back to "info".
    """
    chosen = level or os.environ.get("RELAY_LOG_LEVEL") or DEFAULT_LEVEL
    try:
        chosen = validate_level(chosen)
    except ValueError:
        chosen = DEFAULT_LEVEL
    with _lock:
        _state["level"] = chosen
        _state["pane"] = str(pane or os.environ.get("RELAY_PANE_ID") or "")[:64]
    logger = logging.getLogger("relay")
    logger.propagate = False
    for handler in list(logger.handlers):
        logger.removeHandler(handler)
        try:
            handler.close()
        except OSError:
            pass
    logger.setLevel(_LEVEL_NUMBERS[chosen])
    if chosen == "off":
        logger.addHandler(logging.NullHandler())
        _state["configured"] = True
        return logger
    try:
        directory = log_dir()
        directory.mkdir(parents=True, exist_ok=True)
        try:
            os.chmod(directory, DIR_MODE)
        except OSError:
            pass
        handler = _SharedRotatingHandler(directory / f"{component}.log")
        handler.setFormatter(_Formatter("%(asctime)s %(levelname)s %(name)s pane=%(pane)s %(message)s origin=%(origin)s run_id=%(run_id)s build_id=%(build_id)s"))
        logger.addHandler(handler)
        # Same directory, same choice: logging off means no files, this one included.
        enable_fault_reports(component)
    except OSError as exc:
        # A read-only or missing data directory must never stop a turn.
        print(f"relay: no log file ({type(exc).__name__})", file=sys.stderr)
        logger.addHandler(logging.NullHandler())
    _state["configured"] = True
    return logger


def get(name: str = "") -> logging.Logger:
    return logging.getLogger("relay." + name if name else "relay")


def fields(**values) -> str:
    """`key=value` pairs in a stable order; None and "" are dropped and values are quoted when needed."""
    parts = []
    for key, value in values.items():
        if value is None or value == "":
            continue
        if isinstance(value, float):
            text = f"{value:.3f}".rstrip("0").rstrip(".")
        else:
            text = str(value)
        text = text.replace("\n", " ").replace("\r", " ")
        if any(c in text for c in " \"="):
            text = '"' + text.replace('"', "'")[:500] + '"'
        parts.append(f"{key}={text[:500]}")
    return " ".join(parts)


def event(logger: logging.Logger, message: str, *, level_name: str = "info", **values) -> None:
    """One structured line: `<message> key=value …`. Never pass prompts or tool output."""
    line = message if not values else message + " " + fields(**values)
    getattr(logger, "error" if level_name == "error" else "debug" if level_name == "debug" else "info")(line)


def prompt(logger: logging.Logger, message: str, text, **values) -> None:
    """Prompt text, only at the opt-in `verbose` level. A no-op at every other level."""
    if not verbose():
        return
    event(logger, message, level_name="debug", text=str(text)[:2000], **values)
