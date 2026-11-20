#include "regionselector.h"

#include <QColor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QHBoxLayout>
#include <QPoint>
// 创建遮罩及其子控件；this 作为父对象负责管理橡皮筋和按钮。
RegionSelector::RegionSelector(QWidget *parent)
    : QWidget(parent)
    ,m_band(QRubberBand::Rectangle, this)
    ,m_topBar(this)
    ,m_confirmButton(QStringLiteral("确认"),&m_topBar)// 父对象改为m_topBar
    ,m_cancelButton(QStringLiteral("取消"),&m_topBar)
    ,m_sizeLabel(this)
{
    // 顶层、无边框、置顶：选区时覆盖主屏幕。
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // 允许 paintEvent 绘制带透明度的遮罩；Windows 上需配合无边框标志。
    setAttribute(Qt::WA_TranslucentBackground);
    // 遮罩覆盖主屏幕；这里使用 Qt 的逻辑坐标。
    setGeometry(QGuiApplication::primaryScreen()->geometry());
    // 鼠标移到遮罩上时显示十字光标。
    setCursor(Qt::CrossCursor);
    // 让遮罩窗口取得键盘焦点，以便按 Esc 取消。
    setFocusPolicy(Qt::StrongFocus);

    // 橡皮筋只负责显示，不截走鼠标事件
    m_band.setAttribute(Qt::WA_TransparentForMouseEvents);
    m_confirmButton.setObjectName(QStringLiteral("confirmButton"));
    m_cancelButton.setObjectName(QStringLiteral("cancelButton"));
    m_confirmButton.setMinimumSize(80, 32);
    m_cancelButton.setMinimumSize(80, 32);
    m_confirmButton.setFocusPolicy(Qt::NoFocus);
    m_cancelButton.setFocusPolicy(Qt::NoFocus);

    // 顶部工具条：确认/取消居中；以后加控件直接往 topBarLayout 里 addWidget。
    m_topBar.setObjectName(QStringLiteral("selectionToolbar"));
    m_topBar.setFrameShape(QFrame::NoFrame);
    m_topBar.setStyleSheet(QStringLiteral(
        "QFrame#selectionToolbar {"
        " background-color: rgba(30, 30, 32, 215);"
        " border: 1px solid rgba(255, 255, 255, 35);"
        " border-radius: 10px;"
        "}"
        "QPushButton {"
        " color: white; border: none; padding: 6px 18px;"
        " border-radius: 6px; font-size: 13px;"
        "}"
        "QPushButton#confirmButton { background-color: #2f7cf6; }"
        "QPushButton#confirmButton:hover { background-color: #4a8ef8; }"
        "QPushButton#cancelButton { background-color: rgba(255, 255, 255, 28); }"
        "QPushButton#cancelButton:hover { background-color: rgba(255, 255, 255, 50); }"));
    // 创建一个水平布局，把布局安装到m_topBar上
    auto *topBarLayout = new QHBoxLayout(&m_topBar);
    topBarLayout->setContentsMargins(10, 6, 10, 6);
    topBarLayout->setSpacing(8);
    topBarLayout->addWidget(&m_confirmButton);
    topBarLayout->addWidget(&m_cancelButton);
    m_topBar.hide();
    // 尺寸提示不接受鼠标时间，拖拽经过它时仍由遮罩处理
    m_sizeLabel.setAttribute(Qt::WA_TransparentForMouseEvents);
    m_sizeLabel.setAlignment(Qt::AlignCenter);
    m_sizeLabel.setStyleSheet(QStringLiteral("QLabel { background-color: rgba(0,0,0,180); color: white;"
                                             " padding: 3px 8px; border-radius: 3px; }"));
    m_sizeLabel.hide();
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
    m_topBar.hide();
    m_sizeLabel.hide();
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
    m_topBar.hide();
    m_sizeLabel.hide();
    // 从按下位置创建初始橡皮筋，随后由 mouseMoveEvent 调整大小。
    const QRect selection =
        QRect(m_origin, event->position().toPoint()).normalized().intersected(rect());
    m_band.setGeometry(selection);
    // 太小的选区在松开时会被判无效，这里也不显示尺寸，避免误导。
    if (selection.width() >= 20 && selection.height() >= 20)
        updateSizeLabel(selection);
    else
        m_sizeLabel.hide();
    m_band.show();
}
// 拖拽中更新矩形；normalized 支持向任意方向拖动，intersected 限制在屏幕内。
void RegionSelector::mouseMoveEvent(QMouseEvent *event){
    if(!m_dragging)
        return;

    const QRect selection = QRect(m_origin,event->position().toPoint()).normalized().intersected(rect());
    m_band.setGeometry(selection);
    if(selection.width()>=20 && selection.height()>=20)
        updateSizeLabel(selection);
    else
        m_sizeLabel.hide();
}
// 松开左键后保存矩形；过小的选区无效，有效选区等待用户确认。
void RegionSelector::mouseReleaseEvent(QMouseEvent *event){
    if(!m_dragging || event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    m_selection = QRect(m_origin, event->position().toPoint()).normalized().intersected(rect());

    if(m_selection.width() < 20 || m_selection.height() < 20){
        m_band.hide();
        m_sizeLabel.hide();
        return;
    }

    m_band.setGeometry(m_selection);
    m_confirmButton.show();
    m_cancelButton.show();
    positionTopBar();
    m_topBar.show();
    m_topBar.raise();
    updateSizeLabel(m_selection);
    // 极小选区与尺寸标签重叠时，优先保证按钮可点
    m_topBar.raise();
}
// Esc 等同取消；其他按键交给 QWidget 的默认处理。
void RegionSelector::keyPressEvent(QKeyEvent *event){

    if(event->key() == Qt::Key_Escape){
        hide();
        return;
    }

    QWidget::keyPressEvent(event);
}

void RegionSelector::updateSizeLabel(const QRect &selection)
{
    // 只显示 Qt 逻辑像素；高 DPI 下的输出尺寸由主窗口按 DPR 换算。
    m_sizeLabel.setText(QStringLiteral("%1 × %2")
                            .arg(selection.width()).arg(selection.height()));
    m_sizeLabel.adjustSize();

    // 优先放在选区右下方：x 靠右，y 在外侧下方；下方放不下时移到选区内部底部。
    QPoint pos(selection.right() - m_sizeLabel.width() - 6,
               selection.bottom() + 6);
    if (pos.y() + m_sizeLabel.height() > height())
        pos.setY(selection.bottom() - m_sizeLabel.height() - 6);
    if (pos.x() < 6)
        pos.setX(6);
    if (pos.y() < 6)
        pos.setY(6);
    m_sizeLabel.move(pos);
    m_sizeLabel.show();
    m_sizeLabel.raise();
}
// topBar居中函数
void RegionSelector::positionTopBar()
{
    // 工具条宽度由布局决定；居中后固定在顶部，后续加控件会自动重新计算。
    m_topBar.adjustSize();// 向布局要一个sizeHint(),按内容自动把工具条调整到合适大小
    const int x = (width() - m_topBar.width()) / 2;// width=this->width=整个遮罩的宽度，后一个就是工具条宽度
    m_topBar.move(qMax(0, x), 12);// 最大值防止工具条宽大于遮罩宽，12代表距离屏幕顶部12像素
}