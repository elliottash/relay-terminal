#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Split src/main.cpp into one header per unit, by moving text and never rewriting it.

This is a one-shot tool, kept for the record. It exists because several sessions had
uncommitted edits inside main.cpp when the file was split, so the split could not be done by
hand: run it on `git show HEAD:src/main.cpp` to build the commit and again on the working-tree
file to rebuild the working tree, and everyone's in-flight edits come back as ordinary
uncommitted diffs inside the new files.

Every region is found by content anchors (an exact source line), never by line number, and each
class is brace-matched -- with an awareness of strings, char literals, raw strings and comments --
to prove it really closes inside the region it was given. Anything missing or ambiguous is a
non-zero exit, not a guess.

The only text the tool changes in the code it moves is the word `inline`: `static` becomes
`inline` for the two free functions that now live in a header, and the out-of-line
`WindowManager::` definitions, which need the complete RelayWindow and so stay in a header,
get an `inline` prefix. Every such line is listed on stdout.

What ends up where is the SEGMENTS table below: AppPaths.h, Keymap.h, Isolation.h, Pane.h,
PaneChrome.h, WindowChrome.h, RelayWindow.h, WindowManagerImpl.h, and what is left in main.cpp.
This stays one translation unit -- main.cpp includes the headers and nothing else does -- so every
class keeps its members in the class body and the build is the same build it was.

Usage:
    scripts/split-main.py --in <main.cpp> --out <directory>     # writes <directory>/src/...
    scripts/split-main.py --in <main.cpp> --out <directory> --check
    scripts/split-main.py --in <main.cpp> --out <directory> --manifest <file.json>
"""

import argparse
import json
import os
import re
import sys


# ----- the cut ---------------------------------------------------------------------------------
#
# The file is tiled into consecutive segments: each one starts at its anchor line (with the
# comment block above the anchor pulled along, because in this file the comment is the
# documentation of the thing below it) and runs to the line before the next segment's start.
# Tiling is what makes "every original line lands somewhere, exactly once, in order" checkable.

class Segment:
    def __init__(self, name, out, anchor, closes=()):
        self.name = name        # for messages
        self.out = out          # destination file name under src/
        self.anchor = anchor    # the exact source line that starts it (None: start of file)
        self.closes = closes    # anchors whose `{` must brace-match inside this segment
        self.start = None
        self.end = None         # exclusive


SEGMENTS = [
    Segment("preamble", "main.cpp", None),
    Segment("relay-macros", "AppPaths.h", "#ifndef RELAY_VERSION"),
    Segment("dataRoot", "AppPaths.h", "static QString dataRoot() {",
            closes=["static QString dataRoot() {"]),
    Segment("ActionDef", "Keymap.h",
            "struct ActionDef { QString id, description, category; QStringList defaults; };"),
    Segment("Keymap", "Keymap.h", "class Keymap {", closes=["class Keymap {"]),
    Segment("isolation", "Isolation.h", "namespace isolation {", closes=["namespace isolation {"]),
    Segment("relayFuzzyScore", "AppPaths.h",
            "static int relayFuzzyScore(const QString &needle, const QString &haystack) {",
            closes=["static int relayFuzzyScore(const QString &needle, const QString &haystack) {"]),
    Segment("QueueRowDelegate", "Pane.h",
            "class QueueRowDelegate final : public QStyledItemDelegate {",
            closes=["class QueueRowDelegate final : public QStyledItemDelegate {"]),
    Segment("Pane", "Pane.h", "class Pane final : public QWidget {",
            closes=["class Pane final : public QWidget {"]),
    Segment("ToolPane", "PaneChrome.h", "class ToolPane final : public QWidget {",
            closes=["class ToolPane final : public QWidget {"]),
    Segment("PaneChrome", "PaneChrome.h", "class PaneChrome final : public QFrame {",
            closes=["class PaneChrome final : public QFrame {"]),
    # ClosedItem and WindowManager share RelayWindow.h rather than having a file of their own:
    # WindowManager::forget() is defined inline in the class and needs the complete RelayWindow,
    # while RelayWindow needs the complete WindowManager. The cycle cannot be broken without
    # editing the code being moved, and this tool does not edit the code being moved.
    Segment("WindowManager", "RelayWindow.h", "class RelayWindow;",
            closes=["struct ClosedItem {", "class WindowManager {"]),
    Segment("ChromeButton", "WindowChrome.h", "class ChromeButton final : public QToolButton {",
            closes=["class ChromeButton final : public QToolButton {"]),
    Segment("NotificationsPopup", "WindowChrome.h", "class NotificationsPopup final : public QFrame {",
            closes=["class NotificationsPopup final : public QFrame {"]),
    Segment("RelayWindow", "RelayWindow.h", "class RelayWindow final : public QMainWindow {",
            closes=["class RelayWindow final : public QMainWindow {"]),
    Segment("WindowManagerImpl", "WindowManagerImpl.h", "WindowManager::~WindowManager() {"),
    Segment("tail", "main.cpp", "static void registerUrlHandler() {"),
]

# The order the new headers are included from main.cpp; also the order --check walks the files.
OUT_ORDER = ["AppPaths.h", "Keymap.h", "Isolation.h", "Pane.h", "PaneChrome.h",
             "WindowChrome.h", "RelayWindow.h", "WindowManagerImpl.h", "main.cpp"]

HEADERS = [name for name in OUT_ORDER if name != "main.cpp"]


# ----- what each header says about itself, and what it needs -------------------------------------
#
# The prose matches the repo's voice: why the thing is here, not what the compiler already says.
# The include lists say what each header uses: of the includes main.cpp carried, the ones whose
# types or functions the moved code actually names, plus a handful (QCoreApplication, QScrollBar,
# QScreen, QPlainTextEdit, QTreeView) that it reaches through an expression rather than by name and
# that main.cpp only ever got second hand. They are deliberately not cut down to the smallest set
# that happens to compile: a header that leans on <QDir> to drag in QFileInfo breaks the day Qt
# stops doing that. Each one was checked by compiling `#include "X.h"` as a translation unit of
# its own, with the relay target's flags, warnings and all.

DOC = {
    "AppPaths.h": """// Where Relay's data files are (the backend, the shell integration, the scripts it runs) and the
