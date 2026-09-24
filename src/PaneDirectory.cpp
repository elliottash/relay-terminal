// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PaneDirectory.h"

#include "Logging.h"
#include "PaneAddress.h"

#include <algorithm>

namespace relay::panedir {

namespace {
constexpr int kMaxBytes = 16 * 1024;

QString call(const std::function<QString()> &fn) { return fn ? fn() : QString(); }
bool call(const std::function<bool()> &fn) { return fn && fn(); }
}   // namespace

QJsonObject Result::toJson() const {
    QJsonObject out{{"ok", ok}, {"message", message}, {"panes", panes}};
    if (!code.isEmpty()) out.insert(QStringLiteral("code"), code);
    if (!outcome.isEmpty()) out.insert(QStringLiteral("outcome"), outcome);
    if (to > 0) {
        out.insert(QStringLiteral("to"), paneaddress::label(to));
        out.insert(QStringLiteral("to_title"), toTitle);
    }
    return out;
}

Directory &Directory::instance() {
    static Directory directory;
    return directory;
}

int Directory::add(const QString &token, PaneHooks hooks) {
    if (auto it = m_panes.find(token); it != m_panes.end()) {
        it->hooks = std::move(hooks);
        return it->handle;
    }
    Entry entry;
    entry.handle = paneaddress::mint();
    entry.hooks = std::move(hooks);
    m_panes.insert(token, entry);
    m_byHandle.insert(entry.handle, token);
    return entry.handle;
}

void Directory::remove(const QString &token) {
    const auto it = m_panes.find(token);
    if (it == m_panes.end()) return;
    m_retired.insert(it->handle);
    m_byHandle.remove(it->handle);
    m_panes.erase(it);
    m_idleWatchers.remove(token);
    for (auto &watchers : m_idleWatchers)
        watchers.erase(std::remove_if(watchers.begin(), watchers.end(),
                                      [&](const Subscription &s) { return s.watcher == token; }),
                       watchers.end());
}

int Directory::handleOf(const QString &token) const {
    const auto it = m_panes.constFind(token);
    return it == m_panes.constEnd() ? 0 : it->handle;
}

QString Directory::tokenOf(int handle) const { return m_byHandle.value(handle); }

QStringList Directory::tokens() const { return m_panes.keys(); }

QJsonObject Directory::row(const Entry &entry) const {
    const QString state = call(entry.hooks.guestInFront) ? QStringLiteral("guest")
                        : !call(entry.hooks.configured) ? QStringLiteral("no agent")
                        : call(entry.hooks.busy)        ? QStringLiteral("busy")
                                                        : QStringLiteral("idle");
    return QJsonObject{{"pane", paneaddress::label(entry.handle)},
                       {"title", call(entry.hooks.title)},
                       {"workspace", call(entry.hooks.workspace)},
                       {"state", state}};
}

QJsonArray Directory::roster(const QString &exceptToken) const {
    QList<int> handles = m_byHandle.keys();
    std::sort(handles.begin(), handles.end());
    QJsonArray out;
    for (const int handle : handles) {
        const QString token = m_byHandle.value(handle);
        if (token == exceptToken) continue;
        out.append(row(m_panes.value(token)));
    }
    return out;
}

QString Directory::printable(const QString &text) {
    QString out;
    out.reserve(qMin(int(text.size()), kMaxBytes));
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        if (u == '\n' || u == '\t') { out.append(c); continue; }
        if (u == '\r') continue;
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) continue;
        out.append(c);
    }
    while (out.toUtf8().size() > kMaxBytes) out.chop(qMax(1, int(out.size()) / 16));
    return out;
}

void Directory::logSend(const QString &from, const QString &fromWs, const QString &to, const QString &toWs,
                        int bytes, const QString &outcome) {
    // Identifiers only: relay::log never carries prompt text (src/Logging.h), and the message is
    // one. The workspaces are here because they are the one thing no session file can recover
    // once both panes have closed.
    relay::log::info(QStringLiteral("crosspane_send from=%1 to=%2 from_ws=%3 to_ws=%4 bytes=%5 outcome=%6")
                         .arg(from.left(12), to.isEmpty() ? QStringLiteral("-") : to.left(12),
                              fromWs.isEmpty() ? QStringLiteral("-") : fromWs,
                              toWs.isEmpty() ? QStringLiteral("-") : toWs)
                         .arg(bytes)
                         .arg(outcome));
}

