#ifndef REGIONSELECTOR_H
#define REGIONSELECTOR_H

#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QRubberBand>
#include<QWidget>
class QKeyEvent;

class RegionSelector:public QWidget
{
    Q_OBJECT

public:
    // explicit 禁止 QWidget* 隐式转换为 RegionSelector；构造时要明确写出类名。
    explicit RegionSelector(QWidget *parent = nullptr);
    void beginSelection();

// 用户确认后，通过信号把选区的屏幕坐标交给主窗口。
signals:
    void regionConfirmed(const QRect &region);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRubberBand m_band;
    QPushButton m_confirmButton;
    QPushButton m_cancelButton;
    QPoint m_origin;
    QRect m_selection;
    bool m_dragging = false;
};

#endif // REGIONSELECTOR_H
