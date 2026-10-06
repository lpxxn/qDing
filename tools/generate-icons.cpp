// 可选的素材生成工具：由 SVG 生成高 DPI PNG / Windows ICO / macOS iconset。
// 正常构建直接使用已提交的 .ico/.icns，无需 Python 或第三方图像依赖。
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTextStream error(stderr);
    const auto arguments = app.arguments();
    if (arguments.size() != 3) {
        error << "Usage: qding_icon_generator <source.svg> <output directory>\n";
        return 1;
    }
    QSvgRenderer renderer(arguments[1]);
    if (!renderer.isValid()) {
        error << "Invalid SVG\n";
        return 1;
    }
    QDir output(arguments[2]);
    if (!output.mkpath(QStringLiteral("qDing.iconset")))
        return 1;
    QImage master(1024, 1024, QImage::Format_ARGB32_Premultiplied);
    master.fill(Qt::transparent);
    {
        QPainter painter(&master);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
    }
    const auto imageAt = [&](int size) {
        return master.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    };
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) {
        if (!imageAt(size).save(output.filePath(QStringLiteral("qding-%1.png").arg(size))))
            return 1;
    }
    for (int size : {16, 32, 128, 256, 512}) {
        const auto stem = QStringLiteral("qDing.iconset/icon_%1x%1").arg(size);
        if (!imageAt(size).save(output.filePath(stem + QStringLiteral(".png"))) ||
            !imageAt(size * 2).save(output.filePath(stem + QStringLiteral("@2x.png"))))
            return 1;
    }
    // ICO 的目录使用小端；每个条目存 PNG，保留完整 alpha。256 的尺寸字段用 0 表示。
    QList<QByteArray> frames;
    const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    for (int size : sizes) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!imageAt(size).save(&buffer, "PNG"))
            return 1;
        frames.append(bytes);
    }
    QFile ico(output.filePath(QStringLiteral("qding.ico")));
    if (!ico.open(QIODevice::WriteOnly))
        return 1;
    QDataStream stream(&ico);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(frames.size());
    quint32 offset = 6 + 16 * frames.size();
    for (int i = 0; i < frames.size(); ++i) {
        const quint8 dimension = sizes[i] == 256 ? 0 : sizes[i];
        stream << dimension << dimension << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(frames[i].size()) << offset;
        offset += frames[i].size();
    }
    for (const auto &frame : frames)
        if (stream.writeRawData(frame.constData(), frame.size()) != frame.size())
            return 1;
    return stream.status() == QDataStream::Ok ? 0 : 1;
}