// fuzzy score the palette and the `@` picker rank their rows with. Two small free functions that
// everything else in main.cpp reaches for, so they come first and depend on nothing of Relay's.
// The RELAY_* fallbacks live here because dataRoot() is what reads them.""",
    "Keymap.h": """// Every window-level shortcut as a named action: the defaults, the user's overrides in
// keybindings.json, the presets, and the reload that makes an edit take effect without a restart.
// Nothing outside main.cpp uses it, which is why it is a header beside main.cpp rather than one of
// the tested libraries in src/. Depends on Qt only.""",
    "Isolation.h": """// Per-pane process isolation: each pane's shell and agent worker run in their own transient
// systemd user scope, so a runaway command is stopped inside its pane instead of taking Relay down
// with it. Free functions over `systemd-run` and `systemctl`, with the probes cached; Qt only.""",
    "Pane.h": """// One terminal pane -- the single largest thing in the app: a shell behind relay::TerminalBackend,
// its Bash bridge, the composer, the queue, and the pane's own agent worker and conversation.
// Pane never names RelayWindow or WindowManager; it calls up through std::function callbacks the
// window sets, which is what lets it sit below them here. QueueRowDelegate draws the queue rows.""",
    "PaneChrome.h": """// The two small widgets that sit around a pane: ToolPane, the non-terminal pane (folder explorer,
// file preview, plan, transcript, Switchboard, settings), and PaneChrome, the button row and drag
// grip in every pane's top-right corner. PaneChrome asks the leaf it is parented to for the room it
// needs, which is the one thing here that has to know what a Pane is -- hence the include.""",
    "WindowChrome.h": """// The window's own title bar, since Relay draws one instead of taking the desktop's: ChromeButton
// paints the header glyphs (bell, gear, minimize, maximize, close, and the tab row's own buttons)
// so they all sit at one stroke weight, and NotificationsPopup is the list behind the bell.""",
    "RelayWindow.h": """// One window -- the tab row that doubles as the title bar, the splitter tree of panes inside each
// tab, the actions palette, and the routing of every shortcut to the pane that should act on it --
// and above it WindowManager, which owns the windows, remembers the closed ones and keeps the
// saved layout. The two share a header because they name each other: RelayWindow holds a
// WindowManager and WindowManager::forget() takes a RelayWindow, both inline, so neither can be
// declared first on its own. The WindowManager members that need more than that are in
// WindowManagerImpl.h.""",
    "WindowManagerImpl.h": """// The WindowManager members that need the complete RelayWindow -- opening, restoring and counting
// windows, and reading and writing the saved layout. They are here rather than in the class
// because they were written out of line, below RelayWindow, for exactly that reason; include this
// after RelayWindow.h. `inline` because they are now definitions in a header, which is the one
// change the split made to the code it moved.""",
}

