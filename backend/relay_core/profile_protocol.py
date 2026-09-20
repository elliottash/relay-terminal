# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Profile button's two requests (docs/AGENT-SESSIONS-PROTOCOL.md section 31.9, card #7BM4).

A sibling of `tests_protocol.py` and wired from `board_protocol.py` the same one line: the board
worker runs `scripts/relay-profile` through `jobs.JobTable` and streams what it prints, because
"profile the project" is minutes of compiling or sampling and the board must stay usable while it
happens.  The shape is `board_cleanup`'s — request, `started`, `progress` lines, one `finished`
carrying the summary — and the numbers it ends with are the ones the pane draws in a table and
the card records under `## Profile`.

    profile_run {target, args?}   -> profile  started / progress… / finished | error
    profile_stop                  -> profile  finished (state "stopped") | error

Four targets, because "profile the project" is four different things here (the research in
`docs/SWITCHBOARD-TOOLING-RESEARCH.md` section 4.2, and the owner's answer on the card: the button
asks, and the build ships first).  Each is a fixed argument vector — the request names a target,
never a command — so nothing a GUI or an agent sends can turn this into "run anything".

One run at a time.  A second `profile_run` while one is in flight is refused with a sentence
rather than queued: two builds in one build directory, or two `perf record`s, are not two
profiles, they are one wrong one.
"""
from __future__ import annotations

import datetime
import json
import os
import shlex
import threading
import re
import time
from pathlib import Path
from typing import Callable

from . import jobs as J

#: The requests this class answers.  `board_protocol.TYPES` includes them and delegates here.
TYPES = frozenset({"profile_run", "profile_stop"})

#: What each target runs, what it is called on screen, and the one line under it in the menu that
#: says what it does and roughly how long — the GUI reads this from here so the menu and the
#: worker can never describe different things.
TARGETS: dict[str, dict] = {
    "build": {
        "label": "Build (this machine)",
        "detail": "Per-target compile times from a Ninja build of `relay` in a directory of the "
                  "tool's own, never the shared build/. Minutes on a cold build, seconds warm.",
        "args": ["build"],
    },
    "build-remote": {
        "label": "Build (another machine)",
        "detail": "The same table for the committed tree, built on a machine you name as ssh "
                  "knows it — asked once, then remembered — through scripts/relay-remote-tests "
                  "--build-only. About four minutes cold.",
        "args": ["build"],
    },
    "tests": {
        "label": "Python tests",
        "detail": "py-spy over `python3 -m unittest`, or cProfile where py-spy is missing. As "
                  "long as the tests take.",
        "args": ["tests"],
    },
    "app": {
        "label": "The app",
        "detail": "Relay itself under `sudo -n perf record`, started fresh and stopped when you "
                  "say so. Use it, then press Stop.",
        "args": ["app"],
    },
}

#: `docs/qa_evidence/<date>-profile-<target>/`, the convention this repo already runs on
#: (docs/PROFILING.md section 5): the summary table is committed onto a card, the raw samples are
#: not.
EVIDENCE_ROOT = "docs/qa_evidence"

MAX_ARGS = 24                   # extra arguments one request may carry
#: `build-remote` needs a `host` on the request (or RELAY_REMOTE_HOST in the worker's
#: environment): the machine is the user's to name, never this file's.  A hostname, or user@host.
HOST_PATTERN = re.compile(r"^(?:[A-Za-z0-9._-]+@)?[A-Za-z0-9._-]{1,253}$")
MAX_ARG_LENGTH = 200
RUN_TIMEOUT = 3 * 3600.0        # seconds one profile may take before it is stopped
MAX_ROWS = 25                   # rows of the summary the `finished` event carries
PROGRESS_LINE_CAP = 400         # characters of one streamed line

#: Arguments a request may add to a target's own.  Anything else is refused by name, so a request
#: cannot reach `--binary /bin/sh` or an `--out` outside the evidence tree.
ALLOWED_FLAGS = {"--stop-after", "--rev", "--target", "--limit", "--time-trace", "--build-dir",
                 "--host"}


class ProfileError(ValueError):
    """A refusal the caller should read: one sentence, no traceback."""


class _Run:
    """The profile in flight: what it is, where it writes, and how to stop it."""

    def __init__(self, target: str, out: Path, command: str):
        self.target = target
        self.out = out
        self.command = command
        self.started = time.time()
        self.lines: list[str] = []
        self.cancel = threading.Event()
        self.finished = threading.Event()
        self.job = None
        self.stopped = False


class ProfileCommands:
    """`profile_run` and `profile_stop` for one board, built exactly as `TestsCommands` is.

    Handed its collaborators rather than finding them: the project root, the `emit` that puts an
    event on the wire, and a `JobTable` (or a factory for one) so a profile's process group is not
    the agent's.  Nothing here reads a settings file.
    """

    def __init__(self, project, emit: Callable[[dict], None] | None = None, jobs=None, *,
                 script=None, clock: Callable[[], float] = time.time):
        self.project = Path(os.path.abspath(Path(project).expanduser()))
        self.emit = emit or (lambda event: None)
        self._jobs = jobs
        self._script = Path(script) if script else None
        self.clock = clock
        self._lock = threading.Lock()
        self._run: _Run | None = None

    # ---- collaborators ---------------------------------------------------------
    @property
    def jobs(self) -> J.JobTable:
        table = self._jobs
        if table is None or callable(table):
            table = table() if callable(table) else J.JobTable()
            self._jobs = table
        return table

    def script(self) -> Path:
        """`scripts/relay-profile`, of this project or of Relay's own checkout.

        A project that is not Relay has no such script, and profiling it is Relay's job rather
        than the project's, so the copy beside this module is the fallback.
        """
        if self._script is not None:
            return self._script
        local = self.project / "scripts" / "relay-profile"
        if local.is_file():
            return local
        return Path(__file__).resolve().parents[2] / "scripts" / "relay-profile"

    # ---- dispatch --------------------------------------------------------------
    @staticmethod
    def handles(kind) -> bool:
        return kind in TYPES

    def dispatch(self, request: dict) -> bool:
        kind = request.get("type")
        if kind not in TYPES:
            return False
        try:
            if kind == "profile_run":
                self.start(request.get("target"), request.get("args"),
                           host=request.get("host"), rid=request.get("id"))
            else:
                self.stop()
        except ProfileError as exc:
            self._emit({"state": "error", "message": str(exc)},
                       target=str(request.get("target") or ""), rid=request.get("id"))
        return True

    def shutdown(self) -> None:
        """Stop whatever is running; the worker is going."""
        run = self._run
        if run is not None and not run.finished.is_set():
            run.cancel.set()
            if run.job is not None:
                try:
                    self.jobs.stop(run.job)
                except Exception:                            # pragma: no cover - already gone
                    pass

    def running(self) -> bool:
        run = self._run
        return run is not None and not run.finished.is_set()

    # ---- starting and stopping -------------------------------------------------
    def evidence_dir(self, target: str) -> Path:
        day = datetime.date.fromtimestamp(self.clock()).isoformat()
        return self.project / EVIDENCE_ROOT / f"{day}-profile-{target}"

    @staticmethod
    def _extra(args) -> list[str]:
        """The request's own arguments, each one checked by name."""
        if args is None:
            return []
        if not isinstance(args, (list, tuple)):
            raise ProfileError("profile_run `args` must be a list of strings.")
        if len(args) > MAX_ARGS:
            raise ProfileError(f"profile_run takes at most {MAX_ARGS} extra arguments.")
        out: list[str] = []
        expecting = False
        for value in args:
            if not isinstance(value, str) or len(value) > MAX_ARG_LENGTH:
                raise ProfileError("profile_run arguments must be strings of at most "
                                   f"{MAX_ARG_LENGTH} characters.")
            if value.startswith("-"):
                flag = value.split("=", 1)[0]
                if flag not in ALLOWED_FLAGS:
                    raise ProfileError(f"profile_run will not pass `{flag}` to relay-profile.")
                expecting = "=" not in value and flag != "--time-trace"
            elif not expecting:
                raise ProfileError(f"profile_run does not take a bare argument ({value!r}); "
                                   "name a target instead.")
            else:
                expecting = False
            out.append(value)
        return out

    def start(self, target, args=None, *, host=None, rid=None) -> _Run:
        name = str(target or "").strip()
        if name not in TARGETS:
            raise ProfileError("profile_run `target` is one of "
                               + ", ".join(sorted(TARGETS)) + ".")
        extra = self._extra(args)
        if name == "build-remote":
            machine = str(host or os.environ.get("RELAY_REMOTE_HOST") or "").strip()
            if not machine:
                raise ProfileError("`build-remote` needs `host`: the machine to build on, as ssh "
                                   "names it. The Profile menu asks for it once; a worker can "
                                   "also read RELAY_REMOTE_HOST.")
            if not HOST_PATTERN.match(machine):
                raise ProfileError(f"`host` {machine!r} is not a hostname.")
            extra = ["--host", machine, *extra]
        script = self.script()
        if not script.is_file():
            raise ProfileError(f"There is no {script} to run; this project has no profiler.")
        with self._lock:
            if self.running():
                raise ProfileError(f"A profile is already running ({self._run.target}); stop it "
                                   "before starting another.")
            out = self.evidence_dir(name)
            argv = ["/bin/bash", str(script), *TARGETS[name]["args"], "--out", str(out), *extra]
            run = _Run(name, out, " ".join(shlex.quote(part) for part in argv[1:]))
            self._run = run
        threading.Thread(target=self._execute, name="relay-profile-run",
                         args=(run, argv, rid), daemon=True).start()
        return run

    def stop(self) -> bool:
        run = self._run
        if run is None or run.finished.is_set():
            raise ProfileError("There is no profile running to stop.")
        run.stopped = True
        run.cancel.set()
        if run.job is not None:
            try:
                self.jobs.stop(run.job)
            except Exception:                                # pragma: no cover - already gone
                pass
        return True

    # ---- the run itself --------------------------------------------------------
    def _execute(self, run: _Run, argv: list[str], rid) -> None:
        """One profile, start to finish, on its own thread.  Never raises out of the thread."""
        try:
            run.out.mkdir(parents=True, exist_ok=True)
            self._emit({"state": "started", "out": str(run.out), "command": run.command,
                        "label": TARGETS[run.target]["label"]}, target=run.target, rid=rid)
            env = dict(os.environ)
            env.setdefault("PYTHONPATH", str(self.project / "backend"))
            # The script lives in Relay's checkout even when the board is some other project's,
            # so it is told which project it is profiling rather than inferring it from its own
            # path (`scripts/relay-profile`, RELAY_PROFILE_PROJECT).
            env["RELAY_PROFILE_PROJECT"] = str(self.project)
            job = self.jobs.start(run.command, str(self.project), env, argv=argv)
            run.job = job
            buffer = [""]

            def on_text(text: str) -> None:
                buffer[0] += text
                *lines, buffer[0] = buffer[0].split("\n")
                for line in lines:
                    line = line.strip()
                    if not line:
                        continue
                    run.lines.append(line[:PROGRESS_LINE_CAP])
                    self._emit({"state": "progress", "line": line[:PROGRESS_LINE_CAP]},
                               target=run.target, rid=rid)

            self.jobs.wait(job, RUN_TIMEOUT, run.cancel, live=on_text)
            if job.running:
                run.cancel.set()
                self.jobs.stop(job)
            if buffer[0].strip():
                run.lines.append(buffer[0].strip()[:PROGRESS_LINE_CAP])
            summary = self.read_summary(run.out)
            if run.stopped or job.stopped:
                state, message = "stopped", "the profile was stopped"
            elif summary is None:
                state = "error"
                message = self._failure_message(run, job)
            else:
                state, message = "finished", summary.get("line") or "the profile finished"
            event = {"state": state, "out": str(run.out), "message": message,
                     "command": run.command, "label": TARGETS[run.target]["label"]}
            if summary is not None:
                event["summary"] = summary
            self._emit(event, target=run.target, rid=rid)
        except Exception as exc:                             # pragma: no cover - belt and braces
            self._emit({"state": "error", "out": str(run.out),
                        "message": f"The profile failed: {type(exc).__name__}: {str(exc)[:300]}"},
                       target=run.target, rid=rid)
        finally:
            run.finished.set()

    @staticmethod
    def _failure_message(run: _Run, job) -> str:
        """Why there is no summary, in the script's own last words.

        A script that exited non-zero said why in one of its own lines, so the last of those is
        what the pane shows — "ninja is not installed", "the build failed; see build.log" — with
        the line that is only the evidence directory skipped, because it says nothing. A script
        that exited **zero** and still left no rows is a different fault, and gets the sentence
        that names it rather than its last progress line.
        """
        code = getattr(job, "exit_code", None)
        for line in (reversed(run.lines) if code else ()):
            if not line.startswith("relay-profile:"):
                continue
            rest = line.split(":", 1)[1].strip()
            if "flame graph" in line or rest.startswith("/") or rest == str(run.out):
                continue
            return line
        return f"relay-profile wrote no summary (exit {code if code is not None else '?'})."

    # ---- what the evidence directory says --------------------------------------
    def read_summary(self, out: Path) -> dict | None:
        """`rows.json` and `meta.json` as the `finished` event's `summary`, or None.

        The script is the authority for the arithmetic (`relay_core.profile_convert`), so this
        reads what it wrote rather than parsing its output a second time.
        """
        try:
            folded = json.loads((Path(out) / "rows.json").read_text())
        except (OSError, ValueError):
            return None
        rows = folded.get("rows")
        if not isinstance(rows, list) or not rows:
            return None
        meta = {}
        try:
            meta = json.loads((Path(out) / "meta.json").read_text())
        except (OSError, ValueError):
            meta = {}
        summary = {"kind": str(folded.get("kind") or "profile"),
                   "rows": [self._row(row) for row in rows[:MAX_ROWS]],
                   "total_rows": int(folded.get("total_rows") or len(rows)),
                   "wall": float(folded.get("wall") or 0.0),
                   "out": str(out),
                   "line": self.summary_line(folded, meta),
                   "markdown": self.markdown(folded, meta)}
        for key in ("steps", "samples", "sum"):
            if key in folded:
                summary[key] = folded[key]
        for key in ("host", "commit", "started", "finished", "command", "raw", "tool_versions"):
            if key in meta:
                summary[key] = meta[key]
        flame = self._flame_file(out, meta)
        if flame:
            summary["flame"] = str(flame)
        return summary

    @staticmethod
    def _row(row: dict) -> dict:
        out = {"name": str(row.get("name") or ""),
               "self": float(row.get("self") or 0.0),
               "total": float(row.get("total") or 0.0),
               "self_pct": float(row.get("self_pct") or 0.0),
               "total_pct": float(row.get("total_pct") or 0.0)}
        if row.get("file"):
            out["file"] = str(row["file"])
        if row.get("line"):
            out["line"] = int(row["line"])
        return out

    @staticmethod
    def _flame_file(out: Path, meta: dict):
        """What `scripts/relay-speedscope` should be handed: the first raw file it can read."""
        for name in (meta.get("raw") or []):
            if str(name).endswith((".speedscope.json", ".folded", ".trace.json")):
                path = Path(out) / str(name)
                if path.is_file():
                    return path
        return None

    @staticmethod
    def summary_line(folded: dict, meta: dict) -> str:
        """nextest's shape, for the board's notice line and the pane's header."""
        host = meta.get("host") or ""
        where = f" · {host}" if host else ""
        if folded.get("kind") == "build":
            return (f"{folded.get('steps', 0)} steps · {_secs(folded.get('wall'))} wall · "
                    f"{_secs(folded.get('sum'))} of compile time{where}")
        return (f"{folded.get('total_rows', len(folded.get('rows') or []))} functions · "
                f"{folded.get('samples', 0)} samples · {_secs(folded.get('wall'))}{where}")

    @staticmethod
    def markdown(folded: dict, meta: dict, *, limit: int = MAX_ROWS) -> str:
        """The `## Profile` block a card records, built from the same rows the pane draws."""
        from . import profile_convert as C
        when = str(meta.get("finished") or meta.get("started") or "")[:16].replace("T", " ")
        head = f"### {meta.get('target') or folded.get('kind') or 'profile'}"
        if when:
            head += f" · {when}"
        if meta.get("host"):
            head += f" · {meta['host']}"
        if meta.get("commit"):
            head += f" · `{meta['commit']}`"
        return f"{head}\n\n{C.markdown_table(folded, limit=limit)}\n"

    # ---- events ----------------------------------------------------------------
    def _emit(self, fields: dict, *, target: str, rid=None) -> None:
        event = {"event": "profile", "target": target, **fields}
        if rid is not None:
            event["id"] = rid
        self.emit(event)


def _secs(value) -> str:
    try:
        seconds = float(value or 0.0)
    except (TypeError, ValueError):
        return "—"
    if seconds >= 60:
        return f"{int(seconds // 60)}m {seconds % 60:.0f}s"
    return f"{seconds:.1f} s"
