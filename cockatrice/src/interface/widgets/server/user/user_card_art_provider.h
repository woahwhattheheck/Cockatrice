#ifndef COCKATRICE_USER_CARD_ART_PROVIDER_H
#define COCKATRICE_USER_CARD_ART_PROVIDER_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QPixmap>
#include <QQueue>
#include <QRectF>
#include <QSet>
#include <QString>
#include <qtmetamacros.h>

class UserCardArtProvider : public QObject
{
    Q_OBJECT

public:
    explicit UserCardArtProvider(QObject *parent = nullptr);

    void requestCardArt(const QString &userName,
                        const QString &cardName,
                        const QString &providerId,
                        const QRectF &crop = {});
    const QMap<QString, QPixmap> &cache() const;
    static QString
    cacheKey(const QString &userName, const QString &cardName, const QString &providerId, const QRectF &crop = {});

signals:
    void cardArtUpdated(const QString &userName);

public slots:
    void onDatabaseReady();

private:
    bool dbReady = false;
    static constexpr int MaxCacheEntries = 1024;
    QList<QString> cacheInsertionOrder; // FIFO eviction
    QMap<QString, QPixmap> cardArtCache;
    QSet<QString> pending;
    struct ArtRequest
    {
        QString key, userName, cardName, providerId;
        QRectF crop;
    };
    QQueue<ArtRequest> queue;

    void processQueue();
    void insertIntoCache(const QString &key, const QPixmap &pixmap);
};

#endif // COCKATRICE_USER_CARD_ART_PROVIDER_H
