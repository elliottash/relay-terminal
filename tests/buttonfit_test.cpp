// SPDX-License-Identifier: GPL-3.0-or-later
// Button labels have to fit inside their buttons (owner report, 2026-09-18: "Add / replace…" in
// the API keys modal painted past its own edge).
//
// The cause is a Qt rule worth knowing: QStyleSheetStyle only folds a stylesheet rule's font into
// the widget's font when the rule carries no pseudo-state. `QPushButton:default { font-weight: 600 }`
// therefore *paints* bold while QPushButton::sizeHint() still measures the label at regular weight,
// so the button comes out too narrow and clips its own text. Any button can hit it: inside a dialog
// push buttons are autoDefault, so the `:default` state follows the focus from button to button.
//
// Rather than pin the one label, this walks every QPushButton label in src/ and renders each one in
// every state the stylesheet gives a different font — plain, `:default`, and `#primary` — measuring
// the ink instead of trusting any metric. A label is clipped when it paints wider once the button is
// given room than it does at its own sizeHint width. That is style-independent: a future theme, a
// longer translation or another bold state is caught by the same check.
#include "Theme.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Every QPushButton label written in the app's sources: `new QPushButton(QStringLiteral("…"))`, the
// QStringLiteral-free spelling, and setText() on a button variable. Labels built at runtime from
// data (provider names, file names) are out of reach here and are covered by the widest-case
// synthetic labels below.
QStringList labelsInSources() {
    QStringList labels;
    // Raw string literals are avoided on purpose: moc cannot parse them.
    const QRegularExpression re(
        QStringLiteral("QPushButton\\s*\\(\\s*(?:QStringLiteral\\s*\\(\\s*)?\"((?:[^\"\\\\]|\\\\.)+)\""));
    QDir dir(QStringLiteral(RELAY_SOURCE_DIR) + QStringLiteral("/src"));
    const QStringList sources = dir.entryList({QStringLiteral("*.cpp")}, QDir::Files, QDir::Name);
    for (const QString &name : sources) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString text = QString::fromUtf8(file.readAll());
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QString label = it.next().captured(1);
            if (!label.isEmpty() && !labels.contains(label)) labels.append(label);
        }
    }
    return labels;
}

enum class Kind { Plain, Default, Primary };

const char *kindName(Kind kind) {
    switch (kind) {
    case Kind::Plain: return "plain";
    case Kind::Default: return ":default";
    case Kind::Primary: return "#primary";
    }
    return "";
}

// Columns of `image` that differ from `blank`: the label's ink, with the background, border and
// rounded corners subtracted. Returns the ink width in pixels (0 when the label paints nothing).
int inkWidth(const QImage &image, const QImage &blank) {
    int first = -1, last = -1;
    for (int x = 0; x < std::min(image.width(), blank.width()); ++x) {
        bool ink = false;
        for (int y = 0; y < image.height() && !ink; ++y) ink = image.pixel(x, y) != blank.pixel(x, y);
        if (!ink) continue;
        if (first < 0) first = x;
        last = x;
    }
    return first < 0 ? 0 : last - first + 1;
}

QImage shot(QPushButton *button) {
    QImage image(button->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    button->render(&image);
    return image;
}

// The label's ink width when the button is `extra` pixels wider than its own sizeHint. Room enough
// and the glyphs paint in full; at extra == 0 a too-narrow button paints a cut-off label.
int inkAt(QPushButton *button, int extra) {
    const QString label = button->text();
    button->adjustSize();
    button->resize(button->sizeHint().width() + extra, button->sizeHint().height());
    const QImage withText = shot(button);
    button->setText(QString());
    const QImage blank = shot(button);
    button->setText(label);
    return inkWidth(withText, blank);
}

}  // namespace

class ButtonFitTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        // The floor test below switches themes, which writes theme/name: never to the real profile.
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", m_config.path().toLocal8Bit());
        auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
        relay::theme::applyTheme(*app);
        // Lets QA reproduce the reported clipping without editing the stylesheet, e.g.
        //   RELAY_BUTTONFIT_EXTRA_QSS='QPushButton:default { font-weight: 600; }'
        // puts the old rule back and this test has to fail on every :default row.
        const QString extra = QString::fromLocal8Bit(qgetenv("RELAY_BUTTONFIT_EXTRA_QSS"));
        if (!extra.isEmpty()) app->setStyleSheet(app->styleSheet() + QLatin1Char('\n') + extra);
    }

    // The legibility floor (docs/ARCHITECTURE.md, "Legible text"): every font-size in the live
    // stylesheet is in points and at least theme::FloorPt, in every shipped theme (a theme's flags
    // append rules of their own). The application font is at least theme::BodyPt.
    void stylesheetFontsStayAtOrAboveTheFloor() {
        auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
        QVERIFY(app->font().pointSizeF() >= relay::theme::BodyPt);
        const QString before = relay::theme::activeThemeId();
        const QRegularExpression size(QStringLiteral("font-size:\\s*([0-9.]+)\\s*([a-z]*)"));
        int seen = 0;
        for (const auto &choice : relay::theme::availableThemes()) {
            if (!choice.builtin) continue;
            QVERIFY(relay::theme::setActiveTheme(choice.id));
            auto it = size.globalMatch(app->styleSheet());
            while (it.hasNext()) {
                const auto m = it.next();
                ++seen;
                QVERIFY2(m.captured(2) == QStringLiteral("pt"),
                         qPrintable(QStringLiteral("%1: \"%2\" is not in points").arg(choice.id, m.captured(0))));
                QVERIFY2(m.captured(1).toDouble() >= relay::theme::FloorPt,
                         qPrintable(QStringLiteral("%1: \"%2\" is under the %3pt floor")
                                        .arg(choice.id, m.captured(0)).arg(relay::theme::FloorPt)));
            }
        }
        QVERIFY2(seen > 20, "no font-size rules found; the scan is broken");
        relay::theme::setActiveTheme(before);
        // legible() raises a small font and leaves a large one alone, in points or pixels.
        QFont tiny; tiny.setPointSizeF(7);
        QCOMPARE(relay::theme::legible(tiny).pointSizeF(), relay::theme::FloorPt);
        QFont pixels; pixels.setPixelSize(7);
        QCOMPARE(relay::theme::legible(pixels).pointSizeF(), relay::theme::FloorPt);
        QFont big; big.setPointSizeF(12);
        QCOMPARE(relay::theme::legible(big).pointSizeF(), 12.0);
    }

    // Guards the measurement itself: a label that plainly does not fit has to be reported as
    // clipped, or the sweep below would pass by being blind.
    void detectsClipping() {
        const QString label = QStringLiteral("A label far too long for this button");
        QDialog dialog;
        auto *button = new QPushButton(label, &dialog);   // no layout: the resizes below stand
        dialog.show();
        const int roomy = inkAt(button, 400);
        button->resize(60, button->sizeHint().height());
        const QImage narrow = shot(button);
        button->setText(QString());
        const QImage blank = shot(button);
        button->setText(label);
        QVERIFY(roomy > 0);
        QVERIFY(inkWidth(narrow, blank) < roomy);
    }

    void labelsFitTheirButtons_data() {
        QTest::addColumn<QString>("label");
        QTest::addColumn<int>("kind");
        QStringList labels = labelsInSources();
        QVERIFY2(labels.size() > 20, qPrintable(QStringLiteral("only %1 labels found in src/; the "
                                                               "scan is broken").arg(labels.size())));
        // Widest realistic runtime labels, which the source scan cannot see.
        labels << QStringLiteral("Import from Claude Code / Codex")
               << QStringLiteral("OpenRouter · DeepSeek V4.1 Flash");
        for (const QString &label : labels)
            for (Kind kind : {Kind::Plain, Kind::Default, Kind::Primary})
                QTest::newRow(qPrintable(QStringLiteral("%1 [%2]").arg(label, QString::fromLatin1(kindName(kind)))))
                    << label << int(kind);
    }

    // Every label, in every state the stylesheet styles differently, has to paint in full at the
    // width the button asks the layout for.
    void labelsFitTheirButtons() {
        QFETCH(QString, label);
        QFETCH(int, kind);
        QDialog dialog;
        auto *layout = new QVBoxLayout(&dialog);
        auto *button = new QPushButton(label, &dialog);
        if (kind == int(Kind::Primary)) button->setObjectName(QStringLiteral("primary"));
        layout->addWidget(button);
        dialog.show();
        button->setDefault(kind == int(Kind::Default));
        button->ensurePolished();

        const int natural = inkAt(button, 0);
        const int roomy = inkAt(button, 400);
        QVERIFY2(natural >= roomy - 1,
                 qPrintable(QStringLiteral("“%1” (%2) is clipped: the label paints %3px wide given "
                                           "room but only %4px at the button's own width of %5px")
                                .arg(label, QString::fromLatin1(kindName(Kind(kind))))
                                .arg(roomy).arg(natural).arg(button->sizeHint().width())));
    }

private:
    QTemporaryDir m_config;
};

QTEST_MAIN(ButtonFitTest)
#include "buttonfit_test.moc"
