// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardModel.h"

#include "Projects.h"

#include <QRegularExpression>

#include <QJsonValue>
#include <QLocale>
#include <QTimeZone>
#include <algorithm>

namespace relay {
namespace board {
namespace {

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        const QString text = item.toString();
        if (!text.isEmpty())
            out << text;
    }
    return out;
}

// Where a status that no configured column collects sorts among the sections: a plan's
// lifecycle first, then the parked ones, then anything the board has invented.
int extraStatusRank(const QString &status)
{
    static const QMap<QString, int> ranks{
        {QStringLiteral("planning"), 1}, {QStringLiteral("draft"), 2},
        {QStringLiteral("approved"), 3}, {QStringLiteral("executing"), 4},
        {QStringLiteral("active"), 5},   {QStringLiteral("planned"), 6},
        {QStringLiteral("needs-verification"), 7},
        {QStringLiteral("deferred"), 8}, {QStringLiteral("retired"), 9}};
    return ranks.value(status, 5);
}

// Used when the worker sends no column_statuses (an old board.yaml, or a test fixture).
QStringList fallbackStatuses(const QString &column)
{
    return defaultSectionStatuses(column);
}

bool containsCaseless(const QStringList &list, const QString &value)
{
    for (const QString &item : list)
        if (item.compare(value, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// Subsequence match, the same shape as the composer's `@` picker: every character of the
// query appears in order, and a run of adjacent characters scores higher.
int fuzzy(const QString &query, const QString &text)
{
    if (query.isEmpty())
        return 1;
    int points = 0, run = 0, at = 0;
    for (int i = 0; i < text.size() && at < query.size(); ++i) {
        if (text.at(i).toLower() != query.at(at).toLower()) {
            run = 0;
            continue;
        }
        ++at;
        ++run;
        points += 1 + run;
        if (i == 0 || text.at(i - 1) == QLatin1Char(' ') || text.at(i - 1) == QLatin1Char('-'))
            points += 3;   // a word start
    }
    return at == query.size() ? points : 0;
}

}  // namespace

QStringList defaultSectionStatuses(const QString &column)
{
    if (column == QStringLiteral("waiting"))
        return {QStringLiteral("needs-review"), QStringLiteral("needs-labels"), QStringLiteral("needs-ab")};
    if (column == QStringLiteral("needs-qa"))
        return {QStringLiteral("needs-qa-llm"), QStringLiteral("needs-qa-human")};
    if (column == QStringLiteral("done"))
        return {QStringLiteral("done"), QStringLiteral("dropped")};
    return {column};
}

QString statusTitle(const QString &status)
{
    static const QMap<QString, QString> names{
        {QStringLiteral("inbox"), QStringLiteral("Inbox")},
        {QStringLiteral("discussing"), QStringLiteral("Discussing")},
        // The stage statuses of #3XZV: a card walks inbox → discussing → planning → planned →
        // executing → needs verification → needs QA → done, and Relay makes each move at the
        // event that earns it. The ids are new; `ready` and `in-progress` stay valid so an
        // older board is untouched.
        {QStringLiteral("planning"), QStringLiteral("Planning")},
        {QStringLiteral("planned"), QStringLiteral("Planned")},
        // "Ready" alone was read as "ready to ship" (owner, 2026-09-19: "what does ready mean?
        // done or inbox?"). The status id stays `ready`, so no card file and no folder moves.
        {QStringLiteral("ready"), QStringLiteral("Ready to start")},
        {QStringLiteral("in-progress"), QStringLiteral("In progress")},
        {QStringLiteral("needs-verification"), QStringLiteral("Needs verification")},
        {QStringLiteral("needs-review"), QStringLiteral("Needs review")},
        {QStringLiteral("needs-labels"), QStringLiteral("Needs labels")},
        {QStringLiteral("needs-ab"), QStringLiteral("Needs A/B")},
        {QStringLiteral("needs-qa"), QStringLiteral("Needs QA")},
        {QStringLiteral("needs-qa-llm"), QStringLiteral("Needs QA (LLM)")},
        {QStringLiteral("needs-qa-human"), QStringLiteral("Needs QA (human)")},
        {QStringLiteral("deferred"), QStringLiteral("Deferred")},
        {QStringLiteral("done"), QStringLiteral("Done")},
        {QStringLiteral("dropped"), QStringLiteral("Dropped")},
        {QStringLiteral("waiting"), QStringLiteral("Waiting")},
        {QStringLiteral("draft"), QStringLiteral("Draft")},
        {QStringLiteral("approved"), QStringLiteral("Approved")},
        {QStringLiteral("executing"), QStringLiteral("Executing")},
        {QStringLiteral("active"), QStringLiteral("Active")},
        {QStringLiteral("retired"), QStringLiteral("Retired")}};
    const QString known = names.value(status);
    if (!known.isEmpty())
        return known;
    QString text = status;
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

// What a section means, in one clause, from the column table in docs/SWITCHBOARD-DESIGN.md
// section 3. The pane shows it wherever a section is named without its cards — the header's
// tooltip and the hide checkboxes — because a header read all day is the one place a person
// asks "what is this lane for?" and the list itself cannot answer.
QString sectionMeaning(const QString &id)
{
    static const QMap<QString, QString> meanings{
        {QStringLiteral("inbox"), QStringLiteral("raw capture, not triaged yet")},
        {QStringLiteral("discussing"),
         QStringLiteral("an open question — waiting_on says who owes the answer")},
        {QStringLiteral("planning"), QStringLiteral("a Plan turn is writing its plan")},
        {QStringLiteral("planned"), QStringLiteral("the plan is written, not started")},
        {QStringLiteral("ready"), QStringLiteral("agreed and not started — anyone may pick it up")},
        {QStringLiteral("in-progress"), QStringLiteral("someone or some agent has it now")},
        {QStringLiteral("executing"), QStringLiteral("the plan is being carried out")},
        {QStringLiteral("needs-verification"),
         QStringLiteral("built, waiting for its implementer's checklist to be checked")},
        {QStringLiteral("waiting"),
         QStringLiteral("built, and a check is owed: review, labels or an A/B")},
        {QStringLiteral("needs-review"), QStringLiteral("built, waiting to be read by a person")},
        {QStringLiteral("needs-labels"), QStringLiteral("built, waiting for its labelling run")},
        {QStringLiteral("needs-ab"), QStringLiteral("built, waiting for an A/B")},
        {QStringLiteral("needs-qa"), QStringLiteral("built, waiting to be verified")},
        {QStringLiteral("needs-qa-llm"), QStringLiteral("built, waiting for a model to verify it")},
        {QStringLiteral("needs-qa-human"), QStringLiteral("built, waiting for you to verify it")},
        {QStringLiteral("deferred"), QStringLiteral("agreed, but not now")},
        {QStringLiteral("verified"), QStringLiteral("closed and signed by whoever verified it")},
        {QStringLiteral("done"), QStringLiteral("closed, with a Resolution — dropped cards too")},
        {QStringLiteral("draft"), QStringLiteral("a plan still being written")},
        {QStringLiteral("approved"), QStringLiteral("a plan you approved, not started")},
        {QStringLiteral("active"), QStringLiteral("in use — loaded when it applies")},
        {QStringLiteral("retired"), QStringLiteral("kept for the record, never loaded")}};
    return meanings.value(id);
}

QString tabTitle(const QString &id)
{
    if (id == QStringLiteral("planning"))
        return QStringLiteral("Plans");
    QString text = id;
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

QString sortId(Sort sort)
{
    switch (sort) {
    case Sort::NewestFirst:
        return QStringLiteral("newest");
    case Sort::OldestFirst:
        return QStringLiteral("oldest");
    case Sort::RecentlyUpdated:
        return QStringLiteral("updated");
    case Sort::OldestUpdated:
        return QStringLiteral("updated-oldest");
    case Sort::TitleAsc:
        return QStringLiteral("title");
    case Sort::TitleDesc:
        return QStringLiteral("title-desc");
    case Sort::PriorityHigh:
        return QStringLiteral("priority");
    case Sort::PriorityLow:
        return QStringLiteral("priority-low");
    case Sort::Manual:
        break;
    }
    return QStringLiteral("manual");
}

Sort sortFromId(const QString &id)
{
    if (id == QStringLiteral("newest"))
        return Sort::NewestFirst;
    if (id == QStringLiteral("oldest"))
        return Sort::OldestFirst;
    if (id == QStringLiteral("updated"))
        return Sort::RecentlyUpdated;
    if (id == QStringLiteral("updated-oldest"))
        return Sort::OldestUpdated;
    if (id == QStringLiteral("title"))
        return Sort::TitleAsc;
    if (id == QStringLiteral("title-desc"))
        return Sort::TitleDesc;
    if (id == QStringLiteral("priority"))
        return Sort::PriorityHigh;
    if (id == QStringLiteral("priority-low"))
        return Sort::PriorityLow;
    return Sort::Manual;
}

QString sortTitle(Sort sort)
{
    switch (sort) {
    case Sort::NewestFirst:
        return QStringLiteral("Newest first");
    case Sort::OldestFirst:
        return QStringLiteral("Oldest first");
    case Sort::RecentlyUpdated:
        return QStringLiteral("Recently updated");
    case Sort::OldestUpdated:
        return QStringLiteral("Least recently updated");
    case Sort::TitleAsc:
        return QStringLiteral("Title A→Z");
    case Sort::TitleDesc:
        return QStringLiteral("Title Z→A");
    case Sort::PriorityHigh:
        return QStringLiteral("Priority high first");
    case Sort::PriorityLow:
        return QStringLiteral("Priority low first");
    case Sort::Manual:
        break;
    }
    return QStringLiteral("Manual");
}

QString columnTitle(SortColumn column)
{
    switch (column) {
    case SortColumn::Priority:
        // The flag itself, on the flag's own column: the cell is one glyph wide (#VKFV).
        return QStringLiteral("⚑");
    case SortColumn::Card:
        return QStringLiteral("Card");
    case SortColumn::Created:
        return QStringLiteral("Created");
    case SortColumn::Updated:
        return QStringLiteral("Updated");
    }
    return QString();
}

Sort nextColumnSort(SortColumn column, Sort current)
{
    switch (column) {
    case SortColumn::Priority:
        if (current == Sort::PriorityHigh)
            return Sort::PriorityLow;
        return current == Sort::PriorityLow ? Sort::Manual : Sort::PriorityHigh;
    case SortColumn::Card:
        if (current == Sort::TitleAsc)
            return Sort::TitleDesc;
        return current == Sort::TitleDesc ? Sort::Manual : Sort::TitleAsc;
    case SortColumn::Created:
        if (current == Sort::NewestFirst)
            return Sort::OldestFirst;
        return current == Sort::OldestFirst ? Sort::Manual : Sort::NewestFirst;
    case SortColumn::Updated:
        if (current == Sort::RecentlyUpdated)
            return Sort::OldestUpdated;
        return current == Sort::OldestUpdated ? Sort::Manual : Sort::RecentlyUpdated;
    }
    return Sort::Manual;
}

int sortColumnIndex(Sort sort)
{
    switch (sort) {
    case Sort::PriorityHigh:
    case Sort::PriorityLow:
        return 0;
    case Sort::TitleAsc:
    case Sort::TitleDesc:
        return 1;
    case Sort::NewestFirst:
    case Sort::OldestFirst:
        return 2;
    case Sort::RecentlyUpdated:
    case Sort::OldestUpdated:
        return 3;
    case Sort::Manual:
        break;
    }
    return -1;
}

bool sortAscending(Sort sort)
{
    return sort == Sort::OldestFirst || sort == Sort::OldestUpdated || sort == Sort::TitleAsc
           || sort == Sort::PriorityLow;
}

QString dateCell(const QString &stamp)
{
    const QString text = stamp.trimmed();
    if (text.size() < 10)
        return QString();
    for (int i = 0; i < 10; ++i) {
        const QChar character = text.at(i);
        if (i == 4 || i == 7) {
            if (character != QLatin1Char('-'))
                return QString();
        } else if (!character.isDigit()) {
            return QString();
        }
    }
    return text.left(10);
}

QString issueHeading()
{
    return QStringLiteral("Issue");
}

QString modeTitle(const QString &mode)
{
    if (mode == QStringLiteral("discuss"))
        return QStringLiteral("Discuss");
    if (mode == QStringLiteral("plan"))
        return QStringLiteral("Plan");
    if (mode == QStringLiteral("execute"))
        return QStringLiteral("Execute");
    return {};
}

QString threadMarkdown(const QString &text, const QString &kind)
{
    static const QRegularExpression summary(QStringLiteral("<details>\\s*<summary>(.*?)</summary>"),
                                            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression close(QStringLiteral("\\s*</details>"));
    QString out = text;
    out.replace(summary, QStringLiteral("**\\1**"));
    out.replace(close, QString());
    if (kind == QStringLiteral("rewrite") && out.startsWith(QStringLiteral("- ")))
        out = out.mid(2);
    return out;
}

QString executeTask(const QString &id, const QString &title, bool hasPlan, bool hasAcceptance,
                    const QString &note)
{
    const QString ref = QStringLiteral("#") + id;
    QStringList lines;
    lines << QStringLiteral("Execute %1: %2").arg(ref, title) << QString();
    QString what = QStringLiteral("The Switchboard card %1 is attached");
    if (hasPlan && hasAcceptance)
        what += QStringLiteral(" with its issue, its `## Plan` and its acceptance. Carry out the plan "
                               "until the acceptance holds. Where the plan has an Orchestration "
                               "block, follow it: parallel steps to subagents started together, "
                               "dependent waves in order.");
    else if (hasPlan)
        what += QStringLiteral(" with its issue and its `## Plan`. Carry out the plan. Where the "
                               "plan has an Orchestration block, follow it: parallel steps to "
                               "subagents started together, dependent waves in order.");
    else if (hasAcceptance)
        what += QStringLiteral(" with its issue and its acceptance. It has no plan: read the code "
                               "first, then implement it until the acceptance holds.");
    else
        what += QStringLiteral(" with its issue. It has no plan and no acceptance: read the code "
                               "first, and say what you took \"done\" to mean when you finish.");
    lines << what.arg(ref) << QString();
    lines << QStringLiteral("The owner handed it to you from the Switchboard; it is already in "
                            "progress and assigned to the agent.")
          // #T71W: the worker knows its own preset and model exactly and stamps `implemented_by`
          // itself, so the agent is not asked to type a signature it can only guess at.
          << QStringLiteral("- %1's `implemented_by` is stamped with your provider and model by "
                            "the board itself; you do not have to set it.").arg(ref)
          << QStringLiteral("- Put %1 in the message of every commit you make for it, and after "
                            "each commit add its short hash to the card's `links.commits` "
                            "(board_update_card `fields.links`: the card's whole `links` object "
                            "from board_read, with the hash appended).").arg(ref)
          // #T71W: the signature travels with the commits, not only with the card, so a reader of
          // `git log --grep '#ID'` can tell who wrote each one. The card's own `implemented_by` is
          // the worker's to stamp, so the agent is not asked for it twice.
          << QStringLiteral("- Sign every one of those commits with the trailer "
                            "`Implemented-By: <vendor>/<your exact model id>` on its own line at "
                            "the end of the message: lower case, the vendor of the *model* and the "
                            "id you are actually running, not the family — "
                            "`anthropic/claude-opus-5`, `openai/gpt-6-astra`, `glm/glm-5.3`. The "
                            "card's `implemented_by` is stamped for you; the trailer is not.")
          << QStringLiteral("- If you are Claude Code or Codex, append ` via claude-code` or "
                            "` via codex` to that signature and still name the model you are on "
                            "(`anthropic/claude-opus-5 via claude-code`). Only when you cannot see "
                            "which model you are is `anthropic/claude-code` or `openai/codex` on "
                            "its own the right answer.")
          << QStringLiteral("- Post progress, questions and decisions on %1 with board_comment, "
                            "not only here.").arg(ref)
          << QStringLiteral("- When it lands, move %1 to needs-verification with the evidence "
                            "path and a `## QA checklist`, as the Switchboard rules say. Its "
                            "verifier then moves it on to needs QA, or back to an earlier "
                            "stage.").arg(ref);
    if (!note.trimmed().isEmpty())
        lines << QString() << QStringLiteral("The owner adds, verbatim:") << note.trimmed();
    return lines.join(QLatin1Char('\n'));
}

// ---- cross-provider QA (#T71W) --------------------------------------------------------------

QString familyLabel(const QString &family)
{
    static const QMap<QString, QString> names{
        {QStringLiteral("openai"), QStringLiteral("OpenAI")},
        // The design's own line reads "Claude skipped: …", and that is the name a reader of this
        // board uses for the family; "Anthropic" would be the company, not the verifier.
        {QStringLiteral("anthropic"), QStringLiteral("Claude")},
        {QStringLiteral("glm"), QStringLiteral("GLM")},
        {QStringLiteral("kimi"), QStringLiteral("Kimi")},
        {QStringLiteral("deepseek"), QStringLiteral("DeepSeek")},
        {QStringLiteral("gemini"), QStringLiteral("Gemini")},
        {QStringLiteral("minimax"), QStringLiteral("MiniMax")},
        {QStringLiteral("relay-free"), QStringLiteral("Relay Free")},
        {QStringLiteral("local"), QStringLiteral("Local")}};
    const QString known = names.value(family);
    if (!known.isEmpty())
        return known;
    QString text = family;
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

namespace {

// One word of a model id, capitalised the way a person writes it. Only the two lists below are
// invented; everything else keeps the id's own letters, so a model that ships tomorrow reads
// correctly without an edit here.
QString modelWord(const QString &word, bool *acronym)
{
    static const QMap<QString, QString> spellings{
        {QStringLiteral("openai"), QStringLiteral("OpenAI")},
        {QStringLiteral("deepseek"), QStringLiteral("DeepSeek")},
        {QStringLiteral("minimax"), QStringLiteral("MiniMax")},
        {QStringLiteral("zai"), QStringLiteral("Z.AI")}};
    // Said letter by letter, so it is written that way. A four-letter word is *not* an acronym by
    // its length: "kimi" and "qwen" are names.
    static const QSet<QString> acronyms{QStringLiteral("glm"), QStringLiteral("gpt"),
                                        QStringLiteral("llm"), QStringLiteral("api"),
                                        QStringLiteral("xai"), QStringLiteral("qa")};
    if (acronym)
        *acronym = false;
    const QString lower = word.toLower();
    if (spellings.contains(lower))
        return spellings.value(lower);
    if (acronyms.contains(lower)) {
        if (acronym)
            *acronym = true;
        return word.toUpper();
    }
    if (word.isEmpty() || !word.at(0).isLetter())
        return word;                       // "5.3", "27b": a version keeps its own shape
    return word.at(0).toUpper() + word.mid(1);
}

// "claude-opus-5" -> "Claude Opus 5", "glm-5.3" -> "GLM-5.3", "gpt-6-astra" -> "GPT-6 Astra".
// The hyphen survives only between an acronym and its version number, which is where a person
// writes one; every other `-` in a model id is a word break.
QString modelLabel(const QString &model)
{
    const QStringList words = model.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    QString out;
    bool previousWasAcronym = false;
    for (const QString &word : words) {
        bool acronym = false;
        const QString text = modelWord(word, &acronym);
        if (!out.isEmpty())
            out += (previousWasAcronym && !word.isEmpty() && word.at(0).isDigit())
                       ? QStringLiteral("-")
                       : QStringLiteral(" ");
        out += text;
        previousWasAcronym = acronym;
    }
    return out;
}

// One entry of `recommended` / `alternates` / `skipped` / `unavailable` as a name: its own
// `label` when the worker sent one, else the family's.
QString entryLabel(const QJsonObject &entry)
{
    const QString label = entry.value(QStringLiteral("label")).toString();
    return label.isEmpty() ? familyLabel(entry.value(QStringLiteral("family")).toString()) : label;
}

// What the parenthesis after the recommendation says: how this verifier is reachable here. The
// runner id is the worker's word on that, so nothing is looked up on this side.
QString runnerWord(const QString &runner)
{
    if (runner.startsWith(QStringLiteral("guest:")))
        return QStringLiteral("installed");
    if (runner.startsWith(QStringLiteral("preset:")))
        return QStringLiteral("key");
    return {};
}

}  // namespace

QString signatureLabel(const QString &signature)
{
    static const QRegularExpression parenthetical(QStringLiteral("\\([^)]*\\)"));
    // `anthropic/claude-opus-5 via claude-code`: the harness the model actually ran under, which
    // is the thing a person reopens, so it is named beside the model rather than instead of it.
    static const QRegularExpression via(QStringLiteral("\\s+via\\s+([A-Za-z0-9._-]+)$"));
    QString text = signature;
    text.remove(parenthetical);
    text = text.simplified();
    if (text.isEmpty())
        return {};
    QString harness;
    const QRegularExpressionMatch match = via.match(text);
    if (match.hasMatch()) {
        harness = match.captured(1);
        text = text.left(match.capturedStart()).trimmed();
    }
    QString label = modelLabel(text.section(QLatin1Char('/'), -1).trimmed());
    if (label.isEmpty())
        label = familyLabel(text.section(QLatin1Char('/'), 0, 0).trimmed());
    if (!harness.isEmpty()) {
        const QString harnessLabel = modelLabel(harness);
        if (!harnessLabel.isEmpty() && harnessLabel != label)
            label += QStringLiteral(" · ") + harnessLabel;
        else if (label.isEmpty())
            label = harnessLabel;
    }
    return label;
}

QString verifyNote(const QJsonObject &qa)
{
    return qa.value(QStringLiteral("note")).toString().trimmed();
}

QString verifyRunner(const QJsonObject &qa)
{
    return qa.value(QStringLiteral("recommended")).toObject().value(QStringLiteral("runner")).toString();
}

QString verifyLabel(const QJsonObject &qa)
{
    const QJsonObject recommended = qa.value(QStringLiteral("recommended")).toObject();
    return recommended.isEmpty() ? QString() : entryLabel(recommended);
}

QString verifyLine(const QJsonObject &qa)
{
    if (qa.isEmpty())
        return {};
    const QJsonObject recommended = qa.value(QStringLiteral("recommended")).toObject();
    const QString separator = QStringLiteral(" · ");
    if (recommended.isEmpty() || verifyRunner(qa).isEmpty()) {
        // Nothing to open: the line says why, family by family, so the reader knows what to
        // install or key rather than only that the button is dead.
        QStringList reasons;
        const auto collect = [&reasons](const QJsonArray &entries, const QString &word) {
            for (const QJsonValue &value : entries) {
                const QJsonObject entry = value.toObject();
                const QString why = entry.value(QStringLiteral("why")).toString();
                const QString name = entryLabel(entry) + word;
                reasons << (why.isEmpty() ? name : QStringLiteral("%1: %2").arg(name, why));
            }
        };
        // The worker says it better when it has something to say: Relay Free carries no verifier
        // at all, and "no key, no key, no key" would not tell the reader that.
        const QString note = verifyNote(qa);
        if (!note.isEmpty())
            return note;
        collect(qa.value(QStringLiteral("skipped")).toArray(), QStringLiteral(" skipped"));
        collect(qa.value(QStringLiteral("unavailable")).toArray(), QString());
        return reasons.isEmpty()
                   ? QStringLiteral("No verifier available.")
                   : QStringLiteral("No verifier available: ") + reasons.join(separator);
    }
    QStringList parts;
    // The worker says how the verifier is available here ("installed", "key", "on this
    // machine"); the runner's prefix is only the fallback, and it would call a local model "key".
    const QString said = recommended.value(QStringLiteral("available")).toString().trimmed();
    const QString word = said.isEmpty() ? runnerWord(verifyRunner(qa)) : said;
    parts << (word.isEmpty() ? QStringLiteral("Verify with %1").arg(entryLabel(recommended))
                             : QStringLiteral("Verify with %1 (%2)").arg(entryLabel(recommended), word));
    QStringList alternates;
    for (const QJsonValue &value : qa.value(QStringLiteral("alternates")).toArray())
        alternates << entryLabel(value.toObject());
    if (!alternates.isEmpty())
        parts << QStringLiteral("then ") + alternates.join(QStringLiteral(", "));
    for (const QJsonValue &value : qa.value(QStringLiteral("skipped")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString why = entry.value(QStringLiteral("why")).toString();
        parts << (why.isEmpty() ? QStringLiteral("%1 skipped").arg(entryLabel(entry))
                                : QStringLiteral("%1 skipped: %2").arg(entryLabel(entry), why));
    }
    return parts.join(separator);
}

QString verifyTask(const QString &id, const QString &title, const QString &verifier,
                   const QString &implementedBy, const QString &status, const QString &note)
{
    const QString ref = QStringLiteral("#") + id;
    // Which lane the card is in decides what the pass/fail moves are (#3XZV): from
    // needs-verification a pass goes on to QA and a failure goes back a stage; from a QA lane
    // a pass closes the card and a failure sends it back to be worked on again.
    const bool verifying = status == QStringLiteral("needs-verification");
    QStringList lines;
    lines << QStringLiteral("Verify %1: %2").arg(ref, title) << QString();
    QString who = QStringLiteral("The Switchboard card %1 is in a %2 and you are its "
                                 "verifier%3. ")
                      .arg(ref,
                           verifying ? QStringLiteral("verify lane (needs-verification)")
                                     : QStringLiteral("QA lane"),
                           verifier.trimmed().isEmpty()
                               ? QString()
                               : QStringLiteral(" (%1)").arg(verifier.trimmed()));
    who += implementedBy.trimmed().isEmpty()
               ? QStringLiteral("Somebody else implemented it; you check that work, you do not do it again.")
               : QStringLiteral("%1 implemented it; you check that work, you do not do it again.")
                     .arg(implementedBy.trimmed());
    lines << who << QString();
    // A preset runner gets the card attached (`ask {cards: [id]}`); a guest CLI is handed this
    // text and nothing else, so the brief has to say where the card lives as well.
    lines << QStringLiteral("The card is attached. If you cannot see it, its file is the one "
                            "`grep -rl '%1' issues/` finds, and its thread is "
                            "`issues/threads/%2.md`.").arg(ref, id)
          << QStringLiteral("- Read %1, its `## QA checklist` and the implementer's evidence under "
                            "`docs/qa_evidence/`.").arg(ref)
          << QStringLiteral("- Run every item of the `## QA checklist` yourself and write down what "
                            "you actually saw, not what should have happened.")
          << QStringLiteral("- Put your own evidence in the card's evidence directory "
                            "(`docs/qa_evidence/<date>-<slug>/`), in files whose names start with "
                            "`qa-`, beside the implementer's.")
          << QStringLiteral("- Write a `## Verdict` section on %1 with board_update_card: what "
                            "passed, what failed, and what you ran it on.").arg(ref)
          << (verifying
                  ? QStringLiteral("- Then move %1 with board_move_card: to `needs-qa-llm` when "
                                   "the checklist holds — your `## Verdict` is what carries it "
                                   "on to QA — or back to the stage the failure warrants "
                                   "(`executing`, or earlier: `planned`, `planning`, "
                                   "`discussing`) with the failures on the thread as a "
                                   "board_comment.").arg(ref)
                  : QStringLiteral("- Then move %1 with board_move_card: to `done` when the "
                                   "checklist holds, or back to `executing` (or an earlier "
                                   "stage) with the failures on the thread as a board_comment "
                                   "when it does not.").arg(ref))
          << QStringLiteral("- Commit your evidence with %1 in the message and the trailer "
                            "`Verified-By: <vendor>/<your exact model id>` on its own line at the "
                            "end: lower case, the vendor of the *model* and the id you are "
                            "actually running — `openai/gpt-6-astra`, `glm/glm-5.3`. If you "
                            "are Claude Code or Codex, append ` via claude-code` or ` via codex` "
                            "and still name the model (`anthropic/claude-opus-5 via claude-code`); "
                            "`anthropic/claude-code` or `openai/codex` alone is right only when "
                            "you cannot see which model you are.").arg(ref)
          << QStringLiteral("- Never fix the code yourself. Anything you find goes on %1's thread "
                            "as a board_comment, or into a new bug card — a verifier that edits "
                            "the code becomes its implementer, and the card would need verifying "
                            "again.").arg(ref)
          << QStringLiteral("- If you have no board_* tools (you are a guest CLI), write the "
                            "`## Verdict`, the status and the thread entry into the card's own "
                            "files, in the format the cards already there use — and, because "
                            "no worker is there to stamp it for you, write "
                            "`verified_by: <that same signature>` into the card's front matter "
                            "when you close it. Without it the board cannot say who checked %1.")
                 .arg(ref);
    if (!note.trimmed().isEmpty())
        lines << QString() << QStringLiteral("The owner adds, verbatim:") << note.trimmed();
    return lines.join(QLatin1Char('\n'));
}

QString sessionChip(const QString &token, bool live)
{
    if (token.isEmpty())
        return {};
    // The first eight characters are what every other surface shows of a session token (the
    // thread's "Executing (xxxxxxxx)", SessionInfo's copyable id), so the chip and the thread
    // entry read as the same pane.
    const QString id = QStringLiteral("⧉ ") + token.left(8);
    return live ? id : id + QStringLiteral(" closed");
}

QList<Badge> badges(const Card &card, bool showStatus, bool sessionLive)
{
    QList<Badge> out;
    if (showStatus && !card.status.isEmpty()) {
        // Only the part the column header does not already say ("Needs QA" → "LLM QA").
        static const QMap<QString, QString> shortNames{
            {QStringLiteral("needs-qa-llm"), QStringLiteral("LLM QA")},
            {QStringLiteral("needs-qa-human"), QStringLiteral("human QA")},
            {QStringLiteral("needs-review"), QStringLiteral("review")},
            {QStringLiteral("needs-labels"), QStringLiteral("labels")},
            {QStringLiteral("needs-ab"), QStringLiteral("A/B")},
            {QStringLiteral("dropped"), QStringLiteral("dropped")}};
        out << Badge{Badge::Status, shortNames.value(card.status, statusTitle(card.status))};
    }
    // Who checked it, on the row itself (#T71W): in the Verified section every row has one, so
    // the section header cannot say it and the badge must. A card the agent closed itself is
    // stamped with its own signature (#93WR) and wears no tick: nobody checked it, and the row
    // that folds it says so instead.
    if (!card.verifiedBy.isEmpty() && !selfClosed(card))
        out << Badge{Badge::Verified, QStringLiteral("✓ ") + signatureLabel(card.verifiedBy)};
    for (const QString &label : card.labels)
        out << Badge{Badge::Label, label};
    if (card.assignee == QStringLiteral("agent"))
        out << Badge{Badge::Agent, QStringLiteral("✦ agent")};
    else if (!card.assignee.isEmpty())
        out << Badge{Badge::Assignee, card.assignee};
    if (!card.waitingOn.isEmpty())
        out << Badge{Badge::Waiting, QStringLiteral("waiting: ") + card.waitingOn};
    if (card.tasksTotal > 0)
        out << Badge{card.tasksDone >= card.tasksTotal ? Badge::TasksDone : Badge::Tasks,
                     QStringLiteral("☑ %1/%2").arg(card.tasksDone).arg(card.tasksTotal)};
    if (card.threadEntries > 0)
        out << Badge{Badge::Thread, QStringLiteral("✎ %1").arg(card.threadEntries)};
    if (card.isPrivate)
        out << Badge{Badge::Private, QStringLiteral("private")};
    // Who has the card (#R9G7). Last, after the facts about the card itself: the claim is about
    // right now, and it is the thing a second agent scanning the list is looking for.
    if (!card.session.isEmpty())
        out << Badge{sessionLive ? Badge::Session : Badge::SessionClosed,
                     sessionChip(card.session, sessionLive)};
    return out;
}

int badgeDropOrder(Badge::Kind kind)
{
    switch (kind) {
    case Badge::Label:
        return 1;
    case Badge::Thread:
        return 2;
    case Badge::Tasks:
    case Badge::TasksDone:
        return 4;
    case Badge::Assignee:
        return 5;
    case Badge::Agent:
        return 6;
    case Badge::Private:
        return 7;
    // A pane that has gone is history; the pane still working on the card is not. The live chip
    // sits with `waiting:` at the top, because "somebody else is already on this" is why a row
    // is being read at all.
    case Badge::SessionClosed:
        return 3;
    case Badge::Session:
        return 9;
    case Badge::Status:
        return 8;
    // A verified row is in the Verified section precisely because of this badge; it goes with
    // the status, not before it.
    case Badge::Verified:
        return 8;
    case Badge::Waiting:
        return 9;
    }
    return 5;
}

QList<Badge> fitBadges(const QList<QPair<Badge, int>> &measured, int available, int gap)
{
    QList<int> kept;
    int width = 0;
    for (int i = 0; i < measured.size(); ++i) {
        kept << i;
        width += measured.at(i).second + (i > 0 ? gap : 0);
    }
    while (width > available && !kept.isEmpty()) {
        // The least important goes, and among equals the leftmost: with three labels the row
        // keeps the last one it can, which sits nearest the badges that earned their place.
        int worst = 0;
        for (int i = 1; i < kept.size(); ++i) {
            const int a = badgeDropOrder(measured.at(kept.at(i)).first.kind);
            const int b = badgeDropOrder(measured.at(kept.at(worst)).first.kind);
            if (a < b)
                worst = i;
        }
        width -= measured.at(kept.at(worst)).second + (kept.size() > 1 ? gap : 0);
        kept.removeAt(worst);
    }
    QList<Badge> out;
    for (int index : std::as_const(kept))
        out << measured.at(index).first;
    return out;
}

QString bodyWithoutTitle(const QString &body, const QString &title)
{
    // Leading blank lines are skipped; anything else before the heading means it is not a title.
    int start = 0;
    while (start < body.size() && (body.at(start) == QLatin1Char('\n') || body.at(start) == QLatin1Char('\r')))
        ++start;
    if (!QStringView(body).mid(start).startsWith(QLatin1String("# ")))
        return body;
    int end = body.indexOf(QLatin1Char('\n'), start);
    if (end < 0)
        end = body.size();
    const QString heading = body.mid(start + 2, end - start - 2).trimmed();
    if (heading.compare(title.trimmed(), Qt::CaseInsensitive) != 0)
        return body;
    while (end < body.size() && (body.at(end) == QLatin1Char('\n') || body.at(end) == QLatin1Char('\r')))
        ++end;
    return body.mid(end);
}

QString entryAge(const QString &entryId, const QDateTime &now)
{
    const QDateTime parsed = QDateTime::fromString(entryId.left(16), QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    if (!parsed.isValid())
        return QString();
    const QDateTime at(parsed.date(), parsed.time(), QTimeZone::utc());
    const qint64 seconds = at.secsTo(now);
    if (seconds < 60)
        return QStringLiteral("just now");
    if (seconds < 3600)
        return QStringLiteral("%1 min ago").arg(seconds / 60);
    const QDate day = at.toLocalTime().date(), today = now.toLocalTime().date();
    if (day == today)
        return QStringLiteral("%1 h ago").arg(seconds / 3600);
    if (day.addDays(1) == today)
        return QStringLiteral("yesterday");
    const QLocale c = QLocale::c();
    return day.year() == today.year() ? c.toString(day, QStringLiteral("MMM d"))
                                      : c.toString(day, QStringLiteral("MMM d yyyy"));
}

QPair<QString, QString> placement(const QStringList &order, const QString &moving, int slot)
{
    QStringList others = order;
    others.removeAll(moving);
    slot = qBound(0, slot, int(others.size()));
    return qMakePair(slot < others.size() ? others.at(slot) : QString(),
                     slot > 0 ? others.at(slot - 1) : QString());
}

QStringList cardsInSection(const QList<Row> &rows, const QString &columnId)
{
    QStringList out;
    for (const Row &row : rows)
        if (row.kind == Row::Card && row.columnId == columnId)
            out << row.cardId;
    return out;
}

QPair<QString, int> dropTarget(const QList<Row> &rows, int beforeRow)
{
    if (rows.isEmpty())
        return qMakePair(QString(), 0);
    beforeRow = qBound(0, beforeRow, int(rows.size()));
    // Walk back to the header that owns this point. Above the very first header there is nothing
    // to own it, so the drop falls into the first section.
    int header = -1;
    for (int i = beforeRow - 1; i >= 0; --i) {
        if (rows.at(i).kind == Row::Section) {
            header = i;
            break;
        }
    }
    if (header < 0) {
        for (int i = 0; i < rows.size(); ++i)
            if (rows.at(i).kind == Row::Section)
                return qMakePair(rows.at(i).columnId, 0);
        return qMakePair(QString(), 0);
    }
    int slot = 0;
    for (int i = header + 1; i < beforeRow; ++i)
        if (rows.at(i).kind == Row::Card)
            ++slot;
    return qMakePair(rows.at(header).columnId, slot);
}

bool selectableRow(const Row &row)
{
    // Everything but a section header. A fold row (#93WR) and a signal row or its toggles (#AQ6X)
    // are rows the keyboard stands on — Enter and ←/→ work on them — even though none of them is
    // a card.
    return row.kind != Row::Section;
}

int stepRow(const QList<Row> &rows, int from, int delta)
{
    if (delta == 0)
        return from;
    for (int i = from + delta; i >= 0 && i < rows.size(); i += delta)
        if (selectableRow(rows.at(i)))
            return i;
    return -1;
}

int rowOfCard(const QList<Row> &rows, const QString &cardId)
{
    if (cardId.isEmpty())
        return -1;
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == Row::Card && rows.at(i).cardId == cardId)
            return i;
    return -1;
}

int rowOfSection(const QList<Row> &rows, const QString &columnId)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == Row::Section && rows.at(i).columnId == columnId)
            return i;
    return -1;
}

int rowOfFold(const QList<Row> &rows, const QString &columnId)
{
    if (columnId.isEmpty())
        return -1;
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == Row::Fold && rows.at(i).columnId == columnId)
            return i;
    return -1;
}

int rowOfSignal(const QList<Row> &rows, const QString &key)
{
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == Row::Signal && rows.at(i).signalKey == key)
            return i;
    return -1;
}

int rowOfSignalFold(const QList<Row> &rows)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == Row::SignalFold)
            return i;
    return -1;
}

int rowOfDismissedFold(const QList<Row> &rows)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == Row::DismissedFold)
            return i;
    return -1;
}

