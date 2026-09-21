#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Move the agent block out of class Pane and into relay::AgentConsole, by moving text.

Card #AGNT, step 1. `Pane` is one class of 16 500 lines and the agent -- the prompt box and
everything that belongs to it -- is a demarcated but scattered part of it. This tool moves that
part into `relay::AgentConsole` (src/AgentConsole.h), which reaches the pane it is drawn on
through `relay::agent::Host` (src/AgentHost.h).

It is `scripts/split-main.py`'s model, for the same reason: src/Pane.h is the hot file in this
checkout and several sessions have uncommitted edits in it at any moment, so the move is done on
the *working tree* and a hand-move could not be proved complete. Every region is found by an
exact content anchor, never a line number; every member is brace-matched with an awareness of
strings, char literals, raw strings and comments; anything missing or ambiguous is a non-zero
exit, not a guess.

    --check rebuilds the original src/Pane.h from the two files and diffs it.

The rebuild is possible because the move records where each block came from: every block written
into AgentConsole.h carries a `// moved-from:` line naming the *anchor of the member it
followed* in Pane.h, and `--check` puts each block back after that anchor and compares the result
with the pre-split file (named by the `// split-base:` line, read with `git show`).

The only text the tool changes in the code it moves is the receiver: the calls the agent block
made on the pane's terminal backend, or on the pane itself, become calls on the host
(`m_backend->columns()` -> `host().columns()`, `toast(x)` -> `host().toast(x, 1600)`). Every such
rewrite is listed on stdout, and the list is closed: a receiver the table does not know about is
an error, so a member that still needs the pane cannot be moved by accident.

What the measurement said, 2026-09-20 (--coupling, and why wave 1b is not written yet)
-------------------------------------------------------------------------------------
Card #AGNT step 1 counted the seam as "the 4 631-line agent block touches `m_backend` exactly 30
times". That is true, and it is not the whole cost: the same block *also* reads class Pane's own
fields and calls its own members directly, and those are what a move has to answer for.

Run over the wave 1b regions (the reasoning fold, the Activity ledger, the tool-call rows, the
transcript writer and the prose blocks -- 101 members, 1 395 lines), `--coupling` reports 33
fields that would travel with the cut, **66 that cross it**, and **52 Pane members called from
inside it that are not on `Host`** (14 more already are, and cost nothing). The heaviest are
`m_editor` (181 uses left behind), `m_backend` (131), `m_agentBusy` (71), `m_login` (67),
`m_cwd` (59), `m_token` (40), `m_workspace` (32). Narrowing the cut does not help: the 137-line
prose / `printInline` slice alone still leaves 8 fields and 11 members on the other side.

So waves 1b-1e cannot be a pure move through a 15-call, terminal-shaped `relay::agent::Host` --
`Host` would have to grow to sixty-odd calls, and many of those (`workspace`, `cwd`, the routing,
the agent role, the persist key) are not what the console is *drawn on* at all: they are what the
agent is *about*, which is `relay::agent::Context` (src/AgentContext.h, #AGNT step 2, landed
separately). Step 3 is where a console takes a Context; until it has one, a move would have to
push context through the host interface and every embedded host -- BoardPane, SettingsPane,
Conversations -- would have to answer `loginAtPrompt()` and `foregroundCommandLine()`.

That is a change to the card's own seam, so it is the owner's and the card's, not this tool's.
The waves below are therefore declared and empty: the anchors are the record of what each was to
move, `--coupling` is how the next session re-measures a proposed cut before making it, and 1a --
the seam itself -- has landed.

Usage:
    scripts/split-agent-console.py --wave 1b                  # move wave 1b, in place
    scripts/split-agent-console.py --wave 1b --dry-run        # say what it would move
    scripts/split-agent-console.py --check                    # rebuild Pane.h and diff
    scripts/split-agent-console.py --index                    # every member of Pane, with its span
    scripts/split-agent-console.py --coupling 5119-6018 ...   # what a proposed cut would cost
