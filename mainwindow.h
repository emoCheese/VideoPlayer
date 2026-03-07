#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "Decode/videoplayer.h"
#include "framequeue.h"
#include <memory>

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

    static void frameCallback(std::shared_ptr<VideoFrame> frame, void* ctx)
    {
        if (!ctx) return;
        auto mainwindow = static_cast<MainWindow*>(ctx);
        mainwindow->emitFrameReady(frame);
    }

    inline void emitFrameReady(std::shared_ptr<VideoFrame> f) { emit frameReady(f); }
signals:
    void frameReady(std::shared_ptr<VideoFrame>);
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
};
#endif // MAINWINDOW_H