Card Card::fromJson(const QJsonObject &object)
{
    Card card;
    card.id = object.value(QStringLiteral("id")).toString();
    card.title = object.value(QStringLiteral("title")).toString();
    card.type = object.value(QStringLiteral("type")).toString(QStringLiteral("work"));
    card.status = object.value(QStringLiteral("status")).toString();
    card.section = object.value(QStringLiteral("section")).toString();
    card.tab = object.value(QStringLiteral("tab")).toString();
    card.assignee = object.value(QStringLiteral("assignee")).toString();
    card.waitingOn = object.value(QStringLiteral("waiting_on")).toString();
    card.rank = object.value(QStringLiteral("rank")).toString();
    card.path = object.value(QStringLiteral("path")).toString();
    card.implementedBy = object.value(QStringLiteral("implemented_by")).toString();
    // Which pane holds the card (#R9G7): `board_claim` writes the claiming pane's session token
    // into the front matter, and the row carries it so the list can show who is on it.
    card.session = object.value(QStringLiteral("session")).toString();
    card.verifiedBy = object.value(QStringLiteral("verified_by")).toString();
    card.text = object.value(QStringLiteral("text")).toString();
    card.milestone = object.value(QStringLiteral("milestone")).toString();
    card.created = object.value(QStringLiteral("created")).toString();
    card.updated = object.value(QStringLiteral("updated")).toString();
    card.topic = object.value(QStringLiteral("topic")).toString();
    card.labels = stringList(object.value(QStringLiteral("labels")));
    card.priority = qBound(-1, object.value(QStringLiteral("priority")).toInt(), 3);
    card.threadEntries = object.value(QStringLiteral("thread_entries")).toInt();
    card.tasksDone = object.value(QStringLiteral("tasks_done")).toInt();
    card.tasksTotal = object.value(QStringLiteral("tasks_total")).toInt();
    card.isPrivate = object.value(QStringLiteral("private")).toBool();
    return card;
}

