#!/usr/bin/env python3
"""Apply #SXF1's src/Pane.h edits: each replacement must match exactly once."""
import sys
from pathlib import Path

pane = Path("src/Pane.h")
text = pane.read_text()

edits = [
    # include, beside QueueSubmit.h
    ('#include "QueueSubmit.h"   // when a submitted agent prompt starts its turn at once (#N8VK)\n',
     '#include "QueueSubmit.h"   // when a submitted agent prompt starts its turn at once (#N8VK)\n'
     '#include "ContinueTurn.h"  // when Ctrl+Enter on an empty box continues a stopped turn (#SXF1)\n'),
    # routing: empty idle box continues a stopped turn
    ('''    // Ctrl+Enter: send to the agent. While the agent is busy, stop the current turn and send now.
    void interruptAgentWithPrompt() {
        if (sendSelectedSteerNow()) return;   // a selected steer row: that steer, now
        const QString text = m_editor->toPlainText().trimmed();
        if (!m_agentBusy) {''',
     '''    // Ctrl+Enter: send to the agent. While the agent is busy, stop the current turn and send now.
    // On an empty box with the agent idle, it continues a turn that stopped at its limit or was
    // cut off by a restart (#SXF1); every other empty box still asks for a prompt.
    void interruptAgentWithPrompt() {
        if (sendSelectedSteerNow()) return;   // a selected steer row: that steer, now
        const QString text = m_editor->toPlainText().trimmed();
        if (continueturn::sendNowContinues({m_agentBusy, text.isEmpty(), m_limitReached, m_turnCutOff})) {
            continueTurn();
            return;
        }
        if (!m_agentBusy) {'''),
    # continueTurn: clear both flags; teach the empty-box key on the slow path
    ('''        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        m_limitReached = false;
        submitAgent(QStringLiteral("Continue"), false);
        if (slowPath) {
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
            hint(QStringLiteral("continue.slow"), keys.isEmpty() ? QStringLiteral("Next time: /continue in the prompt box")
                                                               : relay::ShortcutHints::nextTime(keys, QStringLiteral("continue")));
        }''',
     '''        if (!m_configured) { status(QStringLiteral("No agent provider is configured.")); return; }
        m_limitReached = false;
        m_turnCutOff = false;
        submitAgent(QStringLiteral("Continue"), false);
        if (slowPath) {
            // agent.continue has no key of its own — its empty-box path IS agent.interrupt's
            // Ctrl+Enter — so the slow paths (/continue, the ▸ Continue link, the palette row)
            // teach that binding, read live from the Keymap rather than hard-coded (#SXF1).
            const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
            const QString sendNow = Keymap::instance().shortcutText(QStringLiteral("agent.interrupt"));
            hint(QStringLiteral("continue.slow"),
                 !sendNow.isEmpty() ? relay::ShortcutHints::nextTime(sendNow, QStringLiteral("continue a stopped turn from an empty prompt box"))
                                    : keys.isEmpty() ? QStringLiteral("Next time: /continue in the prompt box")
                                                     : relay::ShortcutHints::nextTime(keys, QStringLiteral("continue")));
        }'''),
    # accessor beside limitReached()
    ('    bool limitReached() const { return m_limitReached; }\n',
     '    bool limitReached() const { return m_limitReached; }\n'
     '    bool turnCutOff() const { return m_turnCutOff; }\n'),
    # done: any finished turn retires the restored-turn flag
    ('''        if (type == QStringLiteral("done")) m_limitReached = event.value(QStringLiteral("stop_reason")).toString() == QStringLiteral("limit");''',
     '''        if (type == QStringLiteral("done")) {
            m_limitReached = event.value(QStringLiteral("stop_reason")).toString() == QStringLiteral("limit");
            m_turnCutOff = false;   // a turn that finished — even cancelled — retires the restored flag
        }'''),
    # ready: clear both
    ('        if (type == QStringLiteral("ready")) { m_ledger.clear(); m_limitReached = false; return false; }\n',
     '        if (type == QStringLiteral("ready")) { m_ledger.clear(); m_limitReached = false; m_turnCutOff = false; return false; }\n'),
    # ▸ Continue link: teach the empty-box key when agent.continue is unbound
    ('''        const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
        const QString fast = keys.isEmpty() ? QStringLiteral("/continue") : keys + QStringLiteral(" or /continue");''',
     '''        // The link is a slow path, so it teaches the fast one: agent.continue's own key when it
        // has one, else the empty-box send-now that continues a stopped turn (#SXF1).
        const QString keys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
        const QString sendNow = Keymap::instance().shortcutText(QStringLiteral("agent.interrupt"));
        const QString fast = keys.isEmpty() ? (sendNow.isEmpty() ? QStringLiteral("/continue")
                                                                 : sendNow + QStringLiteral(" or /continue"))
                                            : keys + QStringLiteral(" or /continue");'''),
    # state_loaded: read turn_open (absent on fork load_state and from older workers → false)
    ('''            m_sessionId = event.value(QStringLiteral("session_id")).toString(m_sessionId);
            m_turnsCompleted = event.value(QStringLiteral("turns")).toInt();''',
     '''            m_sessionId = event.value(QStringLiteral("session_id")).toString(m_sessionId);
            m_turnsCompleted = event.value(QStringLiteral("turns")).toInt();
            // A resumed session whose last turn never ended: the pane may offer to continue it (#SXF1).
            m_turnCutOff = event.value(QStringLiteral("turn_open")).toBool();'''),
    # member beside m_limitReached
    ('''    bool m_limitReached = false;   // the last turn stopped at the step or tool-call limit\n''',
     '''    bool m_limitReached = false;   // the last turn stopped at the step or tool-call limit
    bool m_turnCutOff = false;     // a resumed session whose last turn never ended (#SXF1)\n'''),
]

for old, new in edits:
    n = text.count(old)
    if n != 1:
        sys.exit(f"match count {n} (want 1) for:\n{old[:120]}")
    text = text.replace(old, new)

pane.write_text(text)
print(f"applied {len(edits)} edits to {pane}")
