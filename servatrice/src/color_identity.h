#ifndef SERVATRICE_COLOR_IDENTITY_H
#define SERVATRICE_COLOR_IDENTITY_H

#include <QString>

/** @brief Normalizes WUBRG colors or standalone C; malformed/unknown input is empty. */
QString sanitizeColorIdentity(const QString &colorIdentity);

#endif // SERVATRICE_COLOR_IDENTITY_H