bool Card::closed() const
{
    return status == QStringLiteral("done") || status == QStringLiteral("dropped");
}

// The agent closed its own card (#93WR): done, stamped, and stamped by whoever implemented it.
// The comparison is on the trimmed signature — the worker writes the same string into both
// fields, and a stray space is not a second model.
bool selfClosed(const Card &card)
{
    const QString verified = card.verifiedBy.trimmed();
    return card.status == QStringLiteral("done") && !verified.isEmpty()
           && verified == card.implementedBy.trimmed();
}

QString selfClosedTitle(int count)
{
    return count == 1 ? QStringLiteral("1 closed by the agent")
                      : QStringLiteral("%1 closed by the agent").arg(count);
}

bool Card::parked() const
{
    return status == QStringLiteral("deferred");
}

QString Card::folder() const
{
    // `.switchboard/changes/2026-09-17-x.md`, or `.switchboard/.private/changes/…` for a private
    // card. Every spelling of the board folder is stripped — `projects::boardFolders()` is the one
    // list — and so is `.private/`, which is why a leading dotted part goes too. A board filed
    // before 2026-09-19 keeps its folder's older name, `switchboard/` or `issues/` (#JN7X).
    const QStringList folders = relay::projects::boardFolders();
    QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    while (!parts.isEmpty()
           && (folders.contains(parts.first()) || parts.first().startsWith(QLatin1Char('.'))))
        parts.removeFirst();
    return parts.size() > 1 ? parts.first() : QString();
}

