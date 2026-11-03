#include "visual_deck_storage_sort_filter_proxy_model.h"

#include <algorithm>

bool colorIdentityMatches(VisualDeckStorageSortFilterProxyModel::FilterMode mode,
                          const QSet<QChar> &colors,
                          const QString &identity)
{
    if (colors.isEmpty()) {
        return true;
    }

    const QString normalizedIdentity = identity.trimmed().toUpper();
    if (normalizedIdentity.isEmpty()) {
        // Old uploads and incomplete local metadata do not prove colorlessness.
        return false;
    }

    QSet<QChar> colorIdentitySet;
    if (normalizedIdentity == QStringLiteral("C")) {
        colorIdentitySet.insert(QLatin1Char('C'));
    } else {
        for (const QChar &color : normalizedIdentity) {
            if (!QStringLiteral("WUBRG").contains(color)) {
                return false;
            }
            colorIdentitySet.insert(color);
        }
    }

    QSet<QChar> activeColorSet;
    for (const QChar &color : colors) {
        activeColorSet.insert(color.toUpper());
    }

    switch (mode) {
        case VisualDeckStorageSortFilterProxyModel::ExactMatch:
            return activeColorSet == colorIdentitySet;
        case VisualDeckStorageSortFilterProxyModel::Includes:
            return std::all_of(activeColorSet.begin(), activeColorSet.end(),
                               [&colorIdentitySet](const QChar &color) { return colorIdentitySet.contains(color); });
        case VisualDeckStorageSortFilterProxyModel::Excludes:
            return std::none_of(activeColorSet.begin(), activeColorSet.end(),
                                [&colorIdentitySet](const QChar &color) { return colorIdentitySet.contains(color); });
    }
    return false;
}
