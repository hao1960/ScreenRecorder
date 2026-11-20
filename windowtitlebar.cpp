#include "windowtitlebar.h"
#include <QMouseEvent>
#include <QWindow>

WindowTitleBar::WindowTitleBar(QWidget *parent):QFrame(parent)
{
    setObjectName(QStringLiteral("windowTitleBar"));
    setFixedHeight(54);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
}
void WindowTitleBar::mousePressEvent(QMouseEvent *event)
{
    // 把拖动交还给windows，保留贴边、多屏和系统手感
    if(event->button()==Qt::LeftButton && window() && window()->windowHandle()){
        window()->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void WindowTitleBar::mouseDoubleClickEvent(QMouseEvent *event){
    if(event->button() == Qt::LeftButton && window()){
        if(window()->isMaximized())
            window()->showNormal();
        else
            window()->showMaximized();
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}

WindowResizeGrip::WindowResizeGrip(QWidget *parent)
    : QSizeGrip(parent)
{
    setFixedSize(16, 16);
    setCursor(Qt::SizeFDiagCursor);
}

void WindowResizeGrip::mousePressEvent(QMouseEvent *event)
{
    // 不用 QSizeGrip 自己算几何，交给系统进入原生缩放循环。
    if (event->button() == Qt::LeftButton && window() && window()->windowHandle()) {
        if (window()->windowHandle()->startSystemResize(Qt::RightEdge | Qt::BottomEdge)) {
            event->accept();
            return;
        }
    }
    QSizeGrip::mousePressEvent(event);
}