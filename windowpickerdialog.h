#ifndef WINDOWPICKERDIALOG_H
#define WINDOWPICKERDIALOG_H

#include <QDialog>

#include "windowenumerator.h"

class QLabel;
class QPushButton;
class QTableWidget;

class WindowPickerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WindowPickerDialog(QWidget *parent = nullptr);

    // 用户确认后返回选中的窗口；取消时保持默认值。
    WindowTarget selectedWindow() const { return m_selected; }

private slots:
    void refresh();
    void acceptSelection();

private:
    void fillTable();
    void updateSelectionState();

    QVector<WindowTarget> m_windows;
    WindowTarget m_selected;
    QTableWidget *m_table = nullptr;
    QLabel *m_hintLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_okButton = nullptr;
};

#endif // WINDOWPICKERDIALOG_H