# Sibling headers each new header needs, in include order.
SIBLINGS = {
    "AppPaths.h": [],
    "Keymap.h": [],
    "Isolation.h": [],
    "Pane.h": ["AppPaths.h", "Keymap.h", "Isolation.h"],
    "PaneChrome.h": ["Pane.h"],          # its buttons cast the pane they sit on
    "WindowChrome.h": [],
    "RelayWindow.h": ["PaneChrome.h", "WindowChrome.h"],
    "WindowManagerImpl.h": ["RelayWindow.h"],
}

# Includes whose trigger is not simply "the Qt class of the same name": the system headers, and
# the project's own, whose type and namespace names were read off the headers they come from.
INCLUDE_RULES = {
    '#include "RichEditor.h"': ['RichEditor'],
    '#include "Theme.h"': ['Notifier', 'ThemeChoice', 'relay::theme'],
    '#include "FilePanes.h"': [
        'FileExplorer', 'FileMenuHost', 'FileMenuItem', 'FileMenuTarget', 'FilePreview',
        'PlanEditor', 'relay::FileExplorer', 'relay::FileMenuHost',
        'relay::FileMenuItem', 'relay::FileMenuTarget', 'relay::FilePreview',
        'relay::PlanEditor', 'relay::explorerMenu', 'relay::openEntries',
        'relay::previewMenu'
    ],
    '#include "BoardPane.h"': [
        'BoardView', 'CardDetail', 'RichEditor', 'RowList', 'relay::BoardView',
        'relay::CardDetail', 'relay::RowList'
    ],
    '#include "BoardWorker.h"': ['BoardWorker', 'relay::BoardWorker'],
    '#include "AgentUi.h"': [
        'InstructionFile', 'OnboardingResult', 'PickerAction', 'PickerResult',
        'PickerRow', 'relay::agentui'
    ],
    '#include "Completion.h"': [
        'Completion', 'relay::Completion', 'relay::completeAt', 'relay::escapeToken',
        'relay::unescapeToken'
    ],
    '#include "FileIndex.h"': ['FileIndex', 'relay::FileIndex'],
    '#include "ShellHighlighter.h"': ['InputHighlighter', 'relay::InputHighlighter'],
    '#include "Hints.h"': ['ShortcutHints', 'relay::ShortcutHints'],
    '#include "Notifications.h"': [
        'Notification', 'NotificationCenter', 'relay::Notification',
        'relay::NotificationCenter'
    ],
    '#include "InputPolicy.h"': [
        'LineTarget', 'Secret', 'State', 'TerminalMode', 'TypeRefusal', 'relay::input'
    ],
    '#include "ScreenPrompt.h"': ['Detection', 'Kind', 'Signals', 'relay::screen'],
    '#include "PaneLayout.h"': ['Direction', 'PlacementWindow', 'relay::panes'],
    '#include "QueueNav.h"': ['Action', 'State', 'relay::queuenav'],
    '#include "PaneTitles.h"': ['relay::titles'],
    '#include "TurnTranscript.h"': ['TurnTranscriptView', 'relay::TurnTranscriptView'],
    '#include "ModelSettings.h"': [
        'KeysDialog', 'RolesDialog', 'relay::KeysDialog', 'relay::RolesDialog'
    ],
    '#include "SettingsPane.h"': [
        'ActionItem', 'SettingRow', 'SettingsPane', 'SettingsSection',
        'relay::ActionItem', 'relay::SettingRow', 'relay::SettingsPane',
        'relay::SettingsSection'
    ],
    '#include "SkillsDialog.h"': ['SkillsDialog', 'relay::SkillsDialog'],
    '#include "SubagentTranscript.h"': ['SubagentTranscriptView', 'relay::SubagentTranscriptView'],
    '#include "SubagentsPanel.h"': [
        'SubagentModel', 'SubagentRow', 'SubagentsPanel', 'relay::SubagentModel',
        'relay::SubagentRow', 'relay::SubagentsPanel'
    ],
    '#include "RequestLedger.h"': [
        'LedgerOpenItem', 'LedgerRequest', 'LedgerTodo', 'RequestLedgerModel',
        'TaskItem', 'TaskOutcome', 'TaskSummary', 'relay::LedgerOpenItem',
        'relay::LedgerRequest', 'relay::LedgerTodo', 'relay::RequestLedgerModel',
        'relay::TaskItem', 'relay::TaskOutcome', 'relay::TaskSummary'
    ],
    '#include "RequestsPanel.h"': ['RequestsPanel', 'relay::RequestsPanel'],
    '#include "Conversations.h"': ['Dialog', 'FindBar', 'relay::conversations'],
    '#include "Logging.h"': ['Level', 'relay::log'],
    '#include "TerminalBackends.h"': [
        'TerminalBackend', 'TerminalMenuItem', 'TerminalMenuState',
        'relay::TerminalBackend', 'relay::TerminalMenuItem', 'relay::TerminalMenuState',
        'relay::createTerminalBackend', 'relay::defaultEngineCore',
        'relay::resolveEngineCore', 'relay::setDefaultEngineCore',
        'relay::terminalContextMenu'
    ],
    '#include "TerminalBackend.h"': ['TerminalBackend', 'relay::TerminalBackend'],
    '#include "WindowState.h"': ['Screen', 'relay::windowstate'],
    '#include "RuntimeDirs.h"': ['Owner', 'SweepResult', 'relay::runtimedirs'],
    '#include "RemoteShare.h"': [
        'RemoteShare', 'RemoteShareDialog', 'TerminalView', 'relay::QrMatrix',
        'relay::RemoteShare', 'relay::RemoteShareDialog', 'relay::TerminalView'
    ],
    '#include "backend/VTermBackend.h"': [
        'TerminalSession', 'TerminalView', 'VTermBackend', 'relay::TerminalSession',
        'relay::TerminalView', 'relay::VTermBackend'
    ],
    '#include "core/VtCore.h"': [
        'VtCore', 'relay::VtCore', 'relay::availableVtCores', 'relay::createVtCore'
    ],
    '#include "session/TerminalSession.h"': ['TerminalSession', 'relay::TerminalSession'],
    '#include "view/TerminalView.h"': [
        'TerminalSession', 'TerminalView', 'relay::TerminalSession',
        'relay::TerminalView'
    ],
    '#include "Voice.h"': ['Capture', 'Insertion', 'Options', 'relay::voice'],
    '#include "Images.h"': ['relay::images'],
    '#include "Aliases.h"': [
        'Alias', 'Field', 'Invocation', 'Param', 'Rendered', 'relay::aliases'
    ],
    '#include "MarkdownAnsi.h"': ['MarkdownAnsi', 'relay::MarkdownAnsi'],
    '#include "WordWrap.h"': ['WordWrap', 'relay::WordWrap'],
    '#include "OutputLinks.h"': [
        'Candidate', 'Cursor', 'Entry', 'Found', 'Kind', 'Target', 'relay::links'
    ],
    '#include "SlashCommands.h"': ['relay::slash'],
    '#include <iterator>': [
        'std::back_inserter', 'std::inserter', 'std::distance', 'std::next(',
        'std::prev('
    ],
    '#include <sys/stat.h>': [
        'mkdir(', 'S_IR', 'S_IW', 'S_IX', 'chmod(', 'fstat(', 'umask(', 'struct stat'
    ],
    '#include <sys/syscall.h>': [
        'syscall(', 'SYS_read', 'SYS_write', 'SYS_ioctl', 'SYS_gettid',
        'SYS_pidfd_open', 'SYS_tgkill'
    ],
    '#include <algorithm>': [
        'std::max', 'std::min', 'std::sort', 'std::clamp', 'std::find', 'std::any_of',
        'std::all_of', 'std::none_of', 'std::count_if', 'std::reverse',
        'std::remove_if', 'std::stable_sort', 'std::swap', 'std::copy',
        'std::transform', 'std::lower_bound'
    ],
    '#include <functional>': ['std::function', 'std::bind', 'std::ref', 'std::hash'],
    '#include <memory>': [
        'std::unique_ptr', 'std::make_unique', 'std::shared_ptr', 'std::make_shared',
        'std::weak_ptr'
    ],
    '#include <cmath>': [
        'std::round', 'std::floor', 'std::ceil', 'std::pow', 'std::abs', 'std::fabs',
        'std::log', 'std::sin', 'std::cos', 'std::tan', 'std::sqrt', 'std::hypot',
        'std::atan', 'M_PI', 'qDegreesToRadians'
    ],
    '#include <csignal>': ['signal(', 'SIGTERM', 'SIGINT', 'SIGHUP', 'sigaction', 'SIG_'],
    '#include <cstring>': ['memcpy(', 'strlen(', 'memset(', 'strerror(', 'strncpy(', 'strcmp('],
    '#include <stdexcept>': ['std::runtime_error', 'std::logic_error', 'std::exception'],
    '#include <fcntl.h>': [
        'O_RDONLY', 'O_WRONLY', 'O_NONBLOCK', 'O_CLOEXEC', 'fcntl(', '::open(',
        'F_SETFD', 'F_GETFL'
    ],
    '#include <termios.h>': ['termios', 'tcgetattr', 'tcsetattr', 'cfmakeraw'],
    '#include <unistd.h>': [
        '::close(', '::read(', '::write(', 'getpid(', 'isatty(', 'dup2(', 'fork(',
        'execvp(', 'ttyname(', 'getppid(', 'pipe(', 'unlink(', 'access(', '_exit(',
        'setsid(', 'sysconf(', 'kill(', 'readlink('
    ],
}


