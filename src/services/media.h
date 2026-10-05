#pragma once
#include <QString>
namespace qding {
class MediaService final {
public:
    explicit MediaService(QString dataRoot);
    QString importFile(const QString &source, bool image, QString *error) const;
    QString resolve(const QString &relativePath) const;
    QString defaultSound() const;
    const QString &root() const { return root_; }

private:
    QString root_;
};
} // namespace qding
