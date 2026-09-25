"""A pane's text journal (card #HEY7), the Python side: read, print, seal and recompress.

The GUI writes the journal (src/TextJournal.{h,cpp}, where the format is specified); this module
reads it for everything that is not a restore: the ``N earlier lines`` viewer (``cat``, piped into
``less -R``), the session index's ``shell`` source, the delayed compression passes, and Options ->
Storage. It is stdlib-only and imports nothing from the package, so the GUI can run it as a script:

    python3 -S backend/relay_core/textjournal.py cat <journal dir or id>

On disk: ``$XDG_DATA_HOME/relay/text/<id>/`` with ``meta.json`` and one file per segment,
``seg-NNNNNN.rtj`` (open, plain JSONL), ``.rtj.z`` (sealed, a zlib stream) or ``.rtj.xz`` (the
7-day pass, xz). A reader takes the most compressed file of each segment number.
"""

from __future__ import annotations

import argparse
import json
import lzma
import os
import re
import sys
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path

OPEN_SUFFIX = ".rtj"
ZLIB_SUFFIX = ".rtj.z"
XZ_SUFFIX = ".rtj.xz"

#: A segment no writer has touched for this long is sealed by the worker's pass. The GUI's writer
#: seals its own after ten idle minutes; this is for the ones a crash or a quit left open, and it
#: is long enough that a live pane's writer is never raced (it re-declares its tables if it is).
SEAL_IDLE_SECONDS = 3600
#: Sealed segments older than this are rewritten as xz.
RECOMPRESS_AFTER_SECONDS = 7 * 24 * 3600
XZ_PRESET = 9 | lzma.PRESET_EXTREME

# engine/core/CellTypes.h PromptMark
MARK_PROMPT_START = 1 << 0
MARK_COMMAND_START = 1 << 1
MARK_OUTPUT_START = 1 << 2
MARK_COMMAND_FINISHED = 1 << 3
MARK_USER_SHELL = 1 << 4
MARK_USER_AGENT = 1 << 5

_ID = re.compile(r"^[0-9A-Za-z-]{8,64}$")
_SEGMENT = re.compile(r"^seg-(\d+)\.rtj(\.z|\.xz)?$")


# ----- where it lives --------------------------------------------------------------------------

def text_root() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "relay" / "text"


def is_journal_id(value: str) -> bool:
    return bool(_ID.match(value or ""))


def journal_dir(journal: str | Path, root: str | Path | None = None) -> Path | None:
    """A journal id (resolved under the root) or a path to its directory; None for neither."""
    text = str(journal)
    if is_journal_id(text):
        return Path(root) / text if root else text_root() / text
    path = Path(text).expanduser()
    return path if path.is_dir() else None


def segment_files(directory: str | Path) -> list[tuple[int, Path]]:
    """``(number, path)`` in order, the most compressed file of each number."""
    rank = {XZ_SUFFIX: 0, ZLIB_SUFFIX: 1, OPEN_SUFFIX: 2}
    best: dict[int, tuple[int, Path]] = {}
    try:
        entries = list(os.scandir(directory))
    except OSError:
        return []
    for entry in entries:
        match = _SEGMENT.match(entry.name)
        if not match or not entry.is_file():
            continue
        number = int(match.group(1))
        suffix = ".rtj" + (match.group(2) or "")
        score = rank[suffix]
        if number not in best or score < best[number][0]:
            best[number] = (score, Path(entry.path))
    return [(number, best[number][1]) for number in sorted(best)]


# ----- reading ---------------------------------------------------------------------------------

def read_segment_bytes(path: str | Path) -> bytes:
    data = Path(path).read_bytes()
    name = str(path)
    if name.endswith(XZ_SUFFIX):
        return lzma.decompress(data)
    if name.endswith(ZLIB_SUFFIX):
        return zlib.decompress(data)
    return data


def read_segment(path: str | Path) -> list[dict]:
    """A segment's records. A line that does not parse (a torn last append) is skipped."""
    try:
        raw = read_segment_bytes(path)
    except (OSError, zlib.error, lzma.LZMAError):
        return []
    out = []
    for line in raw.split(b"\n"):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if isinstance(record, dict):
            out.append(record)
    return out


