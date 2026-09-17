// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Dialogs for agent sessions: a filterable picker (rewind checkpoints, saved sessions) and the
// first-run instructions dialog. Plain Qt, no KDE dependencies.
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

class QWidget;

namespace relay::agentui {

struct PickerRow {
    QStringList columns;   // shown left to right
    QString detail;        // tooltip
    QVariant data;
};

struct PickerAction {
    QString id, label;
    bool primary = false;
};

struct PickerResult {
    int row = -1;          // -1 when cancelled
    QString action;        // id of the button used (Enter uses the primary action)
};

// Modal picker: a filter field over a multi-column list plus action buttons. Enter runs the
// primary action on the selected row; Esc cancels.
PickerResult pick(QWidget *parent, const QString &title, const QString &hint, const QStringList &headers,
                  const QList<PickerRow> &rows, const QList<PickerAction> &actions);

struct InstructionFile {
    QString path, tool, scope;
    qint64 bytes = 0;
    bool exists = false, checked = false;
};

struct OnboardingResult {
    bool accepted = false;
    QStringList files;          // checked paths
    bool projectAuto = true;
    bool synthesize = false;    // create ~/.config/relay/relay.md from the checked files
};

// First-run (and later "Instructions…") dialog listing instruction files found on this machine.
OnboardingResult chooseInstructions(QWidget *parent, const QList<InstructionFile> &found, bool projectAuto,
                                    const QString &relayMdPath);

}  // namespace relay::agentui