// --------------------------------------------------------------------------- config

void Model::setConfig(const QJsonObject &config)
{
    m_tabs.clear();
    m_columns.clear();
    m_columnStatuses.clear();

    const QJsonObject statuses = config.value(QStringLiteral("column_statuses")).toObject();
    for (auto it = statuses.begin(); it != statuses.end(); ++it)
        m_columnStatuses.insert(it.key(), stringList(it.value()));

    // What this board calls its sections (`column_titles:`), over ids that do not change. It may
    // name a section that is not in `columns:` — a plan's Draft, Deferred, Verified, Done — since
    // those exist whenever a card has that status.
    m_columnTitles.clear();
    const QJsonObject titles = config.value(QStringLiteral("column_titles")).toObject();
    for (auto it = titles.begin(); it != titles.end(); ++it) {
        const QString title = it.value().toString().trimmed();
        if (!title.isEmpty())
            m_columnTitles.insert(it.key(), title);
    }
    m_statusChoices = stringList(config.value(QStringLiteral("all_statuses")));

    m_columns = stringList(config.value(QStringLiteral("columns")));
    if (m_columns.isEmpty())
        m_columns = QStringList{QStringLiteral("inbox"), QStringLiteral("discussing"),
                                QStringLiteral("planning"), QStringLiteral("planned"),
                                QStringLiteral("executing"), QStringLiteral("needs-verification"),
                                QStringLiteral("needs-qa"), QStringLiteral("done")};

    const QJsonArray tabs = config.value(QStringLiteral("tabs")).toArray();
    bool haveMemory = false;
    for (const QJsonValue &value : tabs) {
        const QJsonObject item = value.toObject();
        Tab tab;
        tab.id = item.value(QStringLiteral("id")).toString();
        if (tab.id.isEmpty())
            continue;
        tab.folder = item.value(QStringLiteral("folder")).toString();
        tab.filter = item.value(QStringLiteral("filter")).toString();
        tab.title = item.value(QStringLiteral("title")).toString(tabTitle(tab.id));
        // Memory is a card type with its own statuses and view, not work columns
        // (TASKS-AND-MEMORY-DESIGN section 9, "One object model"). `planning` used to be
        // tagged the same way, for a `plan` card type that #X7NB dropped: it is an ordinary
        // work-card folder, and the cards in it are `type: work`.
        if (tab.folder == QStringLiteral("memory"))
            tab.type = QStringLiteral("memory");
        haveMemory = haveMemory || tab.type == QStringLiteral("memory");
        m_tabs << tab;
    }
    if (!haveMemory) {
        // Memory cards exist in the format whether or not board.yaml lists a tab for them.
        Tab memory;
        memory.id = QStringLiteral("memory");
        memory.title = QStringLiteral("Memory");
        memory.folder = QStringLiteral("memory");
        memory.type = QStringLiteral("memory");
        m_tabs << memory;
    }
}

