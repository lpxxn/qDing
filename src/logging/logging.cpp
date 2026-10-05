#include "logging/logging.h"
#include <QDir>
#include <QMessageLogContext>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace qding {
void initializeLogging(const QString &directory) {
    QDir().mkpath(directory);
#ifdef Q_OS_WIN
    const auto path = (directory + QStringLiteral("/qding.log")).toStdWString();
#else
    const auto path = (directory + QStringLiteral("/qding.log")).toStdString();
#endif
    auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, 5 * 1024 * 1024, 5);
    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("qding", spdlog::sinks_init_list{file, console});
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%Y-%m-%dT%H:%M:%S.%e%z [%l] [thread %t] %v");
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::warn);
    qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext &context, const QString &message) {
            const auto text = message.toUtf8();
            const auto category = context.category ? context.category : "qt";
            switch (type) {
            case QtDebugMsg:
                spdlog::debug("qt {}: {}", category, text.constData());
                break;
            case QtInfoMsg:
                spdlog::info("qt {}: {}", category, text.constData());
                break;
            case QtWarningMsg:
                spdlog::warn("qt {}: {}", category, text.constData());
                break;
            case QtCriticalMsg:
                spdlog::error("qt {}: {}", category, text.constData());
                break;
            case QtFatalMsg:
                spdlog::critical("qt {}: {}", category, text.constData());
                spdlog::default_logger()->flush();
                break;
            }
        });
}
} // namespace qding