"""

import argparse
import difflib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANE = os.path.join(ROOT, "src", "Pane.h")
CONSOLE = os.path.join(ROOT, "src", "AgentConsole.h")

# The class the members are taken from. Anchored on the `class` line, not on a line number: the
# base list changed when the pane became a Host and will change again.
PANE_CLASS = re.compile(r"^class Pane final : public QWidget(?:, public relay::agent::Host)? \{$")

# Where a moved block is written into AgentConsole.h. Everything between these two lines is
# generated; everything outside is hand-written and never touched.
MOVED_BEGIN = "// ===== moved out of class Pane by scripts/split-agent-console.py ====="
MOVED_END = "// ===== end of the moved blocks ====="

BASE_LINE = "// split-base: "      # the commit whose src/Pane.h --check rebuilds
FROM_LINE = "// moved-from: "      # the Pane.h member this block followed, verbatim
WAVE_LINE = "// wave: "


# ----- the waves ---------------------------------------------------------------------------------
#
# Each wave is a list of anchors: the exact first line of a member of class Pane (its signature,
# its field declaration, or the `struct`/`enum` line of a nested type). The comment block above an
# anchor travels with it, because in this file the comment is the documentation of the thing below
# it. The waves are the table in card #AGNT step 1 and are run in order; a wave that cannot be
# made to pass the pane's own suites is abandoned rather than patched, and its anchors stay here
# as the record of what it was trying to move.

WAVES = {
    # 1a was the seam itself -- AgentHost.h, the empty AgentConsole, Pane implementing Host and
    # owning `AgentConsole m_agent{*this}` -- and moved no code, so it has no anchors.
    "1a": [],
    "1b": [],   # transcript writer, inks, ANSI/markdown/wrap, block gaps, tool-call rows,
                # OSC 8 folds, the reasoning fold, the Activity ledger, the turn panes
    "1c": [],   # composer frame, chip row, busy line, voice, model/effort pickers,
                # slash / @ / # popups, hints, prompt history
    "1d": [],   # queue entries, steers, the queue strip and QueueRowDelegate
    "1e": [],   # the worker process, withSessionFields, handle(), the ask, recap, session_info
}

# ----- the receivers -----------------------------------------------------------------------------
#
# The closed list of rewrites the move applies, and the only text it changes in the code it moves.
# Left: what the code says inside Pane. Right: what it says inside AgentConsole. A receiver that
# is not here is an error rather than a guess -- which is what stops a member that still needs
# something only the pane knows from being moved quietly.

RECEIVERS = [
    # the terminal backend, through the host
    ("m_backend->writeToDisplay(", "host().writeTerminal("),
    ("m_backend->columns()", "host().columns()"),
    ("m_backend->foldExpanded(", "host().foldExpanded("),
    ("m_backend->setFoldExpanded(", "host().setFoldExpanded("),
    ("m_backend->setFoldContent(", "host().setFoldContent("),
    ("m_backend->toggleFold(", "host().toggleFold("),
    ("m_backend->viewportAtBottom()", "host().viewportAtBottom()"),
    ("m_backend->scrollToBottom()", "host().scrollToBottom()"),
    ("m_backend->screenText()", "host().screenText()"),
    ("m_backend->cursorPosition()", "host().cursorPosition()"),
    ("m_backend->paste()", "host().paste()"),
    ("m_backend->shellPid()", "host().shellPid()"),
    ("m_backend->foregroundProcessId()", "host().foregroundProcessId()"),
    # the pane's own, likewise
    ("writeTerminal(", "host().writeTerminal("),
    ("terminalFolds()", "host().terminalFolds()"),
    ("terminalAtBottom()", "host().viewportAtBottom()"),
    ("bubbleRoom()", "host().bubbleRoom()"),
    ("terminalMode()", "host().terminalMode()"),
    ("m_atLineStart", "host().atLineStart()"),
]


def die(message):
    sys.stderr.write("split-agent-console: %s\n" % message)
    raise SystemExit(2)


# ----- lexing: which characters are code ---------------------------------------------------------

def code_mask(text):
    """True for every character that is code, False inside a comment or a literal.

    Handles //, /* */, "...", '...' and raw strings R"delim( ... )delim". Brace matching that did
    not know about them would count the braces in a shell snippet or an ANSI escape.
    """
    mask = bytearray(b"\x01") * len(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            mask[i:j] = b"\x00" * (j - i)
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                die("unterminated /* comment")
            mask[i:j + 2] = b"\x00" * (j + 2 - i)
            i = j + 2
        elif c == '"' and i > 0 and text[i - 1] == "R" and not _ident_char(text[i - 2] if i >= 2 else " "):
            open_paren = text.find("(", i + 1)
            if open_paren < 0:
                die("unterminated raw string")
            close = ")" + text[i + 1:open_paren] + '"'
            j = text.find(close, open_paren + 1)
            if j < 0:
                die("unterminated raw string")
            mask[i:j + len(close)] = b"\x00" * (j + len(close) - i)
            i = j + len(close)
        elif c in '"\'':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c or text[j] == "\n":
                    break
                j += 1
            end = min(j + 1, n)
            mask[i:end] = b"\x00" * (end - i)
            i = end
        else:
            i += 1
    return mask


def _ident_char(c):
    return c.isalnum() or c == "_"


class Source:
    """src/Pane.h, lexed once: its lines, the offset of each, and which characters are code."""

    def __init__(self, path):
        self.path = path
        self.text = open(path, encoding="utf-8").read()
        self.lines = self.text.split("\n")
        self.mask = code_mask(self.text)
        self.offsets = []
        at = 0
        for line in self.lines:
            self.offsets.append(at)
            at += len(line) + 1

    def line_of(self, offset):
        lo, hi = 0, len(self.offsets) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if self.offsets[mid] <= offset:
                lo = mid
            else:
                hi = mid - 1
        return lo

    def find_anchor(self, anchor, what="a member"):
        hits = [i for i, line in enumerate(self.lines) if line == anchor]
        if not hits:
            die("anchor for %s not found: %r\n"
                "            src/Pane.h has moved on; fix the anchor rather than guessing a line."
                % (what, anchor))
        if len(hits) > 1:
            die("anchor for %s is ambiguous, on lines %s: %r"
                % (what, ", ".join(str(h + 1) for h in hits), anchor))
        return hits[0]

    def class_body(self):
        """(first line index, last line index) of class Pane's `{` ... `}` , inclusive."""
        hits = [i for i, line in enumerate(self.lines) if PANE_CLASS.match(line)]
        if len(hits) != 1:
            die("class Pane's declaration was not found exactly once (found %d)" % len(hits))
        return hits[0], self.match_brace(hits[0])

    def close_of(self, brace):
        """The offset of the `}` that closes the code `{` at offset `brace`."""
        depth, i, n = 0, brace, len(self.text)
        while i < n:
            if self.mask[i]:
                if self.text[i] == "{":
                    depth += 1
                elif self.text[i] == "}":
                    depth -= 1
                    if depth == 0:
                        return i
            i += 1
        die("unbalanced braces from line %d" % (self.line_of(brace) + 1))

    def match_brace(self, index):
        """The line holding the `}` that closes the first code `{` on line `index`."""
        start = self.offsets[index]
        raw = self.lines[index]
        for k in range(len(raw)):
            if raw[k] == "{" and self.mask[start + k]:
                return self.line_of(self.close_of(start + k))
        die("no opening brace on %r" % raw.strip())

    def _next_code(self, i):
        n = len(self.text)
        while i < n and (not self.mask[i] or self.text[i].isspace()):
            i += 1
        return i

    def member_end(self, index):
        """The line the member starting on line `index` ends on.

        A declaration ends at its `;` -- including a nested type's `};` and a field with a brace
        initialiser -- and a definition at the `}` closing its body. A `{` inside parentheses is
        a default argument or a braced initialiser in a signature, never a body.
        """
        i, n, parens = self.offsets[index], len(self.text), 0
        while i < n:
            if self.mask[i]:
                c = self.text[i]
                if c == "(":
                    parens += 1
                elif c == ")":
                    parens -= 1
                elif parens == 0 and c == ";":
                    return self.line_of(i)
                elif parens == 0 and c == "{":
                    close = self.close_of(i)
                    after = self._next_code(close + 1)
                    if after < n and self.text[after] == ";":
                        return self.line_of(after)    # `};` or `T m{...};`
                    return self.line_of(close)        # a body
            i += 1
        die("no `;` or body after %r" % self.lines[index].strip())

    def member_span(self, anchor):
        """(start, end) line indices of the member whose first line is `anchor`, inclusive.

        The comment block above it travels with it: in this file the comment is the documentation
        of the thing below it, and a move that left it behind would strand it on a stranger.
        """
        index = self.find_anchor(anchor)
        end = self.member_end(index)
        start = index
        probe = index - 1
        while probe >= 0 and self.lines[probe].strip().startswith("//"):
            start = probe
            probe -= 1
        return start, end


