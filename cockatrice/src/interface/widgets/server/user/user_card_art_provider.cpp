#include "user_card_art_provider.h"

#include "../../../card_picture_loader/card_picture_loader.h"
#include "../../cards/card_art_utils.h"
#include "libcockatrice/card/card_info.h"
#include "libcockatrice/card/database/card_database.h"
#include "libcockatrice/card/database/card_database_loader.h"
#include "libcockatrice/card/database/card_database_querier.h"
#include "libcockatrice/card/printing/exact_card.h"
#include "libcockatrice/card/printing/printing_info.h"

#include <QChar>
#include <QMetaObject>
#include <QPointer>
#include <QSize>
#include <QStringList>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/utility/card_ref.h>
#include <memory>

QString
UserCardArtProvider::cacheKey(const QString &user, const QString &card, const QString &providerId, const QRectF &crop)
{
    QString key = user + u'|' + card + u'|' + providerId;
    if (!crop.isEmpty()) {
        key += u'|' + QString::number(crop.x(), 'g', 17) + u',' + QString::number(crop.y(), 'g', 17) + u',' +
               QString::number(crop.width(), 'g', 17) + u',' + QString::number(crop.height(), 'g', 17);
    }
    return key;
}

UserCardArtProvider::UserCardArtProvider(QObject *parent) : QObject(parent)
{
    dbReady = (CardDatabaseManager::getInstance()->getLoadStatus() == LoadStatus::Ok);

    if (!dbReady) {
        connect(CardDatabaseManager::getInstance(), &CardDatabase::cardDatabaseLoadingFinished, this,
                &UserCardArtProvider::onDatabaseReady);
    }
}

void UserCardArtProvider::onDatabaseReady()
{
    dbReady = true;
    processQueue();
}

const QMap<QString, QPixmap> &UserCardArtProvider::cache() const
{
    return cardArtCache;
}

void UserCardArtProvider::requestCardArt(const QString &userName,
                                         const QString &cardName,
                                         const QString &providerId,
                                         const QRectF &crop)
{
    if (cardName.isEmpty()) {
        return;
    }

    const QString key = cacheKey(userName, cardName, providerId, crop);

    if (pending.contains(key)) {
        return;
    }
    if (cardArtCache.contains(key) && !cardArtCache.value(key).isNull()) {
        return;
    }

    pending.insert(key);
    queue.enqueue({key, userName, cardName, providerId, crop});

    processQueue();
}

void UserCardArtProvider::insertIntoCache(const QString &key, const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return;
    }

    if (!cardArtCache.contains(key)) {
        cacheInsertionOrder.append(key);
        while (cacheInsertionOrder.size() > MaxCacheEntries) {
            const QString evicted = cacheInsertionOrder.takeFirst();
            cardArtCache.remove(evicted);
        }
    }
    cardArtCache.insert(key, pixmap);
}

void UserCardArtProvider::processQueue()
{
    if (!dbReady) {
        return;
    }

    while (!queue.isEmpty()) {
        const ArtRequest request = queue.dequeue();
        const QString key = request.key;
        const QString userName = request.userName;
        const QString cardName = request.cardName;
        const QString providerId = request.providerId;
        const QRectF crop = request.crop;

        ExactCard card = CardDatabaseManager::query()->getCard({cardName, providerId});

        if (!card) {
            pending.remove(key);
            continue;
        }

        QPixmap fullRes;
        CardPictureLoader::getPixmap(fullRes, card, QSize(745, 1040));

        // Synchronous hit (already loaded/on disk)
        if (!fullRes.isNull()) {
            insertIntoCache(key, CardArtUtils::cropCardArt(fullRes, card, crop));
            pending.remove(key);

            emit cardArtUpdated(userName);
            continue;
        }

        // Async load required.
        QPointer<UserCardArtProvider> self(this);

        auto conn = std::make_shared<QMetaObject::Connection>();

        *conn = connect(card.getCardPtr().data(), &CardInfo::pixmapUpdated, this,
                        [self, key, userName, card, crop, conn]() mutable {
                            if (!self) {
                                return;
                            }

                            QObject::disconnect(*conn);

                            QPixmap fullRes;
                            CardPictureLoader::getPixmap(fullRes, card, QSize(745, 1040));

                            if (!fullRes.isNull()) {
                                self->insertIntoCache(key, CardArtUtils::cropCardArt(fullRes, card, crop));
                            }

                            self->pending.remove(key);

                            emit self->cardArtUpdated(userName);

                            // Resume processing remaining queued items.
                            self->processQueue();
                        });

        // Stop here. We'll continue when the async load finishes.
        return;
    }
}
