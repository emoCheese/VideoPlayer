#include "mainwindow.h"

#include <QApplication>
#include <qsurfaceformat.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 启用垂直同步（关键！）
    QSurfaceFormat format;
    format.setSwapInterval(1); // 1 = VSync on, 0 = VSync off
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    QSurfaceFormat::setDefaultFormat(format);

    MainWindow w;
    w.show();
    return a.exec();
}
