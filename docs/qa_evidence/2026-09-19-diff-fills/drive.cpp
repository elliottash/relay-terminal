// SPDX-License-Identifier: AGPL-3.0-or-later
// The implementer's evidence drive for the diff fills (card #BH3R): one DiffView, the real widget,
// walked through every shipped theme by the live theme switcher. Each PNG is the same diff as that
// theme draws it — the add/remove lines must be black-or-white text on the theme's own green/red,
// never green/red text.
//
// Build and run from the repo root via drive.sh (Xvfb, isolated XDG_CONFIG_HOME).
#include "DiffView.h"
#include "Theme.h"

#include <QApplication>
#include <QStringList>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    relay::DiffView view;
    view.setDiff(QStringLiteral("src/pane.cpp"), QStringLiteral(
        "--- a/src/pane.cpp\n"
        "+++ b/src/pane.cpp\n"
        "@@ -3,4 +3,6 @@\n"
        " #include \"pane.h\"\n"
        "-int old = 1;\n"
        "+int new = 1;\n"
        "+int extra = 2;\n"
        " // the rest is context\n"));
    view.resize(780, 430);
    view.show();
    const QStringList themes{QStringLiteral("relay-dark"), QStringLiteral("dark-copper"),
                             QStringLiteral("gruvbox-dark"), QStringLiteral("relay-light"),
                             QStringLiteral("ibm-beige")};
    for (const QString &id : themes) {
        if (!relay::theme::setActiveTheme(id, false)) {
            fprintf(stderr, "drive: theme %s did not apply\n", qPrintable(id));
            return 1;
        }
        app.processEvents();   // themeChanged re-renders the diff before the grab
        if (!view.grab().save(out + QStringLiteral("/diff-") + id + QStringLiteral(".png"))) {
            fprintf(stderr, "drive: could not write %s/diff-%s.png\n", qPrintable(out), qPrintable(id));
            return 1;
        }
    }
    return 0;
}
