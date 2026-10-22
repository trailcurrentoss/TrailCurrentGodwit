#include "iconprovider.h"

#include <QColor>
#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const qsizetype slash = id.indexOf(u'/');
    const QString name = slash < 0 ? id : id.left(slash);
    const QColor color(slash < 0 ? QStringLiteral("#000000") : id.mid(slash + 1));

    const QSize out = requestedSize.isValid() ? requestedSize : QSize(24, 24);
    if (size)
        *size = out;

    return render(name, color, out);
}

QImage IconProvider::render(const QString &name, const QColor &color, QSize size)
{
    QFile file(QStringLiteral(":/assets/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("icon: no such icon '%s'", qPrintable(name));
        return {};
    }

    // Filled Ionicons inherit their fill from the root element; outline ones
    // stroke with currentColor. Set both to the requested colour.
    const QByteArray hex = color.name(QColor::HexRgb).toLatin1();
    QByteArray svg = file.readAll();
    svg.replace("currentColor", hex);
    svg.replace("<svg ", "<svg fill=\"" + hex + "\" ");

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    QPainter painter(&image);
    painter.setOpacity(color.alphaF());
    renderer.render(&painter);
    return image;
}
