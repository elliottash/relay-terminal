// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The tool-call `label` (docs/AGENT-SESSIONS-PROTOCOL.md § 23), turned into the one concise line
// every surface prints. Pure: QtCore only, no widget and no theme, so every rule is tested
// headless (tests/toollabel_test.cpp).
//
// The backend puts a `label` on `tool_started`, `tool_result`, each `turn_summary.tools[]` item and
// the `tool_output_get` reply:
//
//     {"kind": "run", "running": "running pytest", "title": "ran pytest",
//      "stats": ["212 lines", "exit 1", "8 s"], "ok": false, "error": "…", "path": "x.py",
//      "inline_diff": true, "open": {"type": "fold"},
//      "merge": {"key": "read", "singular": "file", "plural": "files", "lines": 412}}
//
// A line is `title` plus `stats` joined with " · ". The ▸ / ✗ marker, the fold arrow and every
// colour belong to the surface; the backend never sends decoration, and neither does this file.
//
// `preview` is legacy: nothing here parses it for display, but fromEvent() falls back to it when
// there is no `label` at all — an older worker, or a transcript stored before § 23 existed.
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace relay::toollabel {

// One parsed label. Every field is empty or false when the event did not carry it, so a caller
// may use a default-constructed Label without checking anything but `valid`.
struct Label {
    bool valid = false;        // the event carried a `label` object (a fallback label is valid too)
    bool fallback = false;     // built from the legacy `preview`, not from a `label`

    QString kind;              // run, job, read, list, edit, agent, plan, skill, board, … (§ 23.3)
    QString running;           // present tense, while the call runs: "running pytest"
    QString title;             // past tense, once it is done: "ran pytest", "wrote x.py"
    QStringList stats;         // short pieces, already formatted, in display order (§ 23.4)
    QString error;             // only when the call did not happen; first line, ≤ 120 characters
    QString path;              // workspace-relative path, for the file kinds

    bool hasOk = false;        // `ok` was sent (it is on tool_result only)
    bool ok = true;
    bool hasInlineDiff = false;
    bool inlineDiff = false;   // the diff is at most 12 changed lines: print it with no click

    QString openType;          // fold | file | diff | subagent | card | plan | todos (§ 23.6)
    QString openPath;          // open.path, for openType == "file"
    QString openId;            // open.id, for openType == "subagent" and "card"

    bool hasMerge = false;     // consecutive calls with the same key may become one line (§ 23.7)
    QString mergeKey;          // "read" or "list"
    QString mergeSingular, mergePlural;   // "file" / "files", "folder" / "folders"
    QString mergeUnit;         // what mergeCount counts: "lines" (read) or "entries" (list)
    qint64 mergeCount = 0;

    // The call did not happen, or it happened and failed. A surface draws ✗ for this.
    bool failed() const { return !error.isEmpty() || (hasOk && !ok); }
    // The finished line, without any marker: title, then the stats, then the error when the call
    // never ran — "ran pytest · 212 lines · exit 1 · 8 s", "edit x.py · old_string was not found".
    QString line() const;
    // The line to show while the call is still running: "running pytest". Empty when the label has
    // no `running` (then the surface keeps the title).
    QString runningLine() const { return running; }
};

// One label object, exactly as § 23.2 defines it.
Label parse(const QJsonObject &label);

// A whole event — `tool_started`, `tool_result`, a `turn_summary.tools[]` item, or the
// `tool_output_get` reply. Uses its `label` when it has one; otherwise builds a fallback from the
// legacy `preview` and `tool` so an old worker and a stored transcript still get one line.
Label fromEvent(const QJsonObject &event);

// A run of consecutive calls that may be folded into one line (§ 23.7): only `read_file`,
// `read_skill_file` and `list_directory` carry `merge`, a failed call never does, and any call
// without the run's key ends it.
//
//     MergeRun run;
//     if (run.accepts(label)) run.add(label);          // rewrite the run's line
//     else { run.clear(); if (label.hasMerge) run.add(label); }   // start a new one
//
// `add()` on a label the run does not accept starts a fresh run with it, so a caller that does not
// care about the distinction may simply call add() and read count().
class MergeRun {
public:
    // Would `label` continue the run in progress? False for a label with no `merge`, for a failed
    // call, for a different key, and for every label when no run is in progress.
    bool accepts(const Label &label) const;
    // Add a member: continues the run when accepts() is true, and otherwise starts a new run with
    // this label (or clears the run, when the label cannot be merged at all).
    void add(const Label &label);
    void clear();

    bool active() const { return m_count > 0; }
    // Members so far. A surface shows the member's own line while this is 1 and replaces it with
    // line() from 2 on.
    int count() const { return m_count; }
    QString key() const { return m_key; }
    qint64 total() const { return m_total; }
    // "read 6 files · 4,100 lines", "listed 3 folders · 92 entries". Empty when no run is active.
    QString line() const;

private:
    QString m_key, m_singular, m_plural, m_unit;
    int m_count = 0;
    qint64 m_total = 0;
};

// "4,100": the thousands separator § 23.4 asks for, independent of the user's locale (the backend
// has already grouped the numbers inside `stats`; this is for the sums a merged line adds up).
QString thousands(qint64 value);

// A path as a title shows it: the whole thing, or its base name once it passes 40 characters
// (§ 23.2). Exposed because a surface building its own line from `path` needs the same rule.
QString shortPath(const QString &path);

}  // namespace relay::toollabel
