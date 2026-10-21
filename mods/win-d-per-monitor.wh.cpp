// ==WindhawkMod==
// @id              win-d-per-monitor
// @name            Win+D per monitor(show desktop)
// @name:ar         Win+D لكل شاشة
// @description     Press Win+D to only manage the windows on the monitor where the mouse is located.
// @description:zh-CN   按下Win+D时 只最小化/还原鼠标所在显示器的窗口
// @description:ar  عند الضغط على Win+D تُخفى نوافذ الشاشة التي عليها المؤشر فقط ثم تُعاد، وتبقى الشاشات الأخرى كما هي.
// @version         1.5.260416-shihab.4
// @author          easyatm
// @github          https://github.com/easyatm
// @include         explorer.exe
// @compilerOptions -luser32 -lDwmapi
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- ignoreTopmostNoTitleBarWindows: true
  $name: Ignore topmost tool windows without title bar
  $description: Ignore the topmost tool windows without title bar (always on top tool windows)
  $description:zh-CN: 忽略最顶部且无标题栏的工具窗口（始终在顶部的工具窗口）
  $name:ar: تجاهل الأدوات العائمة بلا شريط عنوان
  $description:ar: لا تُخفى النوافذ الصغيرة التي تبقى دائماً في المقدمة وليس لها شريط عنوان
- minWindowSize: 100
  $name: Minimum window size
  $description: Minimum width or height for windows to be processed (pixels)
  $description:zh-CN: 处理窗口的最小宽度或高度（像素）
  $name:ar: أصغر حجم للنافذة
  $description:ar: النوافذ التي عرضها أو ارتفاعها أقل من هذا الرقم (بالبكسل) لا تُخفى
  $_advanced: true
- ignoreRules:
  - - title: ""
      $name: Window title
      $description: Window title to ignore
      $description:zh-CN: 要忽略的窗口标题
      $name:ar: عنوان النافذة
      $description:ar: عنوان النافذة التي لا تُخفى
    - className: ""
      $name: Window class name
      $description: Window class name to ignore
      $description:zh-CN: 要忽略的窗口类名
      $name:ar: اسم صنف النافذة
      $description:ar: الاسم البرمجي لصنف النافذة التي لا تُخفى
  $_hidden: true
  $name: Custom ignored windows
  $description: Custom list of windows to ignore. Fill at least one field per entry; an empty field matches anything.
  $description:zh-CN: 自定义忽略的窗口列表，每条规则至少填一项，留空的项匹配任意值
  $name:ar: نوافذ لا تُخفى
  $description:ar: قائمة بنوافذ تبقى ظاهرة عند الضغط على Win+D. املأ حقلاً واحداً على الأقل في كل سطر، والحقل الفارغ يطابق أي نافذة.
  $_advanced: true

*/
// ==/WindhawkModSettings==

// ==WindhawkModReadme==
/*
# Win+D per monitor

When you press win+d, it only minimizes/restores the windows on the monitor
where the mouse cursor is located.

按下win+d时,只最小化/还原鼠标所在监视器上的窗口

## Changelog

### 2026-04-16 (v1.5.260416)
- Fixed repeated invocation of touchpad gesture by adding debounce logic to ignore redundant calls
- 修复触摸板手势重复进入问题，添加防抖逻辑忽略连续重复调用

Known issues:
- Touchpad swipe-down can minimize all windows and show the desktop, but swipe-up does not trigger this function, so windows cannot be restored via gesture — swipe down again to toggle
- 已知问题：触摸板下滑可最小化窗口显示桌面，但上滑不会进入此函数，无法通过手势还原窗口，需再次下滑切换

Fixes:
- https://github.com/ramensoftware/windhawk-mods/issues/3794

### 2026-04-15 (v1.4.260415)
- Switched hook target from `_HandleGlobalHotkey` to `_RaiseDesktop`, enabling per-monitor show desktop for both Win+D hotkey and touchpad gestures
- 将 Hook 目标从 `_HandleGlobalHotkey` 改为 `_RaiseDesktop`，同时支持 Win+D 快捷键和触摸板手势的按显示器显示桌面功能

Fixes:
- https://github.com/ramensoftware/windhawk-mods/issues/3794

### 2026-03-30 (v1.3.260330)
- Improved window control: try ShowWindowAsync first, then fall back to PostMessage (WM_SYSCOMMAND) if it fails
- 改进窗口控制：优先尝试 ShowWindowAsync，失败时回退到 PostMessage（WM_SYSCOMMAND）
- Added custom ignored windows support by class name or window title
- 新增自定义忽略窗口支持（按类名或窗口标题）

Fixes:
- https://github.com/ramensoftware/windhawk-mods/issues/2709

### 2025-08-11 (v1.1.20250811)
- Added option to ignore topmost tool windows without title bar during Win+D operation
- 新增忽略置顶且无标题栏的工具窗口选项，在Win+D操作时保持这类窗口可见

- Added configurable minimum window size setting
- 新增可配置的最小窗口尺寸设置

Fixes:
- https://github.com/ramensoftware/windhawk/discussions/597
- https://github.com/ramensoftware/windhawk-mods/issues/2056


*/
// ==/WindhawkModReadme==

