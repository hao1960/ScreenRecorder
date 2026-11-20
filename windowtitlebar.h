#ifndef WINDOWTITLEBAR_H
#define WINDOWTITLEBAR_H
#include <QFrame>
#include <QSizeGrip>

class QMouseEvent;
// 无边框窗口的自定义标题栏：空白处可以拖动，双击最大化/还原
class WindowTitleBar:public QFrame
{
    Q_OBJECT

public:
    explicit WindowTitleBar(QWidget *parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
};

// 无边框窗口右下角的缩放手柄：把缩放交还给系统，保留贴边和高 DPI 行为。
class WindowResizeGrip : public QSizeGrip
{
    Q_OBJECT

public:
    explicit WindowResizeGrip(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
};

#endif // WINDOWTITLEBAR_H
