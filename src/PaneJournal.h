// SPDX-License-Identifier: AGPL-3.0-or-later
// The pane's end of its text journal (card #HEY7, src/TextJournal.h): the rows the terminal lets
// go of for good arrive here as the engine hands them over, and go into the journal named by the
// pane's `scrollback` id. Header-only; one per Pane.
#pragma once

#include "TerminalBackend.h"
#include "TextJournal.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

namespace relay {

class PaneJournal {
public:
    // `id` is read each time rows arrive, because a restore hands the pane its saved id after the
    // pane was built; `skip` names the restore chrome that is nobody's output.
    PaneJournal(QObject *owner, std::function<QString()> id, std::function<QString()> cwd,
                std::function<bool(const QString &)> skip)
        : m_id(std::move(id)), m_cwd(std::move(cwd)), m_skip(std::move(skip)), m_flushTimer(owner) {
        // Rows arrive with every burst of output. Appending them to the file costs a write and a
        // meta.json rename, so that happens at most every two seconds, and on save and close.
        m_flushTimer.setSingleShot(true);
        m_flushTimer.setInterval(2000);
        QObject::connect(&m_flushTimer, &QTimer::timeout, owner, [this] { flush(); });
    }
    ~PaneJournal() { close(nullptr); }
    PaneJournal(const PaneJournal &) = delete;
    PaneJournal &operator=(const PaneJournal &) = delete;

    void attach(TerminalBackend *backend) {
        if (!backend) return;
        backend->onHistoryEvicted = [this](const EvictedText &text) { take(text); };
        backend->setCollectEvicted(true);
    }

    // The conversation this pane's text belongs to from here on (card #HEY7), an empty id when
    // none does. While a conversation owns the pane its output is the transcript's, so the
    // journal holds a `{"c":…}` reference in place of its rows rather than a second copy of them.
    // The pane reports the same conversation on every change; only a real change reaches the
    // journal, and a conversation the writer has not met yet is written the next time rows
    // arrive (or on the flush a restore's absorb triggers).
    void setConversation(const QString &source, const QString &id, const QString &directory) {
        m_conversation = id;
        m_conversationSource = source;
        m_conversationDirectory = directory;
        if (m_writer) syncConversation(m_writer.get());
    }

    void take(const EvictedText &text) {
        textjournal::Writer *w = writer();
        if (!w) return;
        syncConversation(w);
        if (m_writtenConversation.isEmpty()) {
            int clear = 0;
            for (int i = 0; i < text.rows.size(); ++i) {
                while (clear < text.clears.size() && text.clears.at(clear) <= i) {
                    w->appendClear();
                    ++clear;
                }
                w->appendRow(text.rows.at(i), text.continuation.value(i), text.marks.value(i));
            }
            for (; clear < text.clears.size(); ++clear) w->appendClear();
        }
        if (!m_flushTimer.isActive()) m_flushTimer.start();
    }

    // Drain what the engine is holding, then write it: a save must not leave rows that are in
    // neither the tail it writes nor the journal.
    void flush(TerminalBackend *backend = nullptr) {
        if (backend) backend->drainEvictedRows();
        if (!m_writer) return;
        QString error;
        if (!m_writer->flush(&error) && !error.isEmpty())
            fprintf(stderr, "relay: could not append to this pane's text journal: %s\n", qPrintable(error));
    }

    // The pane is going: everything still held is written and the open segment sealed.
    void close(TerminalBackend *backend) {
        if (backend) backend->drainEvictedRows();
        m_flushTimer.stop();
        if (!m_writer) return;
        m_writer->flushPending();
        m_writer->seal();
        m_writer.reset();
    }

    // Rows a restore will not replay because the terminal could not hold them: they go straight to
    // the journal, where the replay would have pushed them anyway, oldest first.
    void absorbRows(const QStringList &rows) {
        textjournal::Writer *w = writer();
        if (!w || rows.isEmpty()) return;
        syncConversation(w);
        if (!m_writtenConversation.isEmpty()) return;   // the transcript holds these rows
        for (const QString &row : rows) w->appendRow(row, false, 0);
        w->flushPending();
        flush();
    }

    // How many lines the journal holds before the terminal's own: what the restore row offers.
    qint64 lines() const {
        if (m_writer) return m_writer->lines();
        return textjournal::lineCount(m_id());
    }

private:
    // Bring the journal's open-conversation marker in line with what the pane last reported:
    // `{"c":…}` when a conversation took over, `{"c":null}` when it handed the pane back.
    void syncConversation(textjournal::Writer *w) {
        if (!w || m_conversation == m_writtenConversation) return;
        w->appendConversation(m_conversationSource, m_conversation, m_conversationDirectory);
        m_writtenConversation = m_conversation;
    }

    textjournal::Writer *writer() {
        const QString id = m_id();
        if (m_writer && m_writer->id() != id) {
            m_writer->seal();
            m_writer.reset();
            m_writtenConversation.clear();   // a new journal has not met the conversation yet
        }
        if (!m_writer) {
            m_writer = std::make_unique<textjournal::Writer>(id);
            if (!m_writer->valid()) {
                m_writer.reset();
                return nullptr;
            }
            m_writer->setSkip(m_skip);
        }
        if (m_cwd) m_writer->setDirectory(m_cwd());
        return m_writer.get();
    }

    std::function<QString()> m_id;
    std::function<QString()> m_cwd;
    std::function<bool(const QString &)> m_skip;
    std::unique_ptr<textjournal::Writer> m_writer;
    QString m_conversation;          // the conversation the pane reported, "" for the shell
    QString m_conversationSource;
    QString m_conversationDirectory;
    QString m_writtenConversation;   // the conversation the journal's open segment holds
    QTimer m_flushTimer;
};

}  // namespace relay
