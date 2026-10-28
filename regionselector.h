#ifndef REGIONSELECTOR_H
#define REGIONSELECTOR_H

#include<QWidget>
class QKeyEvent;

class RegionSelector:public QWidget
{
    Q_OBJECT

public:
    // explicit防止c++把一个QWidget*转换成RegionSelector对象，必须显式构造，防止隐式转换
    explicit RegionSelector(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;
};

#endif // REGIONSELECTOR_H