QString doneSection()
{
    return QStringLiteral("done");
}

QString verifiedSection()
{
    return QStringLiteral("verified");
}

namespace {

// Every section id, manual ones included: what a parked card's `section:` may name.
QSet<QString> sectionIdsOf(const QList<Column> &sections)
{
    QSet<QString> out;
    for (const Column &column : sections)
        out.insert(column.id);
    return out;
}

// Which section a card belongs in, given a status -> section index built once by the caller.
// Verified is the one section a status does not name: it is `done` plus a signature. A manual
// `section:` (#3XZV) wins over the status for as long as that column exists, so a card parked by
// hand stays put while its stage moves underneath it; a removed section stops being in `ids`
// and the card falls back to its status section on its own.
QString sectionForCard(const Card &card, const QMap<QString, QString> &index,
                       const QSet<QString> &ids)
{
    // Verified is for a card another model checked. A card the agent closed itself carries a
    // `verified_by` too (#93WR) — its own signature — so it falls through to Done, where the fold
    // row puts it away.
    if (card.status == QStringLiteral("done") && !card.verifiedBy.trimmed().isEmpty()
        && !selfClosed(card))
        return verifiedSection();
    if (card.closed())
        return doneSection();
    if (!card.section.trimmed().isEmpty() && ids.contains(card.section.trimmed()))
        return card.section.trimmed();
    return index.value(card.status);
}

}  // namespace

