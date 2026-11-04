#include "remote_deck_download.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(RemoteDeckDownload, RejectsInvalidDirectoryNames)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const QStringList invalidNames{"", ".", "..", "a/b", "a\\b", QString("a") + QChar::Null + "b"};
    for (const auto &name : invalidNames) {
        QString error;
        EXPECT_TRUE(RemoteDeckDownload::prepareDirectory(temporary.path(), name, error).isEmpty());
        EXPECT_FALSE(error.isEmpty());
    }
    EXPECT_TRUE(QDir(temporary.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

TEST(RemoteDeckDownload, AppliesColonRestrictionOnlyOnWindows)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    QString error;
    const QString path = RemoteDeckDownload::prepareDirectory(temporary.path(), "Modern: 2024", error);
#ifdef Q_OS_WIN
    EXPECT_TRUE(path.isEmpty());
    EXPECT_FALSE(error.isEmpty());
#else
    EXPECT_EQ(path, QDir(temporary.path()).filePath("Modern: 2024"));
    EXPECT_TRUE(QFileInfo(path).isDir());
    EXPECT_TRUE(error.isEmpty());
#endif
}

TEST(RemoteDeckDownload, CreatesAndReusesTheExactDirectory)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    QString error = "previous failure";
    const QString expected = QDir(temporary.path()).filePath("Modern decks");
    EXPECT_EQ(RemoteDeckDownload::prepareDirectory(temporary.path(), "Modern decks", error), expected);
    EXPECT_TRUE(error.isEmpty());
    QFile marker(QDir(expected).filePath("retained.txt"));
    ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
    ASSERT_EQ(marker.write("keep"), 4);
    marker.close();
    EXPECT_EQ(RemoteDeckDownload::prepareDirectory(temporary.path(), "Modern decks", error), expected);
    EXPECT_TRUE(error.isEmpty());
    ASSERT_TRUE(marker.open(QIODevice::ReadOnly));
    EXPECT_EQ(marker.readAll(), QByteArray("keep"));
}

TEST(RemoteDeckDownload, RejectsAFileCollisionWithoutReplacingIt)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    QFile file(QDir(temporary.path()).filePath("occupied"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("deck data"), 9);
    file.close();
    QString error;
    EXPECT_TRUE(RemoteDeckDownload::prepareDirectory(temporary.path(), "occupied", error).isEmpty());
    EXPECT_FALSE(error.isEmpty());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), QByteArray("deck data"));
}

TEST(RemoteDeckDownload, RejectsSymbolicLinkDirectories)
{
#ifdef Q_OS_UNIX
    QTemporaryDir temporary;
    QTemporaryDir external;
    ASSERT_TRUE(temporary.isValid());
    ASSERT_TRUE(external.isValid());
    const QString link = QDir(temporary.path()).filePath("linked");
    ASSERT_TRUE(QFile::link(external.path(), link));
    ASSERT_TRUE(QFileInfo(link).isSymLink());
    QString error;
    EXPECT_TRUE(RemoteDeckDownload::prepareDirectory(temporary.path(), "linked", error).isEmpty());
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(QFileInfo(link).isSymLink());
    EXPECT_TRUE(QDir(external.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
#else
    GTEST_SKIP() << "QFile::link creates shortcuts rather than filesystem symbolic links on Windows.";
#endif
}

TEST(RemoteDeckDownload, RejectsNonDeckAndMalformedPayloadsButAcceptsAnEmptyDeck)
{
    for (const auto &payload : {QString(), QString("not XML"), QString("<html/>"), QString("<cockatrice_deck>"),
                                QString("<cockatrice_deck/><extra/>")}) {
        QString error;
        EXPECT_FALSE(RemoteDeckDownload::parse(payload, error).has_value());
        EXPECT_FALSE(error.isEmpty());
    }
    QString error = "previous failure";
    const auto empty = RemoteDeckDownload::parse("<?xml version=\"1.0\"?><cockatrice_deck version=\"1\"/>", error);
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->isEmpty());
    EXPECT_TRUE(error.isEmpty());
}

TEST(RemoteDeckDownload, RemovesSelectedDescendantsAndDuplicateColumnsInSelectionOrder)
{
    QStandardItemModel model;
    auto folder = new QStandardItem("folder");
    auto child = new QStandardItem("child folder");
    auto deck = new QStandardItem("nested deck");
    model.appendRow({folder, new QStandardItem("folder metadata")});
    folder->appendRow({child, new QStandardItem("child metadata")});
    child->appendRow(deck);
    auto independent = new QStandardItem("independent deck");
    model.appendRow({independent, new QStandardItem("deck metadata")});
    const auto independentIndex = independent->index();
    const auto folderIndex = folder->index();
    const QModelIndexList selection{deck->index(),
                                    independentIndex.siblingAtColumn(1),
                                    folderIndex.siblingAtColumn(1),
                                    child->index(),
                                    independentIndex,
                                    folderIndex,
                                    QModelIndex()};
    EXPECT_EQ(RemoteDeckDownload::selectionRoots(selection), (QModelIndexList{independentIndex, folderIndex}));
}
