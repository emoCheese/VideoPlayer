#include "mainwindow.h"

#include <QApplication>
#include <qsurfaceformat.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

static void initLogger()
{
    auto logger = spdlog::stdout_color_mt("dev");

    logger->set_pattern(
        "[%H:%M:%S] " // 时间
        "[%^%l%$] "  // 日志等级
        "[%s:%#] "  // 输出文件名和行号
        "%v"
        );

#ifndef NDEBUG
    logger->set_level(spdlog::level::trace);
#else
    logger->set_level(spdlog::level::off);
#endif

    spdlog::set_default_logger(logger);

    SPDLOG_INFO("Logger initialized (Debug mode)");
}


struct VideoFrame;
// 声明元类型
Q_DECLARE_METATYPE(std::shared_ptr<VideoFrame>)

static void registerMyType()
{
    // 注册元类型（只调用一次）
    qRegisterMetaType<std::shared_ptr<VideoFrame>>("std::shared_ptr<VideoFrame>");
}

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    initLogger();
    registerMyType();
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