# ----- moving ------------------------------------------------------------------------------------

def rewrite_receivers(block, anchor, report):
    out = []
    for line in block:
        before = line
        for old, new in RECEIVERS:
            if old in line:
                line = line.replace(old, new)
        if line != before:
            report.append((anchor, before.strip(), line.strip()))
        out.append(line)
    # `m_backend` reached in any other way is a receiver the table does not know about.
    for line in out:
        code = line.split("//")[0]
        if "m_backend" in code:
            die("a moved line still names m_backend, and no receiver rewrites it:\n    %s\n"
                "            add it to RECEIVERS, or leave the member in Pane." % line.strip())
    return out


def preceding_anchor(source, start, floor):
    """The first line above `start` that can name this block's place: the nearest non-blank,
    non-comment code line still inside the class. `--check` puts the block back after it."""
    probe = start - 1
    while probe > floor:
        stripped = source.lines[probe].strip()
        if stripped and not stripped.startswith("//"):
            return source.lines[probe]
        probe -= 1
    die("no line above %d can anchor the block's place" % (start + 1))


def console_blocks(text):
    """The generated region of AgentConsole.h, as a list of (wave, moved_from, lines)."""
    lines = text.split("\n")
    marks = [i for i, line in enumerate(lines) if line.strip() == MOVED_BEGIN]
    ends = [i for i, line in enumerate(lines) if line.strip() == MOVED_END]
    if len(marks) != 1 or len(ends) != 1 or ends[0] < marks[0]:
        return [], lines, None
    lo, hi = marks[0], ends[0]
    blocks, current = [], None
    for line in lines[lo + 1:hi]:
        bare = line.strip()
        if bare.startswith(WAVE_LINE.strip()):
            if current:
                blocks.append(current)
            current = {"wave": bare[len(WAVE_LINE.strip()):].strip(), "from": None, "lines": []}
        elif current is not None and bare.startswith(FROM_LINE.strip()) and current["from"] is None:
            current["from"] = bare[len(FROM_LINE.strip()):].strip()
        elif current is not None:
            current["lines"].append(line)
    if current:
        blocks.append(current)
    for block in blocks:
        while block["lines"] and block["lines"][-1] == "":
            block["lines"].pop()
    return blocks, lines, (lo, hi)


