#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
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
    qInfo() << "打开视频: " << url;

    player = new VideoPlayer(url.toStdString());
    player->start();



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