# Includes the moved code uses through an expression rather than by name -- `verticalScrollBar()`,
# `window()->screen()`, the real type behind `parentWidget()` -- so no token in the text gives them
# away. main.cpp only ever had some of these second hand, through another Qt header.
FORCED = {
    "AppPaths.h": ['#include <QCoreApplication>', '#include <QString>', '#include <QStringList>'],
    "Keymap.h": [],
    "Isolation.h": [],
    # Pane reaches the engine's view and session by chaining off the backend
    # (`engine->view()->session()`), never by naming either type.
    "Pane.h": ['#include "view/TerminalView.h"', '#include "session/TerminalSession.h"',
               '#include <QScrollBar>'],
    "PaneChrome.h": ['#include <QPlainTextEdit>', '#include <QTreeView>'],
    "WindowChrome.h": ['#include <QScreen>'],
    "RelayWindow.h": [],
    "WindowManagerImpl.h": [],
}

INCLUDE_LINE = re.compile(r'^#\s*include\s+("[^"]+"|<[^>]+>)')
QT_HEADER = re.compile(r'^#include <(Q[A-Za-z]\w*)>$')

# Filled in by plan(): the includes main.cpp carries, in its order, and the moved text per file.
POOL = []
FILE_TEXT = {}


def triggers(include):
    """The names whose presence in a file's text means that file needs this include."""
    if include in INCLUDE_RULES:
        return INCLUDE_RULES[include]
    match = QT_HEADER.match(include)
    return [match.group(1)] if match else None


