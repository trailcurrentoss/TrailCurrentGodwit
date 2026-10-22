#pragma once

#include <QQuickImageProvider>

// Serves the bundled Ionicons SVGs tinted to a colour:
//   image://icon/<name>/<color>   e.g. image://icon/navigate/#ffffff
// Tinting happens once when the SVG is rasterised, and Qt caches the result,
// so icons cost one texture each with no shader effect.
class IconProvider : public QQuickImageProvider
{
public:
    IconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // One icon at one size and colour; a null image if there is no such icon.
    // Also used to send icons to the encoder modules' displays.
    static QImage render(const QString &name, const QColor &color, QSize size);
};
