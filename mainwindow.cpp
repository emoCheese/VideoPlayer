#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>
#include <spdlog/spdlog.h>
#include <videowidget.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    connect(this, &MainWindow::frameReady,
            ui->videoWidget, &VideoWidget::onFrameReady);
    initUI();
}

MainWindow::~MainWindow()
{
    if (player)
        delete player;
    delete ui;
}

void MainWindow::initUI()
{
    ui->btnPlay->setVisible(false);
    ui->btnStop->setVisible(false);
}

void MainWindow::on_btnPlay_clicked()
{
    ui->btnPlay->setVisible(false);
    ui->btnStop->setVisible(true);
}


void MainWindow::on_btnSelectVideo_clicked()
{
    QString url =
        QFileDialog::getOpenFileName(
            nullptr,
            "选择视频文件",
            QStandardPaths::standardLocations(QStandardPaths::MoviesLocation).first(),
            "视频 (*.mkv *.mp4 *.*)"
            );
    spdlog::debug("打开视频: {}",  url.toStdString());

    player = new VideoPlayer(url.toStdString());
    // 需要先设置 videoWidget 和对应的 时钟 clock
    ui->videoWidget->setVideoPlayer(player);    
    player->start();

    // 启动时钟，控制拉帧
    player->startClock([this](std::shared_ptr<VideoFrame> frame_ptr){
        spdlog::debug("帧渲染 frame pts {}", frame_ptr->pts);
        emit frameReady(frame_ptr);
    });

    ui->btnStop->setVisible(true);
}


void MainWindow::on_btnStop_clicked()
{
    ui->btnStop->setVisible(false);
    ui->btnPlay->setVisible(true);
}

void MainWindow::on_btnSelectRtsp_clicked()
{

}
