#ifndef ORACLE_SOURCE_DATA_H
#define ORACLE_SOURCE_DATA_H

#include <QByteArray>
#include <QTemporaryFile>

/**
 * Oracle input storage. Downloads and decompression write to temporary files;
 * local inputs open directly without a full-file copy. The JSON scanner and
 * importer share a read-only mapping of the resulting input.
 * The mapping remains valid until this object is destroyed. Access is serial:
 * the download finishes before a worker takes ownership of the input.
 */
class OracleSourceData
{
public:
    bool open(QString &error);
    bool openLocalFile(const QString &fileName, QString &error);
    bool append(QIODevice &input, QString &error);
    bool rewind(QString &error);
    bool map(QString &error);

    QFile &device()
    {
        return localFile ? mappedFile : file;
    }
    qint64 size() const
    {
        return localFile ? mappedFile.size() : file.size();
    }
    QByteArray bytes() const;

private:
    QTemporaryFile file;
    QFile mappedFile;
    bool localFile = false;
    uchar *mapping = nullptr;
    qint64 mappedLength = 0;
};

#endif
