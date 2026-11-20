#ifndef WINDOWENUMERATOR_H
#define WINDOWENUMERATOR_H

#include <QSize>
#include <QString>
#include <QVector>
#include <QtGlobal>

// 一个可录制的顶层窗口。hwnd 用整数保存，避免在这个头文件里引入 windows.h。
struct WindowTarget
{
    quintptr hwnd = 0;
    QString title;
    QString processName;
    QSize clientSize;
    bool minimized = false;
};

// 枚举当前可见的顶层窗口，过滤工具窗口、空标题、cloaked 窗口和本程序窗口。
QVector<WindowTarget> enumerateTopLevelWindows();

#endif // WINDOWENUMERATOR_H