def base_commit(text):
    for line in text.split("\n"):
        if line.strip().startswith(BASE_LINE.strip()):
            return line.strip()[len(BASE_LINE.strip()):].strip()
    return None


def move(wave, dry_run):
    anchors = WAVES.get(wave)
    if anchors is None:
        die("unknown wave %r; the waves are %s" % (wave, ", ".join(sorted(WAVES))))
    if not anchors:
        print("wave %s moves no code (its work was the seam itself); nothing to do." % wave)
        return 0

    source = Source(PANE)
    first, last = source.class_body()
    console = open(CONSOLE, encoding="utf-8").read()
    done, console_lines, region = console_blocks(console)
    if any(block["wave"] == wave for block in done):
        die("wave %s has already been moved into src/AgentConsole.h; this tool does not run twice."
            % wave)
    if region is None:
        die("src/AgentConsole.h has no %r marker to write into" % MOVED_BEGIN)

    spans, report = [], []
    for anchor in anchors:
        start, end = source.member_span(anchor)
        if not (first < start and end < last):
            die("%r is not inside class Pane's body" % anchor)
        spans.append((start, end, anchor))
    spans.sort()
    for (a_start, a_end, a), (b_start, _, b) in zip(spans, spans[1:]):
        if b_start <= a_end:
            die("the members %r and %r overlap" % (a, b))

    moved = []
    for start, end, anchor in spans:
        place = preceding_anchor(source, start, first)
        block = rewrite_receivers(source.lines[start:end + 1], anchor, report)
        moved.append({"wave": wave, "from": place, "lines": block})

    if dry_run:
        for start, end, anchor in spans:
            print("  %5d..%-5d  %s" % (start + 1, end + 1, anchor.strip()))
        print("  %d member(s), %d line(s)" % (len(spans), sum(e - s + 1 for s, e, _ in spans)))
        for anchor, before, after in report:
            print("  receiver: %s\n         -> %s" % (before, after))
        return 0

    kept = []
    cut = {i for start, end, _ in spans for i in range(start, end + 1)}
    for i, line in enumerate(source.lines):
        if i not in cut:
            kept.append(line)
    open(PANE, "w", encoding="utf-8").write("\n".join(kept))

    lo, hi = region
    written = []
    for block in done + moved:
        written.append(WAVE_LINE + block["wave"])
        written.append(FROM_LINE + block["from"])
        written.extend(block["lines"])
        written.append("")
    out = console_lines[:lo + 1] + written + console_lines[hi:]
    open(CONSOLE, "w", encoding="utf-8").write("\n".join(out))

    print("wave %s: %d member(s), %d line(s) moved into src/AgentConsole.h"
          % (wave, len(spans), sum(e - s + 1 for s, e, _ in spans)))
    for anchor, before, after in report:
        print("  receiver: %s\n         -> %s" % (before, after))
    return 0