// Adapted for Shihab Alaq (based on win-d-per-monitor 1.5.260416 by easyatm, MIT).
// Changes:
// - Window list is a snapshot (EnumWindows) instead of a GetNextWindow walk, so a
//   window closing or moving mid-walk cannot end or loop the walk.
// - Hung windows are not touched with synchronous calls (ShowOwnedPopups,
//   SetWindowPos, AttachThreadInput), and SetWindowPos on other windows is async,
//   so a frozen app cannot freeze explorer.
// - Remembered windows are checked against their process id (handle reuse), dead
//   ones are pruned, and the per-monitor lists are keyed by the monitor device name,
//   which survives display changes (HMONITOR values do not).
// - The debounce tick was an int (broke after 24.8 days of uptime); calls that
//   overlap or re-enter are skipped; settings and state are guarded by a lock.
// - Class and title buffers are always terminated; strings are UTF-8, so non-Latin
//   titles in ignore rules no longer collapse to "???" and match each other.
// - Minimum window size is clamped; area is computed without int overflow.
// - Wh_ModUninit shows popups this mod hid whose owner is still open.

#include <dwmapi.h>
#include <errhandlingapi.h>
#include <format>
#include <list>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <windhawk_api.h>
#include <windhawk_utils.h>
#include <windows.h>
#include <winsvc.h>

// 将ANSI字符串转换为Unicode宽字符串
static std::wstring ansi_unicode(std::string_view str)
{
    if (str.empty())
        return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    if (size_needed <= 0)
        return L"";
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// 将Unicode宽字符串转换为ANSI字符串
static std::string unicode_ansi(const wchar_t *wstr)
{
    if (!wstr || !wstr[0])
        return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 1)
        return "";
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size_needed, nullptr, nullptr);
    result.resize(size_needed - 1);
    return result;
}

using namespace std;
#define log(...) Wh_Log(L"%s", ansi_unicode(format(__VA_ARGS__)).c_str());

// 自定义忽略规则（标题+类名，任一为空则只判断另一项）
struct IgnoreRule
{
    std::string title;
    std::string className;
};

// 设置变量
struct
{
    bool ignoreTopmostNoTitleBarWindows;
    int minWindowSize;
    std::vector<IgnoreRule> ignoreRules;
} g_settings;

// Guards g_settings and the remembered windows: the hook runs on explorer's tray
// thread, settings changes and uninit on another thread.
std::mutex g_stateMutex;

#define HOTKEY_ID_WIN_D 0x201
class WindShowDesktop
{
    struct WndInfo
    {
        HWND wnd = nullptr;
        std::string cls;
        HWND ownerWnd = nullptr;
        std::string title; // 窗口标题
        DWORD pid = 0;
    };
    // Keyed by monitor device name: HMONITOR values change when displays change.
    inline static std::map<std::wstring, std::list<WndInfo>> mapWnd;

    // Still the same window we saw (handles of closed windows get reused).
    static bool isSameWindow(const WndInfo &info)
    {
        if (!::IsWindow(info.wnd))
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(info.wnd, &pid);
        return pid == info.pid;
    }