@dataclass
class Span:
    col: int
    length: int
    sgr: str = ""
    link: str = ""


@dataclass
class Line:
    text: str
    spans: list[Span] = field(default_factory=list)
    marks: int = 0
    at: str = ""           # the last time stamp before this line


@dataclass
class Event:
    """What a journal holds, in order: a line, a clear, or a conversation reference."""
    kind: str               # "line", "clear", "conversation", "conversation_end"
    line: Line | None = None
    ref: dict | None = None
    at: str = ""


def events(directory: str | Path) -> list[Event]:
    out: list[Event] = []
    at = ""
    for _, path in segment_files(directory):
        styles: dict[int, str] = {}
        links: dict[int, str] = {}
        for record in read_segment(path):
            if "s" in record:
                styles[int(record["s"])] = str(record.get("g", ""))
            elif "k" in record:
                links[int(record["k"])] = str(record.get("u", ""))
            elif "at" in record:
                at = str(record["at"])
            elif "x" in record:
                out.append(Event("clear", at=at))
            elif "c" in record:
                ref = record["c"]
                out.append(Event("conversation", ref=ref, at=at) if isinstance(ref, dict)
                           else Event("conversation_end", at=at))
            elif "t" in record:
                spans = []
                for run in record.get("r") or []:
                    if not isinstance(run, list) or len(run) < 3:
                        continue
                    spans.append(Span(int(run[0]), int(run[1]), styles.get(int(run[2]), ""),
                                      links.get(int(run[3]), "") if len(run) > 3 else ""))
                out.append(Event("line", Line(str(record["t"]), spans, int(record.get("m") or 0), at), at=at))
    return out


def lines(directory: str | Path, since_clear: bool = False) -> list[Line]:
    out: list[Line] = []
    for event in events(directory):
        if event.kind == "clear" and since_clear:
            out.clear()
        elif event.kind == "line" and event.line is not None:
            out.append(event.line)
    return out


def to_ansi(line: Line) -> str:
    """The row in the form the engine's serializer writes it (src/TextJournal.cpp toAnsi)."""
    out: list[str] = []
    sgr = link = ""
    emitted = False
    text = line.text

    def enter(next_sgr: str, next_link: str) -> None:
        nonlocal sgr, link, emitted
        if next_link != link:
            out.append(f"\x1b]8;;{next_link}\x1b\\")
            link = next_link
        if next_sgr != sgr:
            out.append(f"\x1b[{next_sgr or '0'}m")
            sgr = next_sgr
            emitted = True

    col = 0
    for span in line.spans:
        if span.col > col:
            enter("", "")
            out.append(text[col:span.col])
        enter(span.sgr, span.link)
        out.append(text[span.col:span.col + span.length])
        col = span.col + span.length
    if col < len(text):
        enter("", "")
        out.append(text[col:])
    if link:
        out.append("\x1b]8;;\x1b\\")
    if emitted and sgr:
        out.append("\x1b[0m")
    return "".join(out)


