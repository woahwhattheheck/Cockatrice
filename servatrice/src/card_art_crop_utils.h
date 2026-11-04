#ifndef CARD_ART_CROP_UTILS_H
#define CARD_ART_CROP_UTILS_H

#include "libcockatrice/protocol/pb/card_art_crop.pb.h"

#include <QJsonObject>
#include <QJsonValue>
#include <cmath>

namespace CardArtCropUtils
{
inline bool isValid(const CardArtCrop &crop)
{
    return crop.has_left() && crop.has_top() && crop.has_width() && crop.has_height() && std::isfinite(crop.left()) &&
           std::isfinite(crop.top()) && std::isfinite(crop.width()) && std::isfinite(crop.height()) &&
           crop.left() >= 0.0 && crop.top() >= 0.0 && crop.width() > 0.0 && crop.height() > 0.0 &&
           crop.left() + crop.width() <= 1.0 && crop.top() + crop.height() <= 1.0;
}

inline QJsonObject toJson(const CardArtCrop &crop)
{
    return {{"left", crop.left()}, {"top", crop.top()}, {"width", crop.width()}, {"height", crop.height()}};
}

// Invalid or older stored data leaves no custom crop, so the client retains its
// automatic extraction. Do not accept strings/null as numeric coordinates.
inline bool fromJson(const QJsonValue &value, CardArtCrop &crop)
{
    crop.Clear();
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject obj = value.toObject();
    if (!obj.value("left").isDouble() || !obj.value("top").isDouble() || !obj.value("width").isDouble() ||
        !obj.value("height").isDouble()) {
        return false;
    }
    CardArtCrop parsed;
    parsed.set_left(obj.value("left").toDouble());
    parsed.set_top(obj.value("top").toDouble());
    parsed.set_width(obj.value("width").toDouble());
    parsed.set_height(obj.value("height").toDouble());
    if (!isValid(parsed)) {
        return false;
    }
    crop.CopyFrom(parsed);
    return true;
}
} // namespace CardArtCropUtils

#endif
