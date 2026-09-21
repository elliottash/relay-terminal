#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""What a proposed cut through class Pane would cost, measured rather than guessed.

This began as the mover for card #AGNT step 1 -- lift the agent block out of `Pane` into a
`relay::AgentConsole`, the way scripts/split-main.py lifted the classes out of main.cpp -- and it
is kept for the half of itself that turned out to matter. **The extraction was cancelled**, on
2026-09-20, on the strength of what this script measured; the mover and its `--check` went with
it, and `src/AgentConsole.h` no longer exists.

Why. The plan costed the seam as "the 4 631-line agent block touches `m_backend` exactly 30
times". True, and not the whole cost: the block also reads `Pane`'s own fields and calls its own
members. `--coupling` over the wave-1b regions reported 66 fields and 52 members crossing the cut.
`--closure`, which starts from the owner's own "agent sessions UI" banner and grows the set to a
fixed point by adding anything whose every remaining mention is already inside it -- so it can
only make the set bigger and the seam smaller -- stopped at 228 members and 4 031 lines with a
**minimum seam of 284 names**: 157 fields and 127 members, whatever order the waves ran in.

The number that decided it was `m_editor`: **109 uses of the composer stay in `Pane` even when the
entire agent block moves**, and they are one thing -- the routing decision in and around
`requestRoute`: read the typed line, try a slash command, an alias, a skill, then choose the
shell, the foreground program, an ssh login, or the agent. The prompt box is not a part of the
agent block that can be lifted out of it; it is what both halves are made of. Which is the owner's
sentence read literally -- *an agent interface is the prompt box* -- so `Pane` **is** the console,
a terminal is one routing of what is typed in it, and a helper surface is the same `Pane` with a
context whose spec says `shell: false`.

What is left is the measuring, and it is worth keeping: anyone proposing to move code out of this
class should run `--coupling` on the lines first and read the two numbers before writing anything.

Every region is still found by content, never by a line number, and every member is brace-matched
with an awareness of strings, char literals, raw strings and comments; anything ambiguous is a
non-zero exit rather than a guess.

Usage:
    scripts/split-agent-console.py --index                    # every member of Pane, with its span
    scripts/split-agent-console.py --coupling 5119-6018 ...   # what a proposed cut would cost
    scripts/split-agent-console.py --closure 3058-7696        # …and the floor under that cost
"""

import argparse
import difflib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANE = os.path.join(ROOT, "src", "Pane.h")

# The class the members are taken from. Anchored on the `class` line, not on a line number: the
# base list changed when the pane became a Host and will change again.
PANE_CLASS = re.compile(r"^class Pane final : public QWidget(?:, public relay::agent::Host)? \{$")

# Where a moved block is written into AgentConsole.h. Everything between these two lines is
# generated; everything outside is hand-written and never touched.


# ----- the waves ---------------------------------------------------------------------------------
#
# Each wave is a list of anchors: the exact first line of a member of class Pane (its signature,
# its field declaration, or the `struct`/`enum` line of a nested type). The comment block above an
# anchor travels with it, because in this file the comment is the documentation of the thing below
# it. The waves are the table in card #AGNT step 1 and are run in order; a wave that cannot be
# made to pass the pane's own suites is abandoned rather than patched, and its anchors stay here
# as the record of what it was trying to move.


# ----- the receivers -----------------------------------------------------------------------------
#
# The closed list of rewrites the move applies, and the only text it changes in the code it moves.
# Left: what the code says inside Pane. Right: what it says inside AgentConsole. A receiver that
# is not here is an error rather than a guess -- which is what stops a member that still needs
# something only the pane knows from being moved quietly.



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
    parser.add_argument("--index", action="store_true",
                        help="list every member of class Pane with its span")
    parser.add_argument("--coupling", nargs="+", metavar="FIRST-LAST",
                        help="what a proposed cut would leave on the other side of the seam")
    parser.add_argument("--closure", nargs="+", metavar="FIRST-LAST",
                        help="grow a seed cut to a fixed point and print the minimum seam")
    args = parser.parse_args()
    if args.index:
        return index()
    if args.coupling:
        return coupling(args.coupling)
    if args.closure:
        return closure(args.closure)
    parser.error("say --index, --coupling or --closure")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
