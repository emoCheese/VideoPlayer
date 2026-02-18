#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "Decode/videoplayer.h"
#include "videosurface.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private:
    void initUI();
private slots:
    void on_btnPlay_clicked();

    void on_btnSelectVideo_clicked();

    void on_btnStop_clicked();

    void on_btnSelectRtsp_clicked();
private:
    Ui::MainWindow *ui;
    VideoPlayer* player = nullptr;
    VideoSurface* videoSurface = nullptr;
};
#endif // MAINWINDOW_H
