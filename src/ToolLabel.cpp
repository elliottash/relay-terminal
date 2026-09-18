// SPDX-License-Identifier: GPL-3.0-or-later
#include "ToolLabel.h"
#include <QJsonArray>
#include <QJsonValue>

namespace relay::toollabel {

namespace {

constexpr int kPathLimit = 40;      // § 23.2: a longer path shortens to its base name
constexpr int kFallbackLimit = 60;  // how much of a legacy preview's first body line a line keeps

QString str(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString();
}

// The legacy preview's heading line ("RUN COMMAND", "WRITE FILE", …) mapped to the past-tense verb
// and the present-tense one. Everything this used to be parsed for is a field now (§ 23.1); this
// exists only so a worker from before § 23, or a transcript saved by one, still prints one line.
struct Legacy { const char *heading, *kind, *verb, *gerund; };
constexpr Legacy kLegacy[] = {
    {"RUN COMMAND", "run", "ran", "running"},
    {"READ FILE", "read", "read", "reading"},
    {"LIST DIRECTORY", "list", "listed", "listing"},
    {"WRITE FILE", "edit", "wrote", "writing"},
    {"EDIT FILE", "edit", "edited", "editing"},
};

// The lines a preview carries that are not its subject: the trailing notes every file tool adds.
bool isPreviewNote(const QString &line) {
    return line.startsWith(QStringLiteral("Working directory: ")) || line.startsWith(QStringLiteral("Timeout: "))
        || line.startsWith(QStringLiteral("Old bytes: ")) || line.startsWith(QStringLiteral("New bytes: "));
}

QString humanise(QString name) {
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return name.trimmed();
}

}  // namespace

QString thousands(qint64 value) {
    const bool negative = value < 0;
    QString digits = QString::number(negative ? -value : value);
    for (int at = digits.size() - 3; at > 0; at -= 3) digits.insert(at, QLatin1Char(','));
    return negative ? QLatin1Char('-') + digits : digits;
}

QString shortPath(const QString &path) {
    if (path.size() <= kPathLimit) return path;
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 && slash + 1 < path.size() ? path.mid(slash + 1) : path;
}

QString Label::line() const {
    QStringList pieces;
    if (!title.isEmpty()) pieces << title;
    pieces << stats;
    // A call that never happened says why on the same line; one that ran and failed already has
    // "exit 1" in its stats and must not repeat itself (§ 23.2).
    if (!error.isEmpty()) pieces << error;
    return pieces.join(QStringLiteral(" · "));
}

Label parse(const QJsonObject &label) {
    Label out;
    if (label.isEmpty()) return out;
    out.valid = true;
    out.kind = str(label, "kind");
    out.running = str(label, "running");
    out.title = str(label, "title");
    out.error = str(label, "error");
    out.path = str(label, "path");
    for (const QJsonValue &stat : label.value(QStringLiteral("stats")).toArray()) {
        const QString text = stat.toString();
        if (!text.isEmpty()) out.stats << text;
    }
    if (label.contains(QStringLiteral("ok"))) { out.hasOk = true; out.ok = label.value(QStringLiteral("ok")).toBool(true); }
    if (label.contains(QStringLiteral("inline_diff"))) {
        out.hasInlineDiff = true;
        out.inlineDiff = label.value(QStringLiteral("inline_diff")).toBool();
    }
    const QJsonObject open = label.value(QStringLiteral("open")).toObject();
    out.openType = str(open, "type");
    out.openPath = str(open, "path");
    out.openId = str(open, "id");
    const QJsonObject merge = label.value(QStringLiteral("merge")).toObject();
    if (!merge.isEmpty() && !str(merge, "key").isEmpty()) {
        out.hasMerge = true;
        out.mergeKey = str(merge, "key");
        out.mergeSingular = str(merge, "singular");
        out.mergePlural = str(merge, "plural");
        // The backend counts lines for a read and entries for a listing; either may be absent on
        // the started label, which knows the fixed half of `merge` only.
        if (merge.contains(QStringLiteral("lines"))) {
            out.mergeUnit = QStringLiteral("lines");
            out.mergeCount = merge.value(QStringLiteral("lines")).toVariant().toLongLong();
        } else if (merge.contains(QStringLiteral("entries"))) {
            out.mergeUnit = QStringLiteral("entries");
            out.mergeCount = merge.value(QStringLiteral("entries")).toVariant().toLongLong();
        }
    }
    return out;
}

Label fromEvent(const QJsonObject &event) {
    const QJsonObject label = event.value(QStringLiteral("label")).toObject();
    if (!label.isEmpty()) {
        Label out = parse(label);
        // turn_summary items carry their verdict beside the label rather than inside it.
        if (!out.hasOk && event.contains(QStringLiteral("ok"))) {
            out.hasOk = true;
            out.ok = event.value(QStringLiteral("ok")).toBool(true);
        }
        return out;
    }

    // ---- the fallback, for a worker or a stored transcript from before § 23 -------------------
    Label out;
    const QString tool = event.contains(QStringLiteral("tool")) ? str(event, "tool") : str(event, "name");
    const QString preview = str(event, "preview");
    if (tool.isEmpty() && preview.isEmpty()) return out;
    out.valid = true;
    out.fallback = true;
    out.kind = QStringLiteral("other");

    const QStringList lines = preview.left(8000).split(QLatin1Char('\n'));
    const QString heading = lines.isEmpty() ? QString() : lines.first().trimmed();
    // What the call is about: the first real line under the heading for a known preview, and the
    // first real line at all for anything else (a preview with no heading is all subject).
    QString subject, loose;
    for (int at = 0; at < lines.size(); ++at) {
        const QString line = lines.at(at).trimmed();
        if (line.isEmpty() || isPreviewNote(lines.at(at))) continue;
        if (loose.isEmpty()) loose = line;
        if (at > 0) { subject = line; break; }
    }
    for (const Legacy &legacy : kLegacy) {
        if (heading != QLatin1String(legacy.heading)) continue;
        out.kind = QString::fromLatin1(legacy.kind);
        const QString what = out.kind == QLatin1String("run") ? subject.left(kPathLimit) : shortPath(subject);
        if (out.kind != QLatin1String("run") && !subject.isEmpty()) out.path = subject;
        out.title = what.isEmpty() ? QString::fromLatin1(legacy.verb)
                                   : QString::fromLatin1(legacy.verb) + QLatin1Char(' ') + what;
        out.running = what.isEmpty() ? QString::fromLatin1(legacy.gerund)
                                     : QString::fromLatin1(legacy.gerund) + QLatin1Char(' ') + what;
        break;
    }
    if (out.title.isEmpty()) {
        // An unknown heading, or none: the humanised tool name, plus the preview's subject while it
        // is one short line (the same rule § 23.3 gives an unknown tool).
        const QString name = humanise(tool.isEmpty() ? QStringLiteral("tool") : tool);
        const QString extra = loose.size() <= kFallbackLimit ? loose : QString();
        out.title = extra.isEmpty() ? name : name + QLatin1Char(' ') + extra;
        out.running = QStringLiteral("running ") + name;
    }
    if (event.contains(QStringLiteral("ok"))) { out.hasOk = true; out.ok = event.value(QStringLiteral("ok")).toBool(true); }
    const QJsonObject result = event.value(QStringLiteral("result")).toObject();
    if (result.contains(QStringLiteral("error"))) {
        out.hasOk = true;
        out.ok = false;
        out.error = result.value(QStringLiteral("error")).toString().section(QLatin1Char('\n'), 0, 0).left(120);
    }
    // An old `tool_result` keeps the exit code inside `result`; a turn_summary item has it beside
    // the preview. Either way the legacy line says so, whether or not it is zero.
    const QJsonValue exit = event.contains(QStringLiteral("exit_code")) ? event.value(QStringLiteral("exit_code"))
                                                                       : result.value(QStringLiteral("exit_code"));
    if (exit.isDouble()) {
        const int code = exit.toInt();
        out.stats << QStringLiteral("exit %1").arg(code);
        if (code != 0) { out.hasOk = true; out.ok = false; }
    }
    return out;
}

// ---- merged runs (§ 23.7) --------------------------------------------------------------------

bool MergeRun::accepts(const Label &label) const {
    if (!m_count || !label.hasMerge || label.failed()) return false;
    return label.mergeKey == m_key;
}

void MergeRun::add(const Label &label) {
    if (!label.hasMerge || label.failed()) { clear(); return; }
    if (label.mergeKey != m_key) {
        clear();
        m_key = label.mergeKey;
        m_singular = label.mergeSingular;
        m_plural = label.mergePlural;
    }
    if (!label.mergeUnit.isEmpty()) m_unit = label.mergeUnit;
    ++m_count;
    m_total += label.mergeCount;
}

void MergeRun::clear() {
    m_key.clear(); m_singular.clear(); m_plural.clear(); m_unit.clear();
    m_count = 0; m_total = 0;
}

QString MergeRun::line() const {
    if (!m_count) return {};
    // The verb the merged line leads with. Only two keys exist (§ 23.7); an unknown one falls back
    // to its own name rather than inventing English.
    const QString verb = m_key == QStringLiteral("read")   ? QStringLiteral("read")
                       : m_key == QStringLiteral("list")   ? QStringLiteral("listed")
                                                           : m_key;
    const QString noun = m_count == 1 ? (m_singular.isEmpty() ? m_key : m_singular)
                                      : (m_plural.isEmpty() ? m_key : m_plural);
    QString text = QStringLiteral("%1 %2 %3").arg(verb, QString::number(m_count), noun);
    if (!m_unit.isEmpty()) {
        const QString unit = m_total == 1 ? (m_unit == QStringLiteral("entries") ? QStringLiteral("entry")
                                                                                 : QStringLiteral("line"))
                                          : m_unit;
        text += QStringLiteral(" · %1 %2").arg(thousands(m_total), unit);
    }
    return text;
}

}  // namespace relay::toollabel
