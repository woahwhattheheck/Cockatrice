#include "card_art_utils.h"

#include "libcockatrice/card/card_info.h"

#include <QRect>
#include <QSize>
#include <QTransform>
#include <cmath>
#include <libcockatrice/card/printing/exact_card.h>
#include <qnamespace.h>

namespace CardArtUtils
{
QPixmap rotateSidewaysLayoutArt(const QPixmap &art, const ExactCard &card)
{
    if (!card.getInfo().getUiAttributes().landscapeOrientation) {
        return art;
    }

    QTransform transform;
    transform.rotate(90);
    return art.transformed(transform, Qt::SmoothTransformation);
}

QRect cardArtCropRect(const QSize &size, bool landscape, const QRectF &crop)
{
    if (size.isEmpty()) {
        return {};
    }
    if (std::isfinite(crop.x()) && std::isfinite(crop.y()) && std::isfinite(crop.width()) &&
        std::isfinite(crop.height()) && crop.x() >= 0 && crop.y() >= 0 && crop.width() > 0 && crop.height() > 0 &&
        crop.right() <= 1 && crop.bottom() <= 1) {
        const int left = qBound(0, qRound(crop.left() * size.width()), size.width() - 1);
        const int top = qBound(0, qRound(crop.top() * size.height()), size.height() - 1);
        const int right = qBound(left + 1, qRound(crop.right() * size.width()), size.width());
        const int bottom = qBound(top + 1, qRound(crop.bottom() * size.height()), size.height());
        return {left, top, right - left, bottom - top};
    }

    const int marginX = size.width() * 0.07;
    const int top = size.height() * (landscape ? 0.05 : 0.11);
    const int bottom = size.height() * (landscape ? 0.42 : 0.45);
    return QRect(marginX, top, size.width() - 2 * marginX, size.height() - top - bottom)
        .intersected(QRect(QPoint(), size));
}

QPixmap cropCardArt(const QPixmap &art, const ExactCard &card, const QRectF &normalizedCrop)
{
    if (art.isNull()) {
        return {};
    }

    const QPixmap source = rotateSidewaysLayoutArt(art, card);
    const bool landscape = card.getInfo().getUiAttributes().landscapeOrientation;
    return source.copy(cardArtCropRect(source.size(), landscape, normalizedCrop));
}
} // namespace CardArtUtils