# ----- --check ------------------------------------------------------------------------------------

def check():
    """Rebuild src/Pane.h from the two files and diff it against the pre-split original.

    This is the property the whole move rests on: not one line dropped, not one duplicated, and
    nothing rewritten but the receivers the move declares.
    """
    console = open(CONSOLE, encoding="utf-8").read()
    base = base_commit(console)
    if base is None:
        die("src/AgentConsole.h carries no %r line, so there is nothing to rebuild against"
            % BASE_LINE.strip())
    blocks, _, region = console_blocks(console)
    if not blocks:
        print("--check: no wave has moved code yet; src/Pane.h is the original. OK")
        return 0

    original = subprocess.run(["git", "-C", ROOT, "show", "%s:src/Pane.h" % base],
                              capture_output=True, text=True)
    if original.returncode != 0:
        die("could not read %s:src/Pane.h -- %s" % (base, original.stderr.strip()))

    source = Source(PANE)
    rebuilt = list(source.lines)
    # Put each block back after the line it followed, deepest first so earlier insertions do not
    # move the anchors of later ones.
    placed = []
    for block in blocks:
        hits = [i for i, line in enumerate(rebuilt) if line == block["from"]]
        if len(hits) != 1:
            die("the line a block was moved from is %s in the rebuilt file: %r"
                % ("missing" if not hits else "ambiguous", block["from"]))
        placed.append((hits[0], block))
    for index, block in sorted(placed, reverse=True):
        body = list(block["lines"])
        for new, old in [(b, a) for a, b in RECEIVERS]:
            body = [line.replace(new, old) for line in body]
        rebuilt[index + 1:index + 1] = body

    want = original.stdout.split("\n")
    if rebuilt == want:
        print("--check: src/Pane.h + src/AgentConsole.h rebuild %s:src/Pane.h exactly. OK" % base[:12])
        return 0
    diff = list(difflib.unified_diff(want, rebuilt, "original", "rebuilt", lineterm="", n=2))
    sys.stderr.write("--check FAILED: the rebuild differs from %s:src/Pane.h\n" % base)
    sys.stderr.write("\n".join(diff[:400]) + "\n")
    return 1


def index():
    """Every top-level member of class Pane, with its span. The map the waves are chosen from."""
    source = Source(PANE)
    first, last = source.class_body()
    i = first + 1
    while i < last:
        line = source.lines[i]
        stripped = line.strip()
        if not line.startswith("    ") or not stripped or stripped.startswith("//") \
                or stripped.endswith(":") or line.startswith("     "):
            i += 1
            continue
        end = source.member_end(i)
        print("%5d..%-5d  %s" % (i + 1, end + 1, stripped[:110]))
        i = end + 1
    return 0


