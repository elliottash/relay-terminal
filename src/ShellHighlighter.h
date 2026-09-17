// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QStringList>

namespace relay {

// Colours the prompt box. A line bound for the shell is highlighted like a command; a line bound
// for the agent stays plain, with only Relay's own tokens (@file, /command) tinted. The caret
// colour is set by the pane from the same decision, so the destination is visible while typing.
class InputHighlighter : public QSyntaxHighlighter {
public:
    enum class Destination { Auto, Shell, Agent };

    explicit InputHighlighter(QTextDocument *document);

    // The live routing decision; Auto leaves the text plain until the router answers.
    void setDestination(Destination destination);
    Destination destination() const { return m_destination; }
    // Command names Relay knows are installed; anything else is flagged as unknown.
    void setKnownCommands(const QStringList &commands);
    // Flag a command that does not resolve. Only in the chosen Terminal mode: in auto mode the
    // line may well be an agent request, and red on ordinary words is noise (owner, 2026-09-17).
    void setFlagUnknownCommands(bool flag);

    static QColor colorFor(Destination destination);

protected:
    void highlightBlock(const QString &text) override;

private:
    QColor commandColor(const QString &word) const;
    void highlightShell(const QString &text);
    void highlightAgent(const QString &text);

    Destination m_destination = Destination::Auto;
    QSet<QString> m_known;
    bool m_flagUnknown = false;
};

}  // namespace relay
