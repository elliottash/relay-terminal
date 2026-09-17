// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
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

    static QColor colorFor(Destination destination);

protected:
    void highlightBlock(const QString &text) override;

private:
    void highlightShell(const QString &text);
    void highlightAgent(const QString &text);

    Destination m_destination = Destination::Auto;
    QSet<QString> m_known;
};

}  // namespace relay