    // 激活指定窗口
    static void activeWnd(HWND hWnd)
    {
        if (hWnd == nullptr)
            return;

        // Attached input queues make these calls wait on the other thread, so a
        // hung window would freeze explorer.
        if (!::IsWindow(hWnd) || IsHungAppWindow(hWnd))
            return;

        auto hForeWnd = GetForegroundWindow();
        auto dwCurId = GetCurrentThreadId();
        auto dwForeId = hForeWnd ? GetWindowThreadProcessId(hForeWnd, nullptr) : 0;
        bool attached = dwForeId && dwForeId != dwCurId && !IsHungAppWindow(hForeWnd) &&
                        AttachThreadInput(dwCurId, dwForeId, TRUE);
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetForegroundWindow(hWnd);
        if (attached)
            AttachThreadInput(dwCurId, dwForeId, FALSE);
    }

    // 获取桌面窗口句柄
    static HWND getDesktopWnd()
    {
        HWND hWnd = nullptr;

        do
        {
            HWND hWnd = ::FindWindowA("Progman", "Program Manager");
            if (::FindWindowExA(hWnd, nullptr, "SHELLDLL_DefView", nullptr) != nullptr)
            {
                return hWnd;
            }

            break;
        } while (true);

        hWnd = nullptr;
        do
        {
            hWnd = ::FindWindowExA(nullptr, hWnd, "WorkerW", nullptr);
            if (hWnd == nullptr)
                break;

            if (::FindWindowExA(hWnd, nullptr, "SHELLDLL_DefView", nullptr) != nullptr)
                return hWnd;
        } while (true);
        return nullptr;
    }

    // 获取当前鼠标所在的显示器
    static HMONITOR getCurrentMonitor()
    {
        POINT pos = {0};
        if (!GetCursorPos(&pos))
        {
            return nullptr;
        }
        return MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST);
    }

    static std::wstring getMonitorName(HMONITOR hMonitor)
    {
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(hMonitor, &info))
            return L"";
        info.szDevice[ARRAYSIZE(info.szDevice) - 1] = L'\0';
        return info.szDevice;
    }

    static BOOL CALLBACK collectWindow(HWND hWnd, LPARAM lParam)
    {
        reinterpret_cast<std::vector<HWND> *>(lParam)->push_back(hWnd);
        return TRUE;
    }

    // Forget windows that were closed, and lists left empty (e.g. a removed monitor).
    static void pruneDeadWindows()
    {
        for (auto mit = mapWnd.begin(); mit != mapWnd.end();)
        {
            mit->second.remove_if([](const WndInfo &info) { return !isSameWindow(info); });
            if (mit->second.empty())
                mit = mapWnd.erase(mit);
            else
                ++mit;
        }
    }

    // 检测是否为Win10后台应用的隐藏窗口
    static bool isInvisibleWin10BackgroundAppWindow(HWND hWnd)
    {
        int cloakedVal;
        HRESULT hRes = DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &cloakedVal, sizeof(cloakedVal));
        if (hRes != S_OK)
            cloakedVal = 0;
        return cloakedVal != 0;
    }

    // 显示指定显示器上的拥有者窗口
    static bool showOwnerWnd(const std::wstring &monitorName, bool isActive)
    {
        auto &listWnd = mapWnd[monitorName];
        HWND hLastWnd = nullptr;
        for (auto it = listWnd.begin(); it != listWnd.end();)
        {
            auto curIt = it++;
            if (curIt->ownerWnd)
            {
                if (::IsWindow(curIt->ownerWnd) && !IsHungAppWindow(curIt->ownerWnd))
                    ShowOwnedPopups(curIt->ownerWnd, true);
                hLastWnd = curIt->wnd;
                listWnd.erase(curIt);
            }
        }
        if (hLastWnd && isActive)
            activeWnd(hLastWnd);
        return hLastWnd != nullptr;
    }

    // 获取窗口类名
    static std::string getWindowClassName(HWND hwnd)
    {
        WCHAR className[256] = {};
        if (GetClassNameW(hwnd, className, ARRAYSIZE(className)) <= 0)
            return "";
        className[ARRAYSIZE(className) - 1] = L'\0';
        return unicode_ansi(className);
    }
    static std::string getWindowTitleName(HWND hwnd)
    {
        WCHAR windowTitle[256] = {};
        if (GetWindowTextW(hwnd, windowTitle, ARRAYSIZE(windowTitle)) <= 0)
            return "";
        windowTitle[ARRAYSIZE(windowTitle) - 1] = L'\0';
        return unicode_ansi(windowTitle);
    }

    // 窗口最小化/还原：优先 ShowWindowAsync，失败后回退到 PostMessage
    static BOOL controlWindow(HWND hwnd, bool restore)
    {
        if (IsWindowEnabled(hwnd))
        {
            return ::PostMessage(hwnd, WM_SYSCOMMAND, restore ? SC_RESTORE : SC_MINIMIZE, 0);
        }

        if (::ShowWindowAsync(hwnd, restore ? SW_RESTORE : SW_MINIMIZE))
        {
            return TRUE;
        }

        log("ShowWindowAsync failed for 0x{:x}, falling back to PostMessage", (uintptr_t)hwnd);
        return ::PostMessage(hwnd, WM_SYSCOMMAND, restore ? SC_RESTORE : SC_MINIMIZE, 0);
    }

    // 判断是否应该忽略此窗口（置顶且无标题栏的工具窗口）
    static bool ignoreTopmostWindow(HWND hwnd)
    {
        // 如果未启用忽略功能，不忽略任何窗口
        if (!g_settings.ignoreTopmostNoTitleBarWindows)
            return false;

        // 检查是否为置顶窗口
        SetLastError(ERROR_SUCCESS);
        DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        // 检查GetWindowLong调用是否失败，如果失败则记录窗口信息和错误
        DWORD dwError = GetLastError();
        if (dwError != ERROR_SUCCESS)
        {
            log("GetWindowLong failed for window 0x{:x}, error code: {}", (uintptr_t)hwnd, dwError);
        }

        if ((exStyle & WS_EX_TOPMOST) == 0)
            return false;

        // 检查是否无标题栏
        DWORD style = GetWindowLong(hwnd, GWL_STYLE);
        if ((style & WS_CAPTION) != 0)
            return false;

        // 检查是否为工具窗口
        if ((exStyle & WS_EX_TOOLWINDOW) == 0)
            return false;

        // 满足所有条件，应该忽略此窗口
        return true;
    }

    // 判断是否匹配用户自定义忽略规则（标题+类名，留空一项则只判断另一项）
    static bool ignoreByCustomRule(const std::string &title, const std::string &className)
    {
        for (const auto &rule : g_settings.ignoreRules)
        {
            bool titleMatch = rule.title.empty() || title == rule.title;
            bool classMatch = rule.className.empty() || className == rule.className;
            if (titleMatch && classMatch)
                return true;
        }
        return false;
    }

