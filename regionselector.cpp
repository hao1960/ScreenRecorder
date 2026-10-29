#include "regionselector.h"

#include <QColor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

// 创建遮罩及其子控件；this 作为父对象负责管理橡皮筋和按钮。
RegionSelector::RegionSelector(QWidget *parent)
    : QWidget(parent)
    ,m_band(QRubberBand::Rectangle, this)
    ,m_confirmButton(QStringLiteral("确认"),this)
    ,m_cancelButton(QStringLiteral("取消"),this)
{
    // 顶层、无边框、置顶：选区时覆盖主屏幕。
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // 允许 paintEvent 绘制带透明度的遮罩；Windows 上需配合无边框标志。
    setAttribute(Qt::WA_TranslucentBackground);
    // 设置半透明黑色样式；遮罩的实际填充还由 paintEvent 完成。
    setStyleSheet("background-color: rgba(0,0,0,100);");
    // 遮罩覆盖主屏幕；这里使用 Qt 的逻辑坐标。
    setGeometry(QGuiApplication::primaryScreen()->geometry());
    // 鼠标移到遮罩上时显示十字光标。
    setCursor(Qt::CrossCursor);
    // 让遮罩窗口取得键盘焦点，以便按 Esc 取消。
    setFocusPolicy(Qt::StrongFocus);

    // 橡皮筋只负责显示，不截走鼠标事件
    m_band.setAttribute(Qt::WA_TransparentForMouseEvents);
    // 按钮放在左上角；不抢键盘焦点，Esc 仍交给遮罩处理。
    m_confirmButton.setGeometry(20,20,80,32);
    m_cancelButton.setGeometry(110,20,80,32);
    m_confirmButton.setFocusPolicy(Qt::NoFocus);
    m_cancelButton.setFocusPolicy(Qt::NoFocus);
    // 只有拖出有效矩形后才显示确认和取消按钮。
    m_confirmButton.hide();
    m_cancelButton.hide();

    // 确认时把遮罩内的局部坐标换成屏幕坐标，再通知主窗口。
    connect(&m_confirmButton, &QPushButton::clicked,this,[this](){
        const QRect result(mapToGlobal(m_selection.topLeft()),m_selection.size());
        hide();
        emit regionConfirmed(result);
    });

    // 取消只关闭遮罩，不修改主窗口已保存的区域。
    connect(&m_cancelButton, &QPushButton::clicked,this,[this](){hide();});

}

// 每次重新选择时清除旧矩形和按钮，避免沿用上一次的状态。
void RegionSelector::beginSelection(){
    m_dragging =false;
    m_selection = QRect();
    m_band.hide();
    m_confirmButton.hide();
    m_cancelButton.hide();
    // 显示并激活遮罩，让后续鼠标和键盘事件由它接收。
    show();
    raise();
    activateWindow();
    setFocus();
}
// 用 alpha=100 的黑色填满窗口，保留后方桌面的可见性。
void RegionSelector::paintEvent(QPaintEvent *){
    QPainter painter(this);
    painter.fillRect(rect(),QColor(0,0,0,100));
}

// 鼠标左键按下：记录起点，开始新一轮拖拽。
void RegionSelector::mousePressEvent(QMouseEvent *event){
    if(event->button()!=Qt::LeftButton)
        return;

    m_dragging = true;
    m_origin = event->position().toPoint();
    m_confirmButton.hide();
    m_cancelButton.hide();
    // 从按下位置创建初始橡皮筋，随后由 mouseMoveEvent 调整大小。
    m_band.setGeometry(QRect(m_origin,QSize(1,1)));
    m_band.show();
}
// 拖拽中更新矩形；normalized 支持向任意方向拖动，intersected 限制在屏幕内。
void RegionSelector::mouseMoveEvent(QMouseEvent *event){
    if(!m_dragging)
        return;

    m_band.setGeometry(
        QRect(m_origin, event->position().toPoint()).normalized().intersected(rect())
    );
}
// 松开左键后保存矩形；过小的选区无效，有效选区等待用户确认。
void RegionSelector::mouseReleaseEvent(QMouseEvent *event){
    if(!m_dragging || event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    m_selection = QRect(m_origin, event->position().toPoint()).normalized().intersected(rect());

    if(m_selection.width() < 20 || m_selection.height() < 20){
        m_band.hide();
        return;
    }

    m_band.setGeometry(m_selection);
    m_confirmButton.show();
    m_cancelButton.show();
    // 把按钮提到橡皮筋上层，确保它们可见、可点击。
    m_confirmButton.raise();
    m_cancelButton.raise();
}
// Esc 等同取消；其他按键交给 QWidget 的默认处理。
void RegionSelector::keyPressEvent(QKeyEvent *event){

    if(event->key() == Qt::Key_Escape){
        hide();
        return;
    }

    QWidget::keyPressEvent(event);
}