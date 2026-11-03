#include "oracle_source_data.h"

#include <QCoreApplication>
#include <limits>

namespace
{
bool copyData(QIODevice &input, QIODevice &output, QString &error)
{
    char buffer[64 * 1024];
    while (!input.atEnd()) {
        const qint64 count = input.read(buffer, sizeof(buffer));
        if (count < 0) {
            error =
                QCoreApplication::translate("OracleSourceData", "Cannot read card data: %1.").arg(input.errorString());
            return false;
        }
        if (count == 0) {
            break;
        }
        qint64 written = 0;
        while (written < count) {
            const qint64 result = output.write(buffer + written, count - written);
            if (result <= 0) {
                error = QCoreApplication::translate("OracleSourceData", "Cannot write card data: %1.")
                            .arg(output.errorString());
                return false;
            }
            written += result;
        }
    }
    return true;
}
} // namespace

bool OracleSourceData::open(QString &error)
{
    if (!file.open()) {
        error = QCoreApplication::translate("OracleSourceData", "Cannot create a temporary card data file: %1.")
                    .arg(file.errorString());
        return false;
    }
    return true;
}

bool OracleSourceData::append(QIODevice &input, QString &error)
{
    return copyData(input, file, error);
}

bool OracleSourceData::openLocalFile(const QString &fileName, QString &error)
{
    mappedFile.setFileName(fileName);
    if (!mappedFile.open(QIODevice::ReadOnly)) {
        error = QCoreApplication::translate("OracleSourceData", "Cannot open card data file '%1': %2.")
                    .arg(fileName, mappedFile.errorString());
        return false;
    }
    localFile = true;
    return true;
}

bool OracleSourceData::rewind(QString &error)
{
    if ((!localFile && !file.flush()) || !device().seek(0)) {
        error = QCoreApplication::translate("OracleSourceData", "Cannot read the card data file: %1.")
                    .arg(device().errorString());
        return false;
    }
    return true;
}

bool OracleSourceData::map(QString &error)
{
    if (mapping) {
        return true;
    }
    if (!rewind(error)) {
        return false;
    }
    const qint64 length = size();
    if (length <= 0 || length > std::numeric_limits<qsizetype>::max()) {
        error = QCoreApplication::translate("OracleSourceData", "The card data file is empty or too large to open.");
        return false;
    }
    if (!localFile) {
        mappedFile.setFileName(file.fileName());
        if (!mappedFile.open(QIODevice::ReadOnly)) {
            error = mappedFile.errorString();
            return false;
        }
    }
    mapping = mappedFile.map(0, length);
    if (!mapping) {
        error = QCoreApplication::translate(
                    "OracleSourceData",
                    "Cannot map the card data file: %1. Close other applications and try again, or select a smaller "
                    "JSON file or a ready-made Cockatrice XML database.")
                    .arg(mappedFile.errorString());
        return false;
    }
    mappedLength = length;
    return true;
}

QByteArray OracleSourceData::bytes() const
{
    return mapping
               ? QByteArray::fromRawData(reinterpret_cast<const char *>(mapping), static_cast<qsizetype>(mappedLength))
               : QByteArray();
}