Result Directory::send(const QString &fromToken, const QString &to, const QString &text, bool notifyWhenIdle,
                       bool mayWake) {
    Result result;
    result.panes = roster(fromToken);
    const Entry sender = m_panes.value(fromToken);
    const QString fromWs = call(sender.hooks.workspace);
    const QString body = printable(text);
    const int bytes = int(body.toUtf8().size());
    auto refuse = [&](const QString &code, const QString &message, const QString &toToken = QString(),
                      const QString &toWs = QString()) {
        result.ok = false;
        result.code = code;
        result.message = message;
        logSend(fromToken, fromWs, toToken, toWs, bytes, code);
        return result;
    };
    if (!m_enabled)
        return refuse(kDisabled, QStringLiteral("Cross-pane messaging is turned off in this Relay, so nothing was sent."));
    if (sender.handle == 0)
        return refuse(kNotSupported, QStringLiteral("This pane cannot send messages to other panes."));
    const int handle = paneaddress::parse(to);
    if (handle == 0 || (!m_byHandle.contains(handle) && !m_retired.contains(handle)))
        return refuse(kUnknownPane, QStringLiteral("There is no pane %1 in this Relay. The panes are listed in `panes`.")
                                        .arg(to.trimmed().left(40)));
    if (m_retired.contains(handle))
        return refuse(kClosed, QStringLiteral("Pane %1 has been closed, so nothing was sent. Pane numbers are never "
                                              "reused; the open panes are listed in `panes`.")
                                   .arg(paneaddress::label(handle)));
    if (handle == sender.handle)
        return refuse(kSelf, QStringLiteral("%1 is this pane. Send to another one.").arg(paneaddress::label(handle)));
    const QString toToken = m_byHandle.value(handle);
    const Entry recipient = m_panes.value(toToken);
    const QString toWs = call(recipient.hooks.workspace);
    result.to = handle;
    result.toTitle = call(recipient.hooks.title);
    if (body.trimmed().isEmpty())
        return refuse(kEmpty, QStringLiteral("The message is empty, so nothing was sent."), toToken, toWs);
    if (call(recipient.hooks.guestInFront))
        return refuse(kNotSupported, QStringLiteral("Pane %1 runs a Claude Code or Codex guest, which cannot take a "
                                                    "message from another pane yet.")
                                         .arg(paneaddress::label(handle)),
                      toToken, toWs);
    if (!call(recipient.hooks.configured) || !recipient.hooks.deliver)
        return refuse(kNotConfigured, QStringLiteral("Pane %1 has no agent set up, so there is nobody to read a message.")
                                          .arg(paneaddress::label(handle)),
                      toToken, toWs);
    Note note;
    note.from = sender.handle;
    note.fromToken = fromToken;
    note.fromTitle = call(sender.hooks.title);
    note.fromWorkspace = fromWs;
    note.text = body;
    note.mayWake = mayWake;
    const QString outcome = recipient.hooks.deliver(note);
    result.ok = true;
    result.outcome = outcome;
    const QString who = QStringLiteral("pane %1").arg(paneaddress::label(handle));
    if (outcome == kWoke)
        result.message = QStringLiteral("Delivered to %1, which was idle: it has started a turn to read it.").arg(who);
    else if (outcome == kNoWake)
        result.message = QStringLiteral("Delivered to %1 as a note, without waking it (%2): it reads the note when "
                                        "its next turn starts.")
                             .arg(who, mayWake ? QStringLiteral("it has been woken too many times since its user last "
                                                                "typed there")
                                               : QStringLiteral("a turn that was itself started by a message cannot "
                                                                "wake another pane"));
    else
        result.message = QStringLiteral("Delivered to %1, which is busy: it reads the message at its next step.").arg(who);
    result.message += QStringLiteral(" Delivered is not read, and silence is not agreement.");
    if (notifyWhenIdle) {
        auto &watchers = m_idleWatchers[toToken];
        const bool already = std::any_of(watchers.cbegin(), watchers.cend(),
                                         [&](const Subscription &s) { return s.watcher == fromToken; });
        if (!already) watchers.append({fromToken, mayWake});
        result.message += QStringLiteral(" You will get one note when %1 next goes idle; do not poll it.").arg(who);
    }
    logSend(fromToken, fromWs, toToken, toWs, bytes, outcome);
    return result;
}

void Directory::wentIdle(const QString &token) {
    const QList<Subscription> watchers = m_idleWatchers.take(token);
    if (watchers.isEmpty()) return;
    const Entry subject = m_panes.value(token);
    if (subject.handle == 0) return;
    for (const Subscription &sub : watchers) {
        const Entry watcher = m_panes.value(sub.watcher);
        if (watcher.handle == 0 || !watcher.hooks.deliver) continue;
        Note note;
        note.from = subject.handle;
        note.fromToken = token;
        note.fromTitle = call(subject.hooks.title);
        note.fromWorkspace = call(subject.hooks.workspace);
        note.idle = true;
        note.mayWake = sub.mayWake;
        note.text = QStringLiteral("Pane %1 is idle now.").arg(paneaddress::label(subject.handle));
        const QString outcome = watcher.hooks.deliver(note);
        logSend(token, note.fromWorkspace, sub.watcher, call(watcher.hooks.workspace), 0,
                QStringLiteral("idle_notice_") + outcome);
    }
}

void Directory::resetForTest() {
    m_panes.clear();
    m_byHandle.clear();
    m_retired.clear();
    m_idleWatchers.clear();
    m_enabled = true;
}

} // namespace relay::panedir
