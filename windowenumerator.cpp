#include "windowenumerator.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <utility>

namespace {

QString fromWide(const wchar_t *text)
{
    return QString::fromWCharArray(text ? text : L"");
}

// 取进程可执行文件名；权限不足时返回 "?"，不影响窗口枚举。
QString processName(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return QStringLiteral("?");
    wchar_t path[1024]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    QString name = QStringLiteral("?");
    if (QueryFullProcessImageNameW(process, 0, path, &length)) {
        const wchar_t *base = std::wcsrchr(path, L'\\');
        name = fromWide(base ? base + 1 : path);
    }
    CloseHandle(process);
    return name;
}

// UWP 应用会留下不可见的“克隆”窗口；DWM 标记为 cloaked 的窗口不应出现在列表里。
bool isCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED,
                                           &cloaked, sizeof(cloaked))) && cloaked != 0;
}

BOOL CALLBACK enumWindow(HWND hwnd, LPARAM lParam)
{
    auto *windows = reinterpret_cast<QVector<WindowTarget> *>(lParam);

    // 只要用户看得见、有标题、不是工具窗口的顶层窗口。
    if (!IsWindowVisible(hwnd) || isCloaked(hwnd))
        return TRUE;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;

    wchar_t title[512]{};
    const int titleLength = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (titleLength <= 0)
        return TRUE;

    RECT rect{};
    GetWindowRect(hwnd, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return TRUE;

    RECT client{};
    GetClientRect(hwnd, &client);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    // 不把应用自己的窗口列进候选。
    if (pid == GetCurrentProcessId())
        return TRUE;

    const bool minimized = IsIconic(hwnd) != FALSE;
    // 非最小化但客户区为空的窗口没有可录制的画面。
    if (!minimized && (client.right <= client.left || client.bottom <= client.top))
        return TRUE;

    WindowTarget target;
    target.hwnd = reinterpret_cast<quintptr>(hwnd);
    target.title = fromWide(title);
    target.processName = processName(pid);
    target.clientSize = QSize(client.right - client.left, client.bottom - client.top);
    target.minimized = minimized;
    windows->append(std::move(target));
    return TRUE;
}

} // namespace

QVector<WindowTarget> enumerateTopLevelWindows()
{
    QVector<WindowTarget> windows;
    EnumWindows(enumWindow, reinterpret_cast<LPARAM>(&windows));

    // 按进程名、标题排序，方便和任务栏/任务管理器对照。
    std::sort(windows.begin(), windows.end(), [](const WindowTarget &left, const WindowTarget &right) {
        if (left.processName != right.processName)
            return left.processName < right.processName;
        return left.title < right.title;
    });
    return windows;
}
