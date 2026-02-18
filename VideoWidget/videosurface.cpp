#include "videosurface.h"

VideoSurface::VideoSurface(QWidget *parent)
    : QWidget{parent}
{
    setAttribute(Qt::WA_NativeWindow);          // 强制 QWidget 拥有真实系统窗口
    setAttribute(Qt::WA_PaintOnScreen);         // 禁止 Qt backing store（Qt 不缓存、不合成）
    setAttribute(Qt::WA_NoSystemBackground);    // Qt 不擦背景（避免闪烁 / 黑帧）
}

QPaintEngine *VideoSurface::paintEngine() const {
    return nullptr; // 禁止 Qt 绘制
}