public:
    // 显示桌面功能的主要实现
    static bool showDesktop()
    {
        HWND hDesktop = getDesktopWnd();
        HMONITOR hMonitor = getCurrentMonitor();
        std::wstring monitorName = hMonitor ? getMonitorName(hMonitor) : L"";

        if (!hDesktop || !hMonitor || monitorName.empty())
        {
            log("Failed to get desktop window or current monitor");
            return false;
        }

        pruneDeadWindows();

        auto enumMonitorWnd = [&](HWND hDesktop, HMONITOR hMonitor)
        {
            std::vector<WndInfo> vec;

            // Snapshot of the top-level windows, top to bottom. Walking with
            // GetNextWindow can stop early or loop when windows close or move.
            std::vector<HWND> zOrder;
            EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&zOrder));

            // Windows above the desktop, nearest to the desktop first (as before).
            size_t desktopPos = 0;
            while (desktopPos < zOrder.size() && zOrder[desktopPos] != hDesktop)
                desktopPos++;
            if (desktopPos == zOrder.size())
            {
                log("Desktop window not found in the window list");
                return vec;
            }

            long long minSize = (long long)g_settings.minWindowSize * g_settings.minWindowSize;

            for (size_t i = desktopPos; i-- > 0;)
            {
                HWND hWndCcc = zOrder[i];

                if (!::IsWindowVisible(hWndCcc) || IsIconic(hWndCcc) || isInvisibleWin10BackgroundAppWindow(hWndCcc))
                {
                    continue;
                }

                // 忽略置顶且无标题栏的工具窗口
                if (ignoreTopmostWindow(hWndCcc))
                {
                    continue;
                }

                // 获取窗口类名
                auto wndClass = getWindowClassName(hWndCcc);

                // 获取窗口标题
                auto windowTitle = getWindowTitleName(hWndCcc);

                // 检查自定义忽略规则
                if (ignoreByCustomRule(windowTitle, wndClass))
                {
                    log("skipping window by custom rule, title: {} class: {}", windowTitle, wndClass);
                    continue;
                }

                // 获取拥有者窗口句柄
                auto ownerWnd = ::GetWindow(hWndCcc, GW_OWNER);

                // 处理需要忽略的类名.
                {
                    static std::set<std::string> ignoredClass = {"Shell_SecondaryTrayWnd", "Shell_TrayWnd", "WorkerW", "SysShadow", "TaskListThumbnailWnd"};
                    if (ignoredClass.find(wndClass) != ignoredClass.end())
                    {
                        continue;
                    }
                }

                RECT rct = {0};
                if (!::GetWindowRect(hWndCcc, &rct))
                    continue;

                // 检查窗口是否在当前显示器上
                if (MonitorFromRect(&rct, MONITOR_DEFAULTTONEAREST) != hMonitor)
                    continue;

                // 忽略小窗口
                {
                    int width = rct.right - rct.left;
                    int height = rct.bottom - rct.top;

                    if ((long long)width * height < minSize)
                        continue;

                    if (height < g_settings.minWindowSize || width < g_settings.minWindowSize)
                        continue;
                }

                DWORD pid = 0;
                GetWindowThreadProcessId(hWndCcc, &pid);
                vec.push_back({hWndCcc, wndClass, ownerWnd, windowTitle, pid});
            }
            return vec;
        };

        auto vecCur = enumMonitorWnd(hDesktop, hMonitor);
        auto &listWnd = mapWnd[monitorName];

        log("Found {} windows to process on current monitor", vecCur.size());

        // 允许设置前台窗口，避免窗口在任务栏闪烁
        AllowSetForegroundWindow(ASFW_ANY);

        if (!vecCur.empty())
        {
            log("Minimizing windows on current monitor");

            for (auto it = listWnd.begin(); it != listWnd.end();)
            {
                auto curIt = it++;
                if (!curIt->ownerWnd)
                    listWnd.erase(curIt);
            }

            for (auto &rc : vecCur)
            {
                if (!IsWindowVisible(rc.wnd) || IsIconic(rc.wnd))
                    continue;

                if (rc.ownerWnd)
                {
                    // ShowOwnedPopups waits on the owner's thread.
                    if (IsHungAppWindow(rc.ownerWnd))
                        continue;
                    ShowOwnedPopups(rc.ownerWnd, false);
                    log("minimizing ShowOwnedPopups window 0x{:x} title: {} class: {}", (uintptr_t)rc.ownerWnd, rc.title, rc.cls);
                }
                else
                {
                    log("minimizing window 0x{:x} title: {} class: {}", (uintptr_t)rc.wnd, rc.title, rc.cls);
                    controlWindow(rc.wnd, false);
                }

                listWnd.emplace_back(std::move(rc));
            }

            activeWnd(hDesktop);
        }
        else
        {
            log("Restoring windows on current monitor");

            HWND hLastWnd = nullptr;

            for (auto it = listWnd.begin(); it != listWnd.end(); it++)
            {
                auto &rc = *it;
                if (!isSameWindow(rc))
                    continue;

                if (rc.ownerWnd)
                {
                    if (!::IsWindow(rc.ownerWnd) || IsHungAppWindow(rc.ownerWnd))
                        continue;
                    ShowOwnedPopups(rc.ownerWnd, true);
                    log("restoring ShowOwnedPopups window 0x{:x} title: {} class: {}", (uintptr_t)rc.ownerWnd, rc.title, rc.cls);
                }
                else
                {
                    log("restoring window 0x{:x} title: {} class: {}", (uintptr_t)rc.wnd, rc.title, rc.cls);
                    controlWindow(rc.wnd, true);
                    hLastWnd = rc.wnd;
                }

                SetWindowPos(rc.wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_ASYNCWINDOWPOS);
                SetWindowPos(rc.wnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
            }

            listWnd.clear();
            activeWnd(hLastWnd);
        }

        return true;
    }

    // On unload: show popups this mod hid whose owner is still open; nothing
    // else can bring them back. Minimized windows stay on the taskbar.
    static void showHiddenPopups()
    {
        for (auto &[name, listWnd] : mapWnd)
        {
            for (auto &rc : listWnd)
            {
                if (rc.ownerWnd && isSameWindow(rc) && ::IsWindow(rc.ownerWnd) &&
                    !IsIconic(rc.ownerWnd) && !IsHungAppWindow(rc.ownerWnd))
                    ShowOwnedPopups(rc.ownerWnd, true);
            }
        }
        mapWnd.clear();
    }
};

