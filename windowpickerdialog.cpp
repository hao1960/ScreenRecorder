#include "windowpickerdialog.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

WindowPickerDialog::WindowPickerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("选择录制窗口"));
    resize(820, 480);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(QStringList{
        QStringLiteral("窗口标题"),
        QStringLiteral("进程"),
        QStringLiteral("客户区"),
        QStringLiteral("状态"),
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);

    m_refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    m_okButton = new QPushButton(QStringLiteral("选择"), this);
    m_okButton->setDefault(true);
    m_okButton->setEnabled(false);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addWidget(m_hintLabel);
    layout->addLayout(buttonLayout);

    connect(m_refreshButton, &QPushButton::clicked, this, &WindowPickerDialog::refresh);
    connect(m_okButton, &QPushButton::clicked, this, &WindowPickerDialog::acceptSelection);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &WindowPickerDialog::updateSelectionState);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        acceptSelection();
    });

    refresh();
}

void WindowPickerDialog::refresh()
{
    m_windows = enumerateTopLevelWindows();
    m_selected = {};
    fillTable();

    // 默认选中第一个可录制的窗口；全是最小化时保持不选。
    for (int row = 0; row < m_windows.size(); ++row) {
        if (!m_windows[row].minimized) {
            m_table->selectRow(row);
            m_table->setFocus();
            break;
        }
    }
    updateSelectionState();
}

void WindowPickerDialog::fillTable()
{
    m_table->clearContents();
    m_table->setRowCount(m_windows.size());

    for (int row = 0; row < m_windows.size(); ++row) {
        const WindowTarget &window = m_windows[row];

        auto *titleItem = new QTableWidgetItem(window.title);
        titleItem->setToolTip(window.title);
        auto *processItem = new QTableWidgetItem(window.processName);
        auto *sizeItem = new QTableWidgetItem(
            QStringLiteral("%1 × %2").arg(window.clientSize.width()).arg(window.clientSize.height()));
        auto *stateItem = new QTableWidgetItem(
            window.minimized ? QStringLiteral("已最小化") : QStringLiteral("可录制"));
        stateItem->setForeground(window.minimized
                                     ? QBrush(QColor(180, 120, 0))
                                     : QBrush(QColor(30, 140, 60)));

        if (window.minimized) {
            // 最小化窗口没有可用客户区：置灰并禁止选中。
            const Qt::ItemFlags flags =
                titleItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            titleItem->setFlags(flags);
            processItem->setFlags(flags);
            sizeItem->setFlags(flags);
            stateItem->setFlags(flags);
        }

        m_table->setItem(row, 0, titleItem);
        m_table->setItem(row, 1, processItem);
        m_table->setItem(row, 2, sizeItem);
        m_table->setItem(row, 3, stateItem);
    }

    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_hintLabel->setText(QStringLiteral("共 %1 个窗口；最小化的窗口需要先恢复才能录制。")
                             .arg(m_windows.size()));
}

void WindowPickerDialog::updateSelectionState()
{
    const int row = m_table->currentRow();
    const bool valid = row >= 0 && row < m_windows.size() && !m_windows[row].minimized;
    m_okButton->setEnabled(valid);

    if (valid) {
        const WindowTarget &window = m_windows[row];
        m_hintLabel->setText(QStringLiteral("将录制：%1（%2 × %3）")
                                 .arg(window.title)
                                 .arg(window.clientSize.width())
                                 .arg(window.clientSize.height()));
    } else if (m_windows.isEmpty()) {
        m_hintLabel->setText(QStringLiteral("没有找到可选择的窗口。"));
    } else {
        m_hintLabel->setText(QStringLiteral("请选择一个未最小化的窗口。"));
    }
}

void WindowPickerDialog::acceptSelection()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_windows.size())
        return;
    if (m_windows[row].minimized) {
        m_hintLabel->setText(QStringLiteral("该窗口已最小化，请先恢复再选择。"));
        return;
    }

    m_selected = m_windows[row];
    accept();
}
