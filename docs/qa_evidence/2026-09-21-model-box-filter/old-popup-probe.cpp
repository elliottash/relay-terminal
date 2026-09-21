// SPDX-License-Identifier: AGPL-3.0-or-later
// Why the model box never showed which row was current — the measurement behind
// src/FilterPopup.h, kept so the claim can be re-checked on another Qt or another style.
//
//   g++ -fPIC old-popup-probe.cpp -o probe $(pkg-config --cflags --libs Qt5Widgets)
//   Xvfb :350 -screen 0 800x600x24 & DISPLAY=:350 ./probe <relay-stylesheet.qss> [fusion]
//
// It builds a QComboBox named `statusPicker` — the pane's model box, object name and all — under
// Relay's own stylesheet, opens the popup with row 1 current, and reads the pixel at the right
// edge of that row and of a row that is not current. Same pixel, and the "highlight" is
// invisible. The stylesheet is what `relay::theme::applyTheme` sets; dump it with
// `QFile(...).write(app.styleSheet().toUtf8())` from any Relay-linked program.
//
// Measured on this machine, Qt 5.15.13, theme Dark Copper (list ground #241c18):
//
//   default style (breeze)   current row #533927, other rows #241c18   -> visible
//   Fusion (what Relay sets) current row #1d1613, other rows #241c18   -> 3%, invisible
//
// Fusion answers SH_ComboBox_Popup with 1, so Qt draws the list as a menu through
// QComboMenuDelegate and CE_MenuItem, and the `selection-background-color: @accent` in
// `QComboBox#statusPicker QAbstractItemView` is never read. The selection is *set* — the
// selection model says one row is selected — it is only painted in a colour nobody can see.
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QItemSelectionModel>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc > 2 && QString::fromLatin1(argv[2]) == QLatin1String("fusion"))
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFile sheet(QString::fromLocal8Bit(argv[1]));
    sheet.open(QIODevice::ReadOnly);
    app.setStyleSheet(QString::fromUtf8(sheet.readAll()));

    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *box = new QComboBox;
    box->setObjectName(QStringLiteral("statusPicker"));
    box->addItem(QStringLiteral("glm-5.3 (main)"));
    box->addItem(QStringLiteral("glm-5.3-flash (flash)"));
    box->insertSeparator(2);
    box->addItem(QStringLiteral("kimi-k3 · kimi"));
    layout->addWidget(box);
    window.resize(320, 90);
    window.show();
    box->setCurrentIndex(1);

    QTimer::singleShot(400, [&] {
        box->showPopup();
        QTimer::singleShot(350, [&] {
            QAbstractItemView *view = box->view();
            const QRect current = view->visualRect(view->currentIndex());
            const QRect other = view->visualRect(view->model()->index(0, 0));
            const QImage shot = view->viewport()->grab().toImage();
            const auto at = [&shot](const QRect &row) {
                return shot.pixelColor(qMin(row.right() - 3, shot.width() - 1),
                                       qMin(row.center().y(), shot.height() - 1)).name();
            };
            QStyleOptionComboBox option;
            option.initFrom(box);
            qDebug().noquote()
                << "SH_ComboBox_Popup" << box->style()->styleHint(QStyle::SH_ComboBox_Popup, &option, box)
                << "delegate" << box->itemDelegate()->metaObject()->className()
                << "selected rows" << view->selectionModel()->selectedIndexes().size()
                << "palette Highlight" << view->palette().color(QPalette::Highlight).name()
                << "| current row" << at(current) << "other row" << at(other);
            app.quit();
        });
    });
    return app.exec();
}