const Tab *Model::tab(const QString &id) const
{
    for (const Tab &tab : m_tabs)
        if (tab.id == id)
            return &tab;
    return nullptr;
}

// The one list's sections. The configured columns come first in their configured order, then any
// status they do not collect — a plan's Draft, a memory's Active, a Deferred card — so that one
// list really does hold every card that is not closed. A column configured to collect nothing
// (`column_statuses: {research: []}`) is kept as a manual section (#3XZV): cards land in it by
// being parked there, not by their status. Done is always last.
QList<Column> Model::sections() const
{
    QList<Column> out;
    QSet<QString> collected;
    for (const QString &id : m_columns) {
        bool manual = false;
        QStringList statuses;
        if (m_columnStatuses.contains(id)) {
            statuses = m_columnStatuses.value(id);
            manual = statuses.isEmpty();   // an explicit []: a section filled by hand
        } else {
            statuses = fallbackStatuses(id);
        }
        statuses.removeAll(QStringLiteral("done"));
        statuses.removeAll(QStringLiteral("dropped"));
        if (statuses.isEmpty() && !manual)
            continue;                    // a configured Done column: it is the last section
        for (const QString &status : statuses)
            collected.insert(status);
        out << Column{id, sectionTitle(id), statuses};
    }
    QStringList extras;
    for (const Card &card : m_cards) {
        if (card.closed() || card.status.isEmpty() || collected.contains(card.status))
            continue;
        if (!extras.contains(card.status))
            extras << card.status;
    }
    std::sort(extras.begin(), extras.end(), [](const QString &a, const QString &b) {
        const int ra = extraStatusRank(a), rb = extraStatusRank(b);
        return ra != rb ? ra < rb : a < b;
    });
    for (const QString &status : std::as_const(extras))
        out << Column{status, sectionTitle(status), {status}};
    // Verified, then Done. It carries no statuses on purpose: `sectionIndex` must not learn that
    // "done" lives here, or Done would collect nothing, and `dropStatus` must answer nothing, so
    // no drop and no quick add can land in a section a card can only be *closed* into.
    out << Column{verifiedSection(), sectionTitle(verifiedSection()), {}};
    out << Column{doneSection(), sectionTitle(doneSection()),
                  {QStringLiteral("done"), QStringLiteral("dropped")}};
    return out;
}