// 替换为Hook深层的函数,支持触摸板显示桌面手势,并且在该函数中调用我们自己的实现
void(__cdecl *RaiseDesktop_Original)(void *pThis, int flags);
void RaiseDesktop_Hook(void *pThis, int flags)
{
    log("_RaiseDesktop called, flags={},tid={}", flags, ::GetCurrentThreadId());

    if (flags == 2 || flags == 3)
    {
        // Skip a call that overlaps or re-enters one in progress.
        static std::atomic<bool> s_busy = false;
        if (s_busy.exchange(true))
            return;

        static ULONGLONG s_last_tick = 0;
        //修复触摸手势重复进入问题,添加防抖忽略
        //已知问题:触摸手势下滑能最小化所有窗口显示桌面,但上滑不会进入此函数不会还原桌面,想还原窗口仍需下滑
        if (GetTickCount64() - s_last_tick > 200)
        {
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                WindShowDesktop::showDesktop();
            }
            s_last_tick = ::GetTickCount64();
        }
        s_busy = false;
    }
    else
    {
        RaiseDesktop_Original(pThis, flags);
    }
}

// 加载用户设置
void LoadSettings()
{
    log("Loading settings");
    bool ignoreTopmostNoTitleBarWindows = Wh_GetIntSetting(L"ignoreTopmostNoTitleBarWindows");
    int minWindowSize = Wh_GetIntSetting(L"minWindowSize");
    if (minWindowSize < 0)
        minWindowSize = 0;
    if (minWindowSize > 10000)
        minWindowSize = 10000;

    // 加载自定义忽略规则
    std::vector<IgnoreRule> ignoreRules;
    for (int i = 0;; i++)
    {
        auto titleW = Wh_GetStringSetting(L"ignoreRules[%d].title", i);
        auto classW = Wh_GetStringSetting(L"ignoreRules[%d].className", i);

        std::string title = unicode_ansi(titleW);
        std::string cls = unicode_ansi(classW);

        Wh_FreeStringSetting(titleW);
        Wh_FreeStringSetting(classW);

        // 两项均为空则已超出列表范围，停止读取
        if (title.empty() && cls.empty())
            break;

        log("custom ignore rule [{}]: title='{}' class='{}'", i, title, cls);
        ignoreRules.push_back({std::move(title), std::move(cls)});
    }

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_settings.ignoreTopmostNoTitleBarWindows = ignoreTopmostNoTitleBarWindows;
        g_settings.minWindowSize = minWindowSize;
        g_settings.ignoreRules = std::move(ignoreRules);
    }
    log("Finished loading settings");
}

// 模块初始化函数
BOOL Wh_ModInit()
{
    Wh_Log(L"init");

    LoadSettings();

    // explorer.exe
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(protected: void __cdecl CTray::_RaiseDesktop(enum RAISEDESKTOPFLAGS))"},
            (void **)&RaiseDesktop_Original,
            (void *)RaiseDesktop_Hook,
        },
    };

    HMODULE hExplorer = GetModuleHandle(L"explorer.exe");
    if (!hExplorer)
    {
        log("GetModuleHandle(explorer.exe) error:{}", ::GetLastError());
        return false;
    }

    if (!WindhawkUtils::HookSymbols(hExplorer, symbolHooks, ARRAYSIZE(symbolHooks)))
    {
        log("HookSymbols error");
        return false;
    }

    return true;
}

// 模块卸载时清理资源
void Wh_ModUninit()
{
    Wh_Log(L"Uninit");

    std::lock_guard<std::mutex> lock(g_stateMutex);
    WindShowDesktop::showHiddenPopups();
}

// 用户设置发生变化时重新加载设置
void Wh_ModSettingsChanged()
{
    Wh_Log(L"SettingsChanged");
    LoadSettings();
}
