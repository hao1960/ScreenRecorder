#include "regionselector.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>

RegionSelector::RegionSelector(QWidget *parent): QWidget(parent){
    // 设置窗口类型，Window是普通顶层窗口，framlessWindowHint是去掉边框，WindowStaysOnTopHint让遮罩保持在其他普通窗口之上
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // 允许窗口绘制半透明区域；
    setAttribute(Qt::WA_TranslucentBackground);
    // 样式是半透明黑色
    setStyleSheet("background-color: rgba(0,0,0,100);");
    // 把遮罩放在主屏幕位置和大小
    setGeometry(QGuiApplication::primaryScreen()->geometry());
    // 鼠标移动到遮罩上显式十字光标
    setCursor(Qt::CrossCursor);

}

void RegionSelector::keyPressEvent(QKeyEvent *event){

    if(event->key() == Qt::Key_Escape){
        hide();
        return;
    }

    QWidget::keyPressEvent(event);
}