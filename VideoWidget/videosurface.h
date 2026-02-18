#ifndef VIDEOSURFACE_H
#define VIDEOSURFACE_H

#include <QWidget>

class VideoSurface : public QWidget
{
    Q_OBJECT
public:
    explicit VideoSurface(QWidget *parent = nullptr);

protected:
    QPaintEngine* paintEngine() const override;

};

#endif // VIDEOSURFACE_H