def coupling(ranges):
    """What a proposed cut would cost: every Pane field and member it leaves on the other side.

    Run this before writing a wave's anchors, not after. A field whose only mention outside the
    cut is its own declaration travels with the cut and is listed as such; one mentioned anywhere
    else is a call the seam would have to carry, and the count is how many places would have to
    keep working through it.
    """
    source = Source(PANE)
    first, last = source.class_body()
    inside = set()
    for span in ranges:
        lo, _, hi = span.partition("-")
        if not hi.isdigit() or not lo.isdigit():
            die("a range is <first>-<last>, in current src/Pane.h line numbers: %r" % span)
        inside.update(range(int(lo) - 1, int(hi)))
    if not inside <= set(range(first, last + 1)):
        die("the ranges reach outside class Pane's body (lines %d..%d)" % (first + 1, last + 1))

    members = []
    i = first + 1
    while i < last:
        line = source.lines[i]
        stripped = line.strip()
        if not line.startswith("    ") or not stripped or stripped.startswith("//") \
                or stripped.endswith(":") or line.startswith("     "):
            i += 1
            continue
        members.append((i, source.member_end(i), stripped))
        i = members[-1][1] + 1

    def declared(decl):
        if decl.startswith(("struct", "enum", "class", "using", "static_assert", "template")):
            return None
        if decl.startswith("std::function<"):
            # a callback field: its name is the identifier after the closing `>`, not the return
            # type inside it (`std::function<QString(int)> onShareTab;`).
            match = re.search(r">\s*([A-Za-z_]\w*)\s*[;=]", decl)
            return match.group(1) if match else None
        match = re.match(r".*?\b([A-Za-z_]\w*)\s*\(", decl)
        return match.group(1) if match else None

    # What the seam already carries: a call the console can make today, at no further cost.
    on_host = set(re.findall(r"virtual [^(]*?\b([A-Za-z_]\w*)\s*\(",
                             open(os.path.join(ROOT, "src", "AgentHost.h"), encoding="utf-8").read()))

    cut = [m for m in members if m[0] in inside]
    moved_names = set()
    for start, _, decl in cut:
        name = declared(decl)
        if name:
            moved_names.add(name)
        moved_names.update(re.findall(r"\bm_[A-Za-z0-9_]+", source.lines[start]))
    all_members = {declared(d) for _, _, d in members} - {None}

    here, there = {}, {}
    for i in range(first, last + 1):
        text = re.sub(r"//.*", "", source.lines[i])
        bucket = here if i in inside else there
        for field in set(re.findall(r"\bm_[A-Za-z0-9_]+", text)):
            bucket[field] = bucket.get(field, 0) + 1

    text = "\n".join(re.sub(r"//.*", "", source.lines[i]) for i in sorted(inside))
    called = (set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", text)) & all_members) - moved_names
    carried = sorted(called & on_host)
    calls = sorted(called - on_host)

    travels = sorted(f for f in here if there.get(f, 0) <= 1)
    crosses = sorted(((there[f], f) for f in here if there.get(f, 0) > 1), reverse=True)
    print("the cut: %d member(s), %d line(s)"
          % (len(cut), sum(e - s + 1 for s, e, _ in cut)))
    print("\ntravels with the cut (%d field(s), mentioned outside it only where declared):"
          % len(travels))
    print("  " + " ".join(travels))
    print("\ncrosses the seam (%d field(s); the count is uses that stay in Pane):" % len(crosses))
    for count, field in crosses:
        print("  %5d  %s" % (count, field))
    print("\nalready on relay::agent::Host (%d), so they cost the cut nothing:" % len(carried))
    print("  " + " ".join(carried))
    print("\ncrosses the seam (%d Pane member(s) called from inside the cut and not on Host):"
          % len(calls))
    print("  " + " ".join(calls))
    return 0


def closure(seed_ranges, rounds=40):
    """The largest set of Pane's members that could move together, and what would still cross.

    Hand-drawn line ranges cannot answer "is the cut wrong, or merely large": a member that looks
    like it crosses the seam usually just sits outside the range somebody typed. So this starts
    from a seed -- the owner's own "agent sessions UI" banner region -- and grows it to a fixed
    point: any member or field whose every remaining mention is *inside* the set joins the set,
    which can only make the set bigger and the seam smaller. What is left when it stops growing is
    the **minimum** seam the cut can have, whatever order the waves are run in.

    Read it as: everything in the closure can travel; everything in `crosses` has to become a call
    on `relay::agent::Host` (the surface), a field of `relay::agent::ContextSpec` (the subject), or
    an accessor `Pane` calls back on the console. A seam of a dozen is a seam. A seam of a hundred
    and fifty is a shared class with a line drawn through it.
    """
    source = Source(PANE)
    first, last = source.class_body()

    members = []
    i = first + 1
    while i < last:
        line = source.lines[i]
        stripped = line.strip()
        if not line.startswith("    ") or not stripped or stripped.startswith("//") \
                or stripped.endswith(":") or line.startswith("     "):
            i += 1
            continue
        members.append((i, source.member_end(i), stripped))
        i = members[-1][1] + 1

    def declared(decl):
        if decl.startswith(("struct", "enum", "class", "using", "static_assert", "template")):
            return None
        if decl.startswith("std::function<"):
            match = re.search(r">\s*([A-Za-z_]\w*)\s*[;=]", decl)
            return match.group(1) if match else None
        match = re.match(r".*?\b([A-Za-z_]\w*)\s*\(", decl)
        return match.group(1) if match else None

    # Qt and the standard library own plenty of names Pane also uses (`start`, `last`, `list`,
    # `model`). A name is only treated as Pane's when Pane declares it exactly once.
    seen = {}
    for index, (start, end, decl) in enumerate(members):
        name = declared(decl)
        if name:
            seen.setdefault(name, []).append(index)
        for field in re.findall(r"\bm_[A-Za-z0-9_]+", source.lines[start]):
            seen.setdefault(field, []).append(index)
    owner = {name: at[0] for name, at in seen.items() if len(at) == 1}

    bodies = []
    for start, end, _ in members:
        text = "\n".join(re.sub(r"//.*", "", source.lines[k]) for k in range(start, end + 1))
        names = set(re.findall(r"\bm_[A-Za-z0-9_]+", text))
        names |= set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", text))
        bodies.append(names & set(owner))

    inside = set()
    for span in seed_ranges:
        lo, _, hi = span.partition("-")
        inside.update(range(int(lo) - 1, int(hi)))
    seed = {i for i, (start, _, _) in enumerate(members) if start in inside}

    on_host = set(re.findall(r"virtual [^(]*?\b([A-Za-z_]\w*)\s*\(",
                             open(os.path.join(ROOT, "src", "AgentHost.h"), encoding="utf-8").read()))

    moved = set(seed)
    for _ in range(rounds):
        used_outside = set()
        for index, names in enumerate(bodies):
            if index in moved:
                continue
            for name in names:
                used_outside.add(owner[name])
        grown = {owner[name] for name in owner} - used_outside
        grown |= seed
        if grown == moved:
            break
        moved = grown

    crosses = set()
    for index in sorted(moved):
        for name in bodies[index]:
            if owner[name] not in moved:
                crosses.add(name)
    carried = sorted(n for n in crosses if n in on_host)
    left = sorted(n for n in crosses if n not in on_host)

    lines_moved = sum(members[i][1] - members[i][0] + 1 for i in sorted(moved))
    print("seed: %d member(s)" % len(seed))
    print("closure: %d member(s), %d line(s) -- the most that can travel together" % (len(moved), lines_moved))
    print("\nalready on relay::agent::Host (%d):" % len(carried))
    print("  " + " ".join(carried))
    print("\nthe minimum seam: %d name(s) the closure still needs from Pane" % len(left))
    fields = [n for n in left if n.startswith("m_")]
    calls = [n for n in left if not n.startswith("m_")]
    print("  fields (%d): %s" % (len(fields), " ".join(fields)))
    print("  members (%d): %s" % (len(calls), " ".join(calls)))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--wave", help="which wave to move (%s)" % ", ".join(sorted(WAVES)))
    parser.add_argument("--dry-run", action="store_true", help="say what it would move")
    parser.add_argument("--check", action="store_true",
                        help="rebuild src/Pane.h from the outputs and diff it")
    parser.add_argument("--index", action="store_true",
                        help="list every member of class Pane with its span")
    parser.add_argument("--coupling", nargs="+", metavar="FIRST-LAST",
                        help="what a proposed cut would leave on the other side of the seam")
    parser.add_argument("--closure", nargs="+", metavar="FIRST-LAST",
                        help="grow a seed cut to a fixed point and print the minimum seam")
    args = parser.parse_args()
    if args.check:
        return check()
    if args.index:
        return index()
    if args.coupling:
        return coupling(args.coupling)
    if args.closure:
        return closure(args.closure)
    if not args.wave:
        parser.error("say --wave, --check, --index or --coupling")
    return move(args.wave, args.dry_run)


if __name__ == "__main__":
    raise SystemExit(main())
