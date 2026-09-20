#!/usr/bin/env python3
"""Apply #SXF1's src/RelayWindow.h edit: the palette row teaches the empty-box key."""
import sys
from pathlib import Path

window = Path("src/RelayWindow.h")
text = window.read_text()

old = '''        items << actionItem(agent, QStringLiteral("Continue agent turn"),
                            pane && pane->limitReached() ? QStringLiteral("The last turn stopped at its step limit · /continue")
                                                         : QStringLiteral("Send “Continue” to the agent · /continue"), QStringLiteral("agent.continue"));'''
new = '''        // The row is a slow path, so it teaches the fast one (#SXF1): agent.continue's own key
        // when it has one, else agent.interrupt's empty-box send-now that continues a stopped turn.
        QString continueKeys = Keymap::instance().shortcutText(QStringLiteral("agent.continue"));
        if (continueKeys.isEmpty()) continueKeys = Keymap::instance().shortcutText(QStringLiteral("agent.interrupt"));
        const QString continueHow = continueKeys.isEmpty() ? QStringLiteral("/continue")
                                                           : continueKeys + QStringLiteral(" on an empty box or /continue");
        items << actionItem(agent, QStringLiteral("Continue agent turn"),
                            pane && (pane->limitReached() || pane->turnCutOff())
                                ? QStringLiteral("The last turn stopped early · %1").arg(continueHow)
                                : QStringLiteral("Send “Continue” to the agent · %1").arg(continueHow),
                            QStringLiteral("agent.continue"));'''

n = text.count(old)
if n != 1:
    sys.exit(f"match count {n} (want 1)")
window.write_text(text.replace(old, new))
print("applied 1 edit to", window)
