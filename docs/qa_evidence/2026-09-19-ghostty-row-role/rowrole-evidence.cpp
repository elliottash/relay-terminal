// SPDX-License-Identifier: GPL-3.0-or-later
// Evidence generator for #J5DN: the row-role band under the Ghostty core.
//
//   QT_QPA_PLATFORM=offscreen ./rowrole-evidence <core> <outdir>
//
// Starts a pane on the given core, writes the same inline rows
// Pane::printInline would (OSC 7772 role + bold text, plain rows between),
// paints it, then switches the scheme the way a theme switch does and paints
// again — the same rows, recoloured, nothing rewritten. The band/ink values
// are the ones EngineBackend::applyThemeColors sets for "channel": the theme's
// shell/agent destination colour with a chip ink on it (Relay Dark, then
// Relay Light, from data/theme/themes/*.toml).
//
// Built ad hoc against a configured engine build (see README.md); not part of
// the CMake tree.
#include "backend/VTermBackend.h"
#include "session/TerminalSession.h"
#include "view/TerminalView.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <QWidget>

#include <memory>

using namespace relay;

static bool waitScreen(VTermBackend *backend, const QString &needle, int ms = 4000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (backend->screenText().contains(needle))
            return true;
        qApp->processEvents(QEventLoop::AllEvents, 10);
    }
    return false;
}

static QImage grab(TerminalView *view)
{
    // The first frame can lag under the offscreen platform: wait until the
    // widget actually painted something (more than one colour), not just a
    // fixed interval.
    QImage img;
    for (int i = 0; i < 40; ++i) {
        qApp->processEvents(QEventLoop::AllEvents, 50);
        img = view->grab().toImage();
        QRgb first = 0;
        bool varied = false;
        for (int y = 0; y < img.height() && !varied; y += 3) {
            for (int x = 0; x < img.width(); x += 7) {
                const QRgb c = img.pixel(x, y);
                if (!x && !y)
                    first = c;
                else if (c != first) {
                    varied = true;
                    break;
                }
            }
        }
        if (varied)
            return img;
    }
    return img;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString core = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("ghostty");
    const QString out = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral(".");
    std::unique_ptr<VTermBackend> backend = std::make_unique<VTermBackend>(core);
    TerminalView *view = backend->view();
    backend->resizeTerminal(14, 76);
    backend->widget()->show();
    if (!backend->startProgram(QStringLiteral("/bin/cat"), {}, QDir::tempPath())) {
        qWarning("cat failed to start: %s", qPrintable(backend->session()->errorString()));
        return 1;
    }
    view->setFocus();

    ColorScheme dark = view->colorScheme();
    dark.background = QColor(0x0f, 0x11, 0x15);
    dark.foreground = QColor(0xe6, 0xe8, 0xec);
    dark.userShellBand = QColor(0x3e, 0xc5, 0xf0);
    dark.userShellInk = QColor(0x06, 0x1a, 0x22);
    dark.userAgentBand = QColor(0xb4, 0x8e, 0xf7);
    dark.userAgentInk = QColor(0x14, 0x0d, 0x2e);
    view->setColorScheme(dark);

    backend->writeToDisplay("plain output, no role\r\n"
                            "\x1b]7772;agent\x1b\\\x1b[1m* fix the build\x1b[0m\r\n"
                            "plain reply\r\n"
                            "\x1b]7772;shell\x1b\\\x1b[1m! make release\x1b[0m\r\n"
                            "plain output, no role\r\n");
    if (!waitScreen(backend.get(), QStringLiteral("! make release"))) {
        qWarning("rows never reached the screen");
        return 2;
    }
    if (!grab(view).save(out + QStringLiteral("/%1-1-relay-dark.png").arg(core)))
        return 3;

    // The theme changes under the same scrollback: Relay Light's pair.
    ColorScheme light = dark;
    light.background = QColor(0xfb, 0xfb, 0xfd);
    light.foreground = QColor(0x1a, 0x1d, 0x24);
    light.userShellBand = QColor(0x00, 0x6a, 0xb1);
    light.userShellInk = QColor(0xff, 0xff, 0xff);
    light.userAgentBand = QColor(0x7c, 0x3a, 0xed);
    light.userAgentInk = QColor(0xff, 0xff, 0xff);
    view->setColorScheme(light);
    if (!grab(view).save(out + QStringLiteral("/%1-2-relay-light.png").arg(core)))
        return 4;
    return 0;
}
