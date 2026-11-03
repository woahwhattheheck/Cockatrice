#include "color_identity.h"

QString sanitizeColorIdentity(const QString &colorIdentity)
{
    const QString normalized = colorIdentity.trimmed().toUpper();
    if (normalized == QStringLiteral("C")) {
        return QStringLiteral("C");
    }
    for (const QChar &color : normalized) {
        if (!QStringLiteral("WUBRG").contains(color)) {
            return {};
        }
    }
    QString sanitized;
    for (const QChar &color : QStringLiteral("WUBRG")) {
        if (normalized.contains(color)) {
            sanitized.append(color);
        }
    }
    return sanitized;
}
