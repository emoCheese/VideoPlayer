#include "mainwindow.h"

#include <QApplication>
#include <qsurfaceformat.h>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

void static initLogger()
{
    auto logger = spdlog::stdout_color_mt("player");
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("logger initialized");
    spdlog::error("TEST ERROR");
}

int main(int argc, char *argv[])
{
    initLogger();
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