def meta(directory: str | Path) -> dict:
    try:
        data = json.loads((Path(directory) / "meta.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


# ----- conversations ---------------------------------------------------------------------------
# A conversation's own output is not in the journal: its rows are replaced by a reference, and the
# transcript is where the text is. `render_conversation` is set by the caller that can read a
# transcript (the worker, or `cat` below via relay_core when it is importable); without one, the
# reference prints as a single dim line naming it.

def conversation_label(ref: dict) -> str:
    source = str(ref.get("src") or "relay")
    session = str(ref.get("id") or "")
    return f"── {source} conversation {session} ──"


def render_lines(directory: str | Path, *, plain: bool = False, since_clear: bool = False,
                 conversation=None) -> list[str]:
    """Every line, in order, as ANSI (or plain) text; conversations through `conversation(ref)`,
    which returns a list of already-formatted lines, or through `conversation_label`."""
    out: list[str] = []
    for event in events(directory):
        if event.kind == "clear":
            if since_clear:
                out.clear()
            else:
                out.append("" if plain else "\x1b[2m── cleared ──\x1b[0m")
        elif event.kind == "conversation" and event.ref is not None:
            drawn = conversation(event.ref) if conversation else None
            if drawn:
                out.extend(drawn)
            else:
                label = conversation_label(event.ref)
                out.append(label if plain else f"\x1b[2m{label}\x1b[0m")
        elif event.kind == "line" and event.line is not None:
            out.append(event.line.text if plain else to_ansi(event.line))
    return out


# ----- commands, for the index -----------------------------------------------------------------

@dataclass
class Command:
    """One shell command and its output: the prompt line (with what was typed) and what followed,
    up to the next prompt. `index` counts commands from the journal's start."""
    index: int
    prompt: str
    output: list[str]
    at: str = ""


def commands(directory: str | Path, *, max_output_lines: int = 200) -> list[Command]:
    """The journal's shell text cut at its prompts (OSC 133 A, or a line the user typed), for the
    session index. Conversation references are boundaries too; their text is never here."""
    out: list[Command] = []
    current: Command | None = None
    for event in events(directory):
        if event.kind != "line" or event.line is None:
            if current is not None:
                out.append(current)
                current = None
            continue
        line = event.line
        starts = bool(line.marks & (MARK_PROMPT_START | MARK_COMMAND_START | MARK_USER_SHELL))
        if starts or current is None:
            if current is not None:
                out.append(current)
            current = Command(len(out), line.text if starts else "", [] if starts else [line.text], line.at)
            continue
        if len(current.output) < max_output_lines:
            current.output.append(line.text)
    if current is not None:
        out.append(current)
    for number, command in enumerate(out):
        command.index = number
    return out


# ----- compression passes ----------------------------------------------------------------------

def _atomic_write(path: Path, data: bytes) -> None:
    tmp = path.with_name(path.name + ".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise
    os.replace(tmp, path)


def seal_file(path: str | Path) -> Path | None:
    """``seg-N.rtj`` -> ``seg-N.rtj.z``: written first, then the raw file goes."""
    path = Path(path)
    try:
        raw = path.read_bytes()
    except OSError:
        return None
    target = path.with_name(path.name + ".z")
    if raw:
        _atomic_write(target, zlib.compress(raw, 6))
    path.unlink(missing_ok=True)
    return target if raw else None


def recompress_file(path: str | Path) -> Path | None:
    """``seg-N.rtj.z`` -> ``seg-N.rtj.xz``, keeping the sealed file's time so the age holds."""
    path = Path(path)
    try:
        raw = zlib.decompress(path.read_bytes())
        stat = path.stat()
    except (OSError, zlib.error):
        return None
    target = path.with_name(path.name[: -len(".z")] + ".xz")
    _atomic_write(target, lzma.compress(raw, format=lzma.FORMAT_XZ, preset=XZ_PRESET))
    os.utime(target, (stat.st_atime, stat.st_mtime))
    path.unlink(missing_ok=True)
    return target


def _journal_dirs(root: str | Path) -> list[Path]:
    try:
        return [Path(entry.path) for entry in os.scandir(root) if entry.is_dir() and is_journal_id(entry.name)]
    except OSError:
        return []


def seal_idle(root: str | Path | None = None, *, idle_seconds: float = SEAL_IDLE_SECONDS,
              now: float | None = None) -> int:
    """Seal every open segment untouched for `idle_seconds`. Returns how many."""
    now = time.time() if now is None else now
    sealed = 0
    for directory in _journal_dirs(root or text_root()):
        for entry in os.scandir(directory):
            if not entry.name.endswith(OPEN_SUFFIX) or not _SEGMENT.match(entry.name):
                continue
            try:
                if now - entry.stat().st_mtime < idle_seconds:
                    continue
            except OSError:
                continue
            sibling = Path(entry.path + ".z")
            if sibling.exists() or Path(entry.path + ".xz").exists():
                Path(entry.path).unlink(missing_ok=True)   # a seal that did not finish removing
                continue
            if seal_file(entry.path) is not None:
                sealed += 1
    return sealed


def recompress(root: str | Path | None = None, *, older_than: float = RECOMPRESS_AFTER_SECONDS,
               now: float | None = None) -> int:
    """Rewrite every zlib segment sealed more than `older_than` seconds ago as xz."""
    now = time.time() if now is None else now
    done = 0
    for directory in _journal_dirs(root or text_root()):
        for entry in os.scandir(directory):
            if not entry.name.endswith(ZLIB_SUFFIX):
                continue
            try:
                if now - entry.stat().st_mtime < older_than:
                    continue
            except OSError:
                continue
            if recompress_file(entry.path) is not None:
                done += 1
    return done


def usage(root: str | Path | None = None) -> dict:
    """Disk used by the journals: bytes on disk, lines held, journals, and per journal the same."""
    journals = []
    total_bytes = total_lines = 0
    for directory in _journal_dirs(root or text_root()):
        size = 0
        newest = 0.0
        for entry in os.scandir(directory):
            try:
                stat = entry.stat()
            except OSError:
                continue
            size += stat.st_size
            newest = max(newest, stat.st_mtime)
        info = meta(directory)
        count = int(info.get("lines") or 0)
        journals.append({"id": directory.name, "bytes": size, "lines": count, "cwd": info.get("cwd", ""),
                         "created": info.get("created", ""), "updated": info.get("updated", ""),
                         "mtime": newest})
        total_bytes += size
        total_lines += count
    journals.sort(key=lambda row: row["bytes"], reverse=True)
    return {"root": str(root or text_root()), "bytes": total_bytes, "lines": total_lines,
            "journals": journals}


def forget_older_than(seconds: float, root: str | Path | None = None, *, now: float | None = None,
                      keep: set[str] | None = None) -> int:
    """Delete every journal last written more than `seconds` ago, except the ids in `keep` (the
    panes that are open or in Recently closed). Returns how many went."""
    import shutil

    now = time.time() if now is None else now
    keep = keep or set()
    removed = 0
    for row in usage(root)["journals"]:
        if row["id"] in keep or now - row["mtime"] < seconds:
            continue
        shutil.rmtree(Path(root or text_root()) / row["id"], ignore_errors=True)
        removed += 1
    return removed


# ----- command line ----------------------------------------------------------------------------

def _conversation_renderer():
    """Draw a referenced conversation from its transcript, when relay_core can be imported."""
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
        from relay_core import transcript_text  # type: ignore
    except Exception:  # noqa: BLE001 - any failure falls back to the label
        return None
    return getattr(transcript_text, "render_reference", None)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="textjournal", description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    cat = sub.add_parser("cat", help="print a journal as ANSI text (pipe into less -R)")
    cat.add_argument("journal", help="journal id or directory")
    cat.add_argument("--plain", action="store_true", help="no colours")
    cat.add_argument("--since-clear", action="store_true", help="only what follows the last clear")
    cat.add_argument("--no-conversations", action="store_true", help="name conversations, do not draw them")
    sub.add_parser("seal-idle", help="seal open segments idle for an hour")
    sub.add_parser("recompress", help="rewrite segments sealed 7 days ago as xz")
    du = sub.add_parser("du", help="disk used, as JSON")
    du.add_argument("--root", default=None)
    args = parser.parse_args(argv)

    if args.command == "cat":
        directory = journal_dir(args.journal)
        if directory is None or not directory.is_dir():
            print(f"textjournal: no journal {args.journal}", file=sys.stderr)
            return 1
        renderer = None if args.no_conversations else _conversation_renderer()
        conversation = (lambda ref: renderer(ref, plain=args.plain)) if renderer else None
        try:
            for text in render_lines(directory, plain=args.plain, since_clear=args.since_clear,
                                     conversation=conversation):
                sys.stdout.write(text + "\n")
            sys.stdout.flush()
        except BrokenPipeError:   # `less` quit early
            return 0
        return 0
    if args.command == "seal-idle":
        print(seal_idle())
        return 0
    if args.command == "recompress":
        print(recompress())
        return 0
    if args.command == "du":
        print(json.dumps(usage(args.root)))
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