// What this section is called: the board's own name for it when `column_titles:` gives one,
// and Relay's wording otherwise. Renaming a section is a display name over an id that does not
// change, so nothing that reads a status or a column id goes through here.
QString Model::sectionTitle(const QString &id) const
{
    const QString own = m_columnTitles.value(id);
    if (!own.isEmpty())
        return own;
    return id == verifiedSection() ? QStringLiteral("Verified") : statusTitle(id);
}

QStringList Model::statusChoices() const
{
    if (!m_statusChoices.isEmpty())
        return m_statusChoices;
    // An older worker sends no `all_statuses`: fall back to the work statuses, which are the
    // ones a section on this board can collect anyway.
    return {QStringLiteral("inbox"), QStringLiteral("discussing"), QStringLiteral("planning"),
            QStringLiteral("planned"), QStringLiteral("ready"), QStringLiteral("executing"),
            QStringLiteral("in-progress"), QStringLiteral("needs-verification"),
            QStringLiteral("needs-review"),
            QStringLiteral("needs-labels"), QStringLiteral("needs-ab"),
            QStringLiteral("needs-qa-llm"), QStringLiteral("needs-qa-human"),
            QStringLiteral("deferred"), QStringLiteral("done"), QStringLiteral("dropped")};
}

QString Model::dropStatus(const QString &columnId) const
{
    const QList<Column> list = sections();
    for (const Column &column : list)
        if (column.id == columnId)
            return column.statuses.value(0);
    return QString();
}

QMap<QString, QString> Model::sectionIndex(const QList<Column> &sections) const
{
    QMap<QString, QString> out;
    for (const Column &column : sections)
        for (const QString &status : column.statuses)
            if (!out.contains(status))
                out.insert(status, column.id);
    return out;
}

QString Model::sectionOf(const Card &card) const
{
    const QList<Column> list = sections();
    return sectionForCard(card, sectionIndex(list), sectionIdsOf(list));
}

// ---------------------------------------------------------------------------- cards

void Model::reset(const QJsonArray &cards)
{
    m_cards.clear();
    upsert(cards);
}

void Model::upsert(const QJsonArray &cards)
{
    for (const QJsonValue &value : cards)
        upsert(Card::fromJson(value.toObject()));
}

void Model::upsert(const Card &card)
{
    if (!card.id.isEmpty())
        m_cards.insert(card.id, card);
}

void Model::remove(const QStringList &ids)
{
    for (const QString &id : ids)
        m_cards.remove(id);
}

void Model::clear()
{
    m_cards.clear();
}

const Card *Model::card(const QString &id) const
{
    auto it = m_cards.constFind(id.toUpper());
    return it == m_cards.constEnd() ? nullptr : &it.value();
}

namespace {

// The time a sort keys on: the two updated sorts want the card's own `updated` (falling back to
// `created` when the worker sent none, so an old worker still gets a sensible order — the two
// spellings compare lexicographically, a date sorting before that day's timestamps); the created
// sorts and the title ones want `created` and the title.
QString sortTime(const Card &card, Sort sort)
{
    if ((sort == Sort::RecentlyUpdated || sort == Sort::OldestUpdated) && !card.updated.isEmpty())
        return card.updated;
    return card.created;
}

// Where `a` goes relative to `b` under one column sort: -1 before, 1 after, 0 when the two are
// equal on that key — and then the board's own rank breaks the tie, so an order never wobbles
// between rebuilds.
int sortCompare(const Card &a, const Card &b, Sort sort)
{
    if (sort == Sort::PriorityHigh || sort == Sort::PriorityLow) {
        if (a.priority == b.priority)
            return 0;
        return (a.priority > b.priority) == (sort == Sort::PriorityHigh) ? -1 : 1;
    }
    if (sort == Sort::TitleAsc || sort == Sort::TitleDesc) {
        const int byTitle = QString::compare(a.title, b.title, Qt::CaseInsensitive);
        if (byTitle == 0)
            return 0;
        return sort == Sort::TitleAsc ? byTitle : -byTitle;
    }
    const QString at = sortTime(a, sort), bt = sortTime(b, sort);
    if (at == bt)
        return 0;
    return (at < bt) == sortAscending(sort) ? -1 : 1;
}

}  // namespace

QList<Card> Model::sorted(QList<Card> cards, bool closedSection) const
{
    std::sort(cards.begin(), cards.end(), [this, closedSection](const Card &a, const Card &b) {
        // A column sort is the whole order, inside every section alike; Manual is the board's own
        // rank, with the closed sections newest first as they always have been.
        if (m_sort != Sort::Manual) {
            const int byColumn = sortCompare(a, b, m_sort);
            if (byColumn != 0)
                return byColumn < 0;
        } else if (closedSection && a.created != b.created) {
            return a.created > b.created;
        }
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return a.path < b.path;
    });
    return cards;
}

QList<Card> Model::cards(const QString &columnId) const
{
    const QList<Column> list = sections();
    const QMap<QString, QString> index = sectionIndex(list);
    const QSet<QString> ids = sectionIdsOf(list);
    QList<Card> out;
    for (const Card &card : m_cards) {
        const QString section = sectionForCard(card, index, ids);
        if (section != columnId || section.isEmpty())
            continue;
        if (!shown(card))
            continue;
        out << card;
    }
    return sorted(out, columnId == doneSection() || columnId == verifiedSection());
}

int Model::openCount() const
{
    int total = 0;
    for (const Card &card : m_cards)
        if (!card.closed() && shown(card))
            ++total;
    return total;
}

// Open cards a section checkbox is keeping out of the list. Only open ones, so that the count
// label's "62 of 84 open" subtracts exactly: closed cards are not in the 84 either.
int Model::hiddenCount(const QSet<QString> &hidden) const
{
    if (hidden.isEmpty())
        return 0;
    const QMap<QString, QString> index = sectionIndex(sections());
    const QSet<QString> ids = sectionIdsOf(sections());
    int total = 0;
    for (const Card &card : m_cards) {
        if (card.closed() || !shown(card))
            continue;
        const QString section = sectionForCard(card, index, ids);
        if (!section.isEmpty() && hidden.contains(section))
            ++total;
    }
    return total;
}

