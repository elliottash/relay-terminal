#include <QApplication>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QImage>
#include <QDebug>
#include <QtMath>
#include <QTemporaryDir>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QIcon icon(QStringLiteral("data/icons/hicolor/256x256/apps/org.relayterminal.Relay.png"));
    if (icon.isNull()) return 1;
    const qreal scale = app.devicePixelRatio();
    QPixmap old = icon.pixmap(QSize(22,22) * scale);
    const qreal returnedDpr = old.devicePixelRatio();
    old.setDevicePixelRatio(scale);
    QLabel label;
    label.setPixmap(old);
    QImage canvas(QSize(30,30) * scale, QImage::Format_ARGB32_Premultiplied);
    canvas.setDevicePixelRatio(scale);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    icon.paint(&painter, QRect(4,4,22,22));
    painter.end();
    QRect bounds;
    for (int y=0; y<canvas.height(); ++y)
        for (int x=0; x<canvas.width(); ++x)
            if (qAlpha(canvas.pixel(x,y))) bounds |= QRect(x,y,1,1);
    qInfo() << "Qt" << qVersion() << "appDpr" << scale
            << "requested" << QSize(22,22)*scale << "returnedPixels" << old.size()
            << "returnedDpr" << returnedDpr << "oldLabelHint" << label.sizeHint()
            << "paint22PhysicalBounds" << bounds.size();
    if (bounds.width() > qCeil(22*scale) || bounds.height() > qCeil(22*scale)) return 2;
    // A real @2x asset: Qt may cap the requested size to the available artwork,
    // which is another reason not to guess or overwrite returned DPR metadata.
    QTemporaryDir assets;
    QImage original(QStringLiteral("data/icons/hicolor/256x256/apps/org.relayterminal.Relay.png"));
    original.scaled(22,22).save(assets.path() + "/variant.png");
    original.scaled(44,44).save(assets.path() + "/variant@2x.png");
    QIcon variant(assets.path() + "/variant.png");
    const QPixmap highDpi = variant.pixmap(QSize(22,22));
    qInfo() << "@2x logical request 22: pixels" << highDpi.size() << "DPR" << highDpi.devicePixelRatio();
    if (argc > 1) {
        QImage comparison(QSize(220,90) * scale, QImage::Format_ARGB32_Premultiplied);
        comparison.setDevicePixelRatio(scale);
        comparison.fill(QColor("#15181c"));
        QPainter p(&comparison);
        p.setPen(Qt::white);
        p.drawText(QRect(8,4,100,20), "Old pixmap path");
        p.drawText(QRect(118,4,100,20), "Paint 22 DIP");
        p.drawPixmap(8,30,old);
        icon.paint(&p,QRect(118,30,22,22));
        p.end();
        if (!comparison.save(QString::fromLocal8Bit(argv[1]))) return 3;
    }
    return 0;
}
