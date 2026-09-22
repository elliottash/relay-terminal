// SPDX-License-Identifier: AGPL-3.0-or-later
// Card #EFT9, stream E evidence: what relay::panestate::build() now puts in a pane_state's
// `model` block for the three cases the card names. Linked against the real librelay-panestate.a,
// so this is the desktop's own builder, not a re-implementation. Its stdout is piped through
// remote/pane_state.py (the hub's cleaner) by probe.sh, which is the whole seam in one run.
#include "PaneState.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

using namespace relay::panestate;

namespace {
Inputs pane() {
    Inputs in;
    in.pane = QStringLiteral("p1");
    in.modelLabel = QStringLiteral("kimi-k2 · moonshot");
    in.mode = QStringLiteral("auto");
    in.queueHint = QString();
    return in;
}

void show(const char *what, const Inputs &in) {
    Tokens choices{QLatin1Char('m')}, sessions{QLatin1Char('s')};
    const QJsonObject model = build(1, in, choices, sessions).value(QStringLiteral("model")).toObject();
    std::printf("%s\t%s\n", what,
                QJsonDocument(model).toJson(QJsonDocument::Compact).constData());
}
}   // namespace

int main() {
    // 1. An ordinary model: three levels, the pane on `high`, nothing fixed.
    Inputs live = pane();
    live.efforts = QStringList{QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")};
    live.effort = QStringLiteral("high");
    show("live", live);

    // 2. Relay Free: the gateway picks the level for the role, and the levels are still worth
    //    showing (src/ModelCatalog.cpp `effortFixedOf`, `Entry::effortFixedReason`).
    Inputs free = pane();
    free.modelLabel = QStringLiteral("relay free · main");
    free.efforts = QStringList{QStringLiteral("low"), QStringLiteral("high")};
    free.effort = QStringLiteral("high");
    free.effortFixed = true;
    free.effortFixedReason = QStringLiteral("Relay Free sets the level for you.");
    show("relay-free", free);

    // 3. A provider whose words are its own, including one the old EFFORT pattern refused.
    Inputs provider = pane();
    provider.efforts = QStringList{QStringLiteral("low"), QStringLiteral("very_high")};
    provider.effort = QStringLiteral("very_high");
    show("provider-words", provider);

    // 4. A model with no reasoning knob: no levels, so no picker anywhere.
    Inputs none = pane();
    none.effort = QStringLiteral("high");
    none.effortFixed = true;
    none.effortFixedReason = QStringLiteral("kimi-k2 has no reasoning level.");
    show("no-levels", none);
    return 0;
}