QList<Row> Model::rows(const QSet<QString> &collapsed, const QSet<QString> &hidden,
                       const QSet<QString> &selfClosedOpen) const
{
    const bool filtered = !m_filter.trimmed().isEmpty() || !m_labelFilter.isEmpty();
    const QList<Column> list = sections();
    const QMap<QString, QString> index = sectionIndex(list);
    const QSet<QString> ids = sectionIdsOf(list);
    QMap<QString, QList<Card>> grouped;
    for (const Card &card : m_cards) {
        if (!shown(card))
            continue;
        const QString section = sectionForCard(card, index, ids);
        if (section.isEmpty())
            continue;      // a status no section collects and that is not closed: nothing to show
        grouped[section] << card;
    }
    QList<Row> out;
    for (const Column &column : list) {
        if (hidden.contains(column.id))
            continue;      // its checkbox is unticked: the section is not on the page at all
        const QList<Card> cards = sorted(grouped.value(column.id),
                                         column.id == doneSection() || column.id == verifiedSection());
        if (filtered && cards.isEmpty())
            continue;      // a section with nothing to show gets out of the way
        Row header;
        header.kind = Row::Section;
        header.columnId = column.id;
        header.title = column.title;
        header.count = int(cards.size());
        // Nothing is folded while a filter is active: a search that hid its own matches would be
        // a search that does nothing.
        header.collapsed = !filtered && collapsed.contains(column.id);
        out << header;
        if (header.collapsed)
            continue;
        // A section that collects several statuses (Waiting, Needs QA, Done) names each card's
        // exact one; the one whose status is the section's own repeats nothing.
        const bool multi = column.statuses.size() > 1;
        const auto cardRow = [&column, multi](const Card &card) {
            Row row;
            row.kind = Row::Card;
            row.columnId = column.id;
            row.cardId = card.id;
            row.showStatus = multi && card.status != column.id;
            return row;
        };
        // The cards the agent closed itself come out of the run of rows and stand behind one fold
        // row at the end of the section (#93WR). The section header's count still holds them: it
        // says how many cards the section has, and folding is presentation.
        QList<Card> folded;
        for (const Card &card : cards) {
            if (!filtered && selfClosed(card)) {
                folded << card;
                continue;
            }
            out << cardRow(card);
        }
        if (folded.isEmpty())
            continue;
        Row fold;
        fold.kind = Row::Fold;
        fold.columnId = column.id;
        fold.count = int(folded.size());
        fold.title = selfClosedTitle(fold.count);
        fold.collapsed = !selfClosedOpen.contains(column.id);
        out << fold;
        if (!fold.collapsed)
            for (const Card &card : folded)
                out << cardRow(card);
    }
    return out;
}

QStringList Model::allLabels() const
{
    QStringList out;
    for (const Card &card : m_cards)
        for (const QString &label : card.labels)
            if (!out.contains(label))
                out << label;
    out.sort();
    return out;
}

QStringList Model::allIds() const
{
    QStringList out = m_cards.keys();
    out.sort();
    return out;
}

// ------------------------------------------------------------------------ filtering

void Model::setFilter(const QString &text)
{
    m_filter = text.trimmed();
    m_plainTerms = plainTerms(m_filter).join(QLatin1Char(' '));
}

// Which of a filter's terms the worker answers. Everything with a scope on it — `label:`,
// `status:`, `waiting:`, `folder:`, `@name`, `#ID` — is decided here from the row, which is why
// a `status:` term has always been free; the plain words are the ones that used to scan every
// card's text on the GUI thread, 30–80 ms a keystroke (#7M6E).
QStringList Model::plainTerms(const QString &filter)
{
    QStringList out;
    const QStringList terms = filter.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &raw : terms) {
        const QString term = raw.trimmed();
        if (term.isEmpty() || term.startsWith(QLatin1Char('@')) || term.startsWith(QLatin1Char('#'))
            || term.startsWith(QStringLiteral("label:")) || term.startsWith(QStringLiteral("status:"))
            || term.startsWith(QStringLiteral("waiting:")) || term.startsWith(QStringLiteral("folder:")))
            continue;
        out << term;
    }
    return out;
}

void Model::setSearchResult(const QString &terms, const QSet<QString> &ids)
{
    m_searchTerms = terms;
    m_searchIds = ids;
}

// The text filter and the label chips ask their questions together: this is the one gate every
// counting or listing query passes through, so a card the chips rule out is as absent as one the
// words did (#VKFV).
bool Model::shown(const Card &card) const
{
    if (!m_labelFilter.isEmpty()) {
        for (const QString &label : m_labelFilter)
            if (!containsCaseless(card.labels, label))
                return false;
    }
    // The worker's answer only while it answers the words that are in the box: one more keystroke
    // and it is a query out of date, and the fields decide until the next one lands (#7M6E).
    const bool current = !m_plainTerms.isEmpty() && m_searchTerms == m_plainTerms;
    return matches(card, m_filter, current ? &m_searchIds : nullptr);
}

bool Model::matches(const Card &card, const QString &filter, const QSet<QString> *textMatch)
{
    const QString trimmed = filter.trimmed();
    if (trimmed.isEmpty())
        return true;
    // Every plain word is the worker's to answer when it has answered them (see the header): all
    // of them or none, because its id set is the conjunction, not one term's.
    bool plain = false;
    const QStringList terms = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &raw : terms) {
        const QString term = raw.trimmed();
        if (term.isEmpty())
            continue;
        if (term.startsWith(QStringLiteral("label:"))) {
            if (!containsCaseless(card.labels, term.mid(6)))
                return false;
        } else if (term.startsWith(QStringLiteral("status:"))) {
            if (card.status.compare(term.mid(7), Qt::CaseInsensitive) != 0)
                return false;
        } else if (term.startsWith(QStringLiteral("waiting:"))) {
            const QString who = term.mid(8);
            const QString wanted = who == QStringLiteral("me") ? QStringLiteral("owner") : who;
            if (card.waitingOn.compare(wanted, Qt::CaseInsensitive) != 0)
                return false;
        } else if (term.startsWith(QStringLiteral("folder:"))) {
            // The folder on disk, or the board.yaml tab id that names it: `folder:changes` and
            // `folder:bugs` both reach the same cards.
            const QString want = term.mid(7);
            if (card.folder().compare(want, Qt::CaseInsensitive) != 0
                && card.tab.compare(want, Qt::CaseInsensitive) != 0)
                return false;
        } else if (term.startsWith(QLatin1Char('@'))) {
            if (card.assignee.compare(term.mid(1), Qt::CaseInsensitive) != 0)
                return false;
        } else if (term.startsWith(QLatin1Char('#'))) {
            if (card.id.compare(term.mid(1), Qt::CaseInsensitive) != 0)
                return false;
        } else if (textMatch) {
            plain = true;   // the worker is answering this word, with all the others
        } else {
            const QString haystack = card.id + QLatin1Char(' ') + card.title + QLatin1Char(' ')
                                     + card.labels.join(QLatin1Char(' ')) + QLatin1Char(' ')
                                     + card.assignee + QLatin1Char(' ') + card.milestone;
            // Full text: a plain word also searches the card's whole body and thread (the
            // worker's `text` on the row), not only the row's own fields. Checked apart so a
            // long card's text is never copied into the haystack on every term.
            if (!haystack.contains(term, Qt::CaseInsensitive)
                && !card.text.contains(term, Qt::CaseInsensitive))
                return false;
        }
    }
    return !plain || textMatch->contains(card.id);
}

int Model::score(const QString &query, const Card &card)
{
    if (query.isEmpty())
        return card.closed() ? 1 : 100;
    if (card.id.compare(query, Qt::CaseInsensitive) == 0)
        return 100000;
    const int title = fuzzy(query, card.title);
    const int id = fuzzy(query, card.id);
    if (title == 0 && id == 0)
        return 0;
    // Open cards first (design section 5): a closed card has to earn its place.
    return title * 3 + id * 4 + (card.closed() ? 0 : 500);
}

QList<Card> Model::search(const QString &query, int limit) const
{
    QList<QPair<int, Card>> scored;
    for (const Card &card : m_cards) {
        const int points = score(query, card);
        if (points > 0)
            scored << qMakePair(points, card);
    }
    std::sort(scored.begin(), scored.end(), [](const QPair<int, Card> &a, const QPair<int, Card> &b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second.id < b.second.id;
    });
    QList<Card> out;
    for (const auto &item : scored) {
        if (out.size() >= limit)
            break;
        out << item.second;
    }
    return out;
}

}  // namespace board
}  // namespace relay
