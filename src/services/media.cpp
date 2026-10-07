#include "services/media.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <cmath>
#include <numbers>

namespace qding {
MediaService::MediaService(QString root) : root_(QDir(std::move(root)).absolutePath()) {
    QDir().mkpath(root_ + QStringLiteral("/media/images"));
    QDir().mkpath(root_ + QStringLiteral("/media/sounds"));
}

QString MediaService::resolve(const QString &relative) const {
    if (relative.isEmpty() || QDir::isAbsolutePath(relative))
        return {};
    const auto clean = QDir::cleanPath(relative);
    if (!clean.startsWith(QStringLiteral("media/")) || clean.contains(QStringLiteral("../")))
        return {};
    const QFileInfo file(root_ + QLatin1Char('/') + clean);
    const auto canonical = file.canonicalFilePath();
    const auto base = QFileInfo(root_).canonicalFilePath() + QLatin1Char('/');
    return file.isFile() && canonical.startsWith(base) ? canonical : QString{};
}

QString MediaService::importFile(const QString &source, bool image, QString *error) const {
    const QFileInfo info(source);
    const auto suffix = info.suffix().toLower();
    const QStringList supported =
        image ? QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                            QStringLiteral("gif")}
              : QStringList{QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("ogg"),
                            QStringLiteral("flac"), QStringLiteral("m4a")};
    if (!supported.contains(suffix) || info.size() <= 0 || info.size() > 20 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("请选择支持的格式，单文件不得超过 20 MB。");
        return {};
    }
    if (image) {
        // canRead() 依赖 imageformats 插件（gif/jpeg 等）；缺插件时导入会失败。
        QImageReader reader(source);
        const auto size = reader.size();
        if (!reader.canRead() || !size.isValid() || size.width() > 2048 || size.height() > 2048) {
            if (error)
                *error = QStringLiteral("图片无法解码，或尺寸超过 2048×2048。");
            return {};
        }
    }
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = input.errorString();
        return {};
    }
    const auto bytes = input.readAll();
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    const auto relative = QStringLiteral("media/") +
                          (image ? QStringLiteral("images/") : QStringLiteral("sounds/")) +
                          QString::fromLatin1(hash) + QLatin1Char('.') + suffix;
    QSaveFile output(root_ + QLatin1Char('/') + relative);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit()) {
        if (error)
            *error = output.errorString();
        return {};
    }
    return relative;
}

QString MediaService::defaultSound() const {
    const auto relative = QStringLiteral("media/sounds/default.wav");
    const auto path = root_ + QLatin1Char('/') + relative;
    if (QFileInfo::exists(path))
        return path;
    // 生成一个短提示音，不需要捆绑来源不明的音频资源。
    constexpr quint32 rate = 22050, samples = rate / 2, bytes = samples * 2;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + bytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << rate << quint32(rate * 2) << quint16(2)
        << quint16(16);
    out.writeRawData("data", 4);
    out << bytes;
    for (quint32 i = 0; i < samples; ++i) {
        const double t = double(i) / rate;
        const double envelope = std::sin(std::numbers::pi * double(i) / samples);
        out << qint16(10000 * envelope * std::sin(2 * std::numbers::pi * 660 * t));
    }
    return file.commit() ? path : QString{};
}
} // namespace qding