def names(text, token):
    if "::" in token or not token.isidentifier():
        return token in text
    return re.search(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])" % re.escape(token), text) is not None


def group_of(include):
    return 0 if include.startswith('#include "') else 1 if QT_HEADER.match(include) else 2


def includes_for(name):
    """What this header includes: of the ones main.cpp carries, the ones its code names.

    Deliberately not the smallest set that happens to compile -- a header that leans on <QDir> to
    drag in QFileInfo breaks the day Qt stops doing that -- and deliberately computed from the text
    rather than written down, so a new `#include` and a new use that arrive together in someone
    else's edit reach the header they belong to without this tool being taught about them.
    """
    text = FILE_TEXT[name]
    chosen = [include for include in POOL
              if triggers(include) and any(names(text, t) for t in triggers(include))]
    groups = [[], [], []]
    for include in chosen:
        groups[group_of(include)].append(include)
    for include in FORCED[name]:
        if include not in chosen:
            groups[group_of(include)].append(include)
    return groups[0] + groups[1] + groups[2]


SPDX = "// SPDX-License-Identifier: GPL-3.0-or-later"


# ----- lexing: which characters are code ---------------------------------------------------------

def code_mask(text):
    """True for every character that is code, False inside a comment or a literal.

    Handles //, /* */, "...", '...', and raw strings R"delim( ... )delim" -- Keymap carries a big
    one, and brace matching that did not know about it would count the braces in its JSON.
    """
    mask = bytearray(b"\x01") * len(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                mask[k] = 0
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                die("unterminated /* comment")
            for k in range(i, j + 2):
                mask[k] = 0
            i = j + 2
        elif c == '"' and i > 0 and text[i - 1] == "R" and not _ident_char(text[i - 2] if i >= 2 else " "):
            open_paren = text.find("(", i + 1)
            if open_paren < 0:
                die("unterminated raw string")
            delim = text[i + 1:open_paren]
            close = ")" + delim + '"'
            j = text.find(close, open_paren + 1)
            if j < 0:
                die("unterminated raw string R\"%s(" % delim)
            for k in range(i, j + len(close)):
                mask[k] = 0
            i = j + len(close)
        elif c == '"' or c == "'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c or text[j] == "\n":
                    break
                j += 1
            for k in range(i, min(j + 1, n)):
                mask[k] = 0
            i = min(j + 1, n)
        else:
            i += 1
    return mask


def _ident_char(c):
    return c.isalnum() or c == "_"


def die(message):
    sys.stderr.write("split-main: %s\n" % message)
    raise SystemExit(2)


# ----- finding the regions -----------------------------------------------------------------------

def find_anchor(lines, anchor, what):
    hits = [i for i, line in enumerate(lines) if line.rstrip("\n") == anchor]
    if not hits:
        die("anchor for %s not found: %r\n"
            "            main.cpp has moved on; fix the anchor rather than guessing a line number."
            % (what, anchor))
    if len(hits) > 1:
        die("anchor for %s is ambiguous, found on lines %s: %r"
            % (what, ", ".join(str(h + 1) for h in hits), anchor))
    return hits[0]


def absorb_comment_block(lines, index, floor):
    """Walk back over the comment block above `index`, crossing blank lines between comment runs.

    In this file the block comment above a class is that class's documentation, so it travels with
    it. `floor` is the previous segment's anchor: never walk past it.
    """
    start = index
    probe = index - 1
    while probe > floor:
        if lines[probe].strip() == "":
            probe -= 1
            continue
        if lines[probe].lstrip().startswith("//") and not lines[probe][:1].isspace():
            start = probe
            probe -= 1
            continue
        break
    return start


def close_line(lines, mask, offsets, anchor_index, anchor):
    """The line holding the `}` that closes the `{` on the anchor line."""
    line_start = offsets[anchor_index]
    raw = lines[anchor_index]
    brace = None
    for k in range(len(raw)):
        if raw[k] == "{" and mask[line_start + k]:
            brace = line_start + k
            break
    if brace is None:
        die("no opening brace on the anchor line for %r" % anchor)
    depth = 0
    text_len = len(mask)
    i = brace
    while i < text_len:
        if mask[i]:
            ch = _TEXT[i]
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    break
        i += 1
    else:
        die("unbalanced braces after %r" % anchor)
    lo, hi = 0, len(offsets) - 1
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if offsets[mid] <= i:
            lo = mid
        else:
            hi = mid - 1
    return lo


def plan(lines):
    global _TEXT
    _TEXT = "".join(lines)
    mask = code_mask(_TEXT)
    offsets, at = [], 0
    for line in lines:
        offsets.append(at)
        at += len(line)

    if '#include "RelayWindow.h"' in _TEXT or '#include "Pane.h"' in _TEXT:
        die("this main.cpp is already split (it includes the new headers); refusing to run")

    floor = -1
    for segment in SEGMENTS:
        if segment.anchor is None:
            segment.start = 0
            continue
        index = find_anchor(lines, segment.anchor, segment.name)
        if index <= floor:
            die("segments are out of order: %s at line %d is not after the previous one"
                % (segment.name, index + 1))
        segment.start = absorb_comment_block(lines, index, floor)
        segment.anchor_index = index
        floor = index

    for a, b in zip(SEGMENTS, SEGMENTS[1:]):
        a.end = b.start
    SEGMENTS[-1].end = len(lines)

    for segment in SEGMENTS:
        if segment.end <= segment.start:
            die("segment %s is empty" % segment.name)
        last_close, last_anchor = None, None
        for anchor in segment.closes:
            index = find_anchor(lines, anchor, segment.name)
            end = close_line(lines, mask, offsets, index, anchor)
            if not (segment.start <= index < segment.end and end < segment.end):
                die("%r does not close inside its segment (%s, lines %d-%d): it closes on line %d.\n"
                    "            Something new sits between two of the anchors; teach the tool about it."
                    % (anchor, segment.name, segment.start + 1, segment.end, end + 1))
            if last_close is None or end > last_close:
                last_close, last_anchor = end, anchor
        # Nothing but blank lines and comments may follow the last thing the segment declares:
        # anything else is a unit that grew into main.cpp since, and it needs a home of its own.
        if last_close is not None:
            for extra in range(last_close + 1, segment.end):
                stripped = lines[extra].strip()
                if stripped and not stripped.startswith("//"):
                    die("unexpected code after %r in segment %s: line %d is %r.\n"
                        "            A new file-scope unit appeared; teach the tool about it."
                        % (last_anchor, segment.name, extra + 1, lines[extra].rstrip("\n")))

    # What the new headers may include, and the text each one has to answer for.
    preamble = SEGMENTS[0]
    del POOL[:]
    unknown = []
    for offset in range(preamble.start, preamble.end):
        match = INCLUDE_LINE.match(lines[offset])
        if not match:
            continue
        include = "#include " + match.group(1)
        if include in POOL:
            continue
        POOL.append(include)
        if triggers(include) is None:
            unknown.append(include)
    for include in unknown:
        sys.stderr.write("split-main: no rule for %s, so no new header will take it; main.cpp keeps\n"
                         "            it, and a header that needs it will have to be told.\n" % include)
    FILE_TEXT.clear()
    for name in HEADERS:
        FILE_TEXT[name] = "".join("".join(lines[s.start:s.end])
                                  for s in SEGMENTS if s.out == name)
    return mask


# ----- the one textual change: inline --------------------------------------------------------------

STATIC_TO_INLINE = {
    "static QString dataRoot() {": "inline QString dataRoot() {",
    "static int relayFuzzyScore(const QString &needle, const QString &haystack) {":
        "inline int relayFuzzyScore(const QString &needle, const QString &haystack) {",
}

OUT_OF_LINE = re.compile(
    r"^(?!inline\b)(?:[A-Za-z_][A-Za-z0-9_:<>,\s*&]*?\s[*&]?\s*)?"
    r"WindowManager::~?[A-Za-z_][A-Za-z0-9_]*\s*\(")


def transform(segment, lines):
    """Return the segment's lines, plus the list of (line number, before, after) it changed."""
    out, changed = [], []
    for offset in range(segment.start, segment.end):
        line = lines[offset]
        body, newline = line.rstrip("\n"), line[len(line.rstrip("\n")):]
        if body in STATIC_TO_INLINE:
            new = STATIC_TO_INLINE[body]
            changed.append((offset + 1, body, new))
            out.append(new + newline)
        elif segment.out == "WindowManagerImpl.h" and OUT_OF_LINE.match(body):
            new = "inline " + body
            changed.append((offset + 1, body, new))
            out.append(new + newline)
        else:
            if segment.out == "WindowManagerImpl.h" and body[:1] not in ("", " ", "\t", "/", "}", "#"):
                # A definition at column 0 that is not a WindowManager member would silently lose
                # its `inline` and break the one-definition rule the moment anything else includes
                # this header. Refuse rather than emit it.
                die("line %d of the WindowManager definitions is at column 0 but is not a\n"
                    "            WindowManager member, so the tool does not know to mark it "
                    "`inline`: %r" % (offset + 1, body))
            out.append(line)
    return out, changed


# ----- writing ---------------------------------------------------------------------------------

def boilerplate(name):
    out = [SPDX + "\n", "#pragma once\n", "\n", DOC[name] + "\n", "\n"]
    for sibling in SIBLINGS[name]:
        out.append('#include "%s"\n' % sibling)
    own = includes_for(name)
    if SIBLINGS[name] and own:
        out.append("\n")
    last = None
    for include in own:
        if last is not None and group_of(include) != last:
            out.append("\n")
        out.append(include + "\n")
        last = group_of(include)
    out.append("\n")
    return "".join(out).splitlines(keepends=True)


def main_includes():
    out = ["// The units main.cpp used to hold inline, one header each (see docs/ARCHITECTURE.md).\n",
           "// This is still one translation unit: the headers are included here and nowhere else,\n",
           "// so Pane and RelayWindow keep their bodies in the class and the build stays as it was.\n"]
    for name in HEADERS:
        out.append('#include "%s"\n' % name)
    out.append("\n")
    return out


def build(lines):
    files, changes = {}, []
    for name in OUT_ORDER:
        files[name] = [] if name == "main.cpp" else boilerplate(name)
    for segment in SEGMENTS:
        body, changed = transform(segment, lines)
        changes.extend(changed)
        files[segment.out].extend(body)
        if segment.name == "preamble":
            files["main.cpp"].extend(main_includes())
    return files, changes


# ----- the proof: every original line, once, in order --------------------------------------------

def check(lines, outdir):
    """Read back what was written and rebuild the original from it."""
    rebuilt = {}
    for name in OUT_ORDER:
        path = os.path.join(outdir, "src", name)
        if not os.path.exists(path):
            die("--check: %s was not written" % path)
        with open(path, encoding="utf-8") as handle:
            content = handle.readlines()
        if name == "main.cpp":
            block = main_includes()
            joined, needle = "".join(content), "".join(block)
            if joined.count(needle) != 1:
                die("--check: main.cpp does not carry the generated include block exactly once")
            cut = joined.index(needle)
            content = (joined[:cut] + joined[cut + len(needle):]).splitlines(keepends=True)
        else:
            head = boilerplate(name)
            if content[:len(head)] != head:
                die("--check: %s does not start with the boilerplate the tool writes" % name)
            content = content[len(head):]
        rebuilt[name] = content

    expected, actual = [], []
    for segment in SEGMENTS:
        body, _ = transform(segment, lines)
        expected.extend((segment.name, segment.start + 1 + i, line)
                        for i, line in enumerate(body))
    for segment in SEGMENTS:
        take = len(list(range(segment.start, segment.end)))
        got = rebuilt[segment.out][:take]
        del rebuilt[segment.out][:take]
        actual.extend((segment.name, segment.start + 1 + i, line) for i, line in enumerate(got))

    for name, leftover in rebuilt.items():
        if leftover:
            die("--check: %s has %d lines the segments do not account for, first %r"
                % (name, len(leftover), leftover[0].rstrip("\n")))

    if len(expected) != len(actual):
        die("--check: %d lines expected, %d found" % (len(expected), len(actual)))
    for (seg, number, want), (_, _, got) in zip(expected, actual):
        if want != got:
            die("--check: line %d (segment %s) came back as %r, expected %r"
                % (number, seg, got.rstrip("\n"), want.rstrip("\n")))

    # ...and the sequence we just rebuilt is the original file, line for line, modulo `inline`.
    original = list(lines)
    rebuilt_lines = [line for _, _, line in actual]
    if len(original) != len(rebuilt_lines):
        die("--check: the rebuilt file has %d lines, the original %d"
            % (len(rebuilt_lines), len(original)))
    _, changes = build(lines)
    edited = {number for number, _, _ in changes}
    for i, (before, after) in enumerate(zip(original, rebuilt_lines), start=1):
        if before == after:
            continue
        if i in edited:
            continue
        die("--check: line %d changed and was not one of the `inline` edits:\n"
            "            before %r\n            after  %r"
            % (i, before.rstrip("\n"), after.rstrip("\n")))
    print("--check: %d lines in, %d lines out, in order, each exactly once; %d changed, all `inline`."
          % (len(original), len(rebuilt_lines), len(changes)))
    return 0


def report(files, changes, lines):
    print("split-main: %d lines in" % len(lines))
    for name in OUT_ORDER:
        moved = sum(segment.end - segment.start for segment in SEGMENTS if segment.out == name)
        print("  src/%-22s %6d lines (%d moved)" % (name, len(files[name]), moved))
    print("split-main: %d moved lines changed, all of them `inline`:" % len(changes))
    for number, before, after in changes:
        print("  line %5d  - %s" % (number, before))
        print("              + %s" % after)


def run():
    parser = argparse.ArgumentParser(description="Split src/main.cpp into one header per unit.")
    parser.add_argument("--in", dest="source", required=True, help="the main.cpp text to split")
    parser.add_argument("--out", dest="outdir", required=True,
                        help="directory to write <outdir>/src/... into")
    parser.add_argument("--check", action="store_true",
                        help="verify a previous run: rebuild the original from the written files")
    parser.add_argument("--manifest", help="also write a JSON description of the cut here")
    args = parser.parse_args()

    with open(args.source, encoding="utf-8") as handle:
        lines = handle.readlines()
    plan(lines)

    if args.check:
        return check(lines, args.outdir)

    files, changes = build(lines)
    target = os.path.join(args.outdir, "src")
    if not os.path.isdir(target):
        die("no such directory: %s" % target)
    for name in OUT_ORDER:
        with open(os.path.join(target, name), "w", encoding="utf-8") as handle:
            handle.writelines(files[name])
    if args.manifest:
        with open(args.manifest, "w", encoding="utf-8") as handle:
            json.dump([{"segment": s.name, "file": s.out, "first": s.start + 1, "last": s.end}
                       for s in SEGMENTS], handle, indent=2)
    report(files, changes, lines)
    return 0


if __name__ == "__main__":
    sys.exit(run())
