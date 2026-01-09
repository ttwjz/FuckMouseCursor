// windres resource.rc -o resource.res
// g++ main.cpp resource.o -o FuckMouseCursor.exe -s -O2 -mwindows -static -fno-exceptions -fno-rtti

// 1. 在最前面定义 Unicode，否则 Windows API 会默认用 char* 版本导致报错
#define UNICODE
#define _UNICODE
// 2. 强制使用 OEM 资源 (用于加载系统光标)
#define OEMRESOURCE
// 3. 目标系统 Win10 (0x0A00)
#define _WIN32_WINNT 0x0A00
// 4. 极简模式，减少依赖
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>

// 链接指令
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shcore.lib") // DPI适配需要

// --- 常量定义 ---
constexpr int WM_TRAYICON = WM_USER + 1;       // 托盘图标消息
constexpr int WM_KICK_TIMER = WM_USER + 2;     // 自定义消息，用于启动隐藏倒计时
constexpr int WM_DESKTOP_SWITCH = WM_USER + 3; // 自定义消息，用于在主线程处理桌面切换

constexpr int ID_TRAY_EXIT = 1001;
constexpr int ID_TRAY_TOGGLE = 1002;
constexpr int ID_TRAY_AUTOSTART = 1003;

constexpr int ID_TIMER_HIDE_DELAY = 101; // 指针隐藏延时器
constexpr int ID_TIMER_MONITOR = 102;    // 指针移动监控器

constexpr int HIDE_DELAY_MS = 500;                            // 指针隐藏延时500ms
constexpr int MONITOR_INTERVAL_MS = 100;                      // 指针移动监控器间隔100ms
constexpr int MONITOR_KEEPALIVE = 2000 / MONITOR_INTERVAL_MS; // 指针移动监控器保活2秒
constexpr int MOUSE_THRESHOLD = 50;                           // 指针移动阈值 (像素距离的平方)

// --- 全局上下文 (整合全局变量，内存布局更紧凑) ---
struct AppContext
{
    // 钩子句柄 (动态管理)
    HHOOK hKeyboardHook;      // 键盘钩子句柄
    HWINEVENTHOOK hEventHook; // 桌面切换事件钩子句柄

    NOTIFYICONDATA nid;
    HWND hMainWnd;
    HCURSOR hGlobalTransCursor; // 透明指针句柄

    // 状态标志
    bool isEnabled;             // 功能启用状态
    bool isCursorHidden;        // 指针隐藏状态
    bool isTimerPending;        // 是否正在500ms延时隐藏指针
    bool isMonitorRunning;      // 100ms指针移动监控器是否运行
    bool isLongPressSuppressed; // 长按抑制标记

    // 状态追踪变量，替代 GetAsyncKeyState
    bool isCtrlDown;
    bool isAltDown;
    bool isWinDown;
    // Shift 不需要拦截（Shift+A 是正常输入），但也记录一下备用
    bool isShiftDown;

    int monitorKeepAlive; // 监控器保活计数
    DWORD lastVkCode;     // 上一次按键VK
    POINT ptLastPos;      // 上一次鼠标坐标
};

// 初始化全局实例 (C++结构体默认初始化为0)
AppContext ctx = {0};

const wchar_t *REG_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t *APP_NAME = L"FuckMouseCursor";

// --- 函数声明 ---
void InitTransparentCursor();    // 初始化透明指针
void DestroyTransparentCursor(); // 销毁透明指针
void UpdateHooks(bool enable);   // 统一管理钩子生命周期
void HideMouseCursor();          // 隐藏指针
void RestoreMouseCursor();       // 恢复指针
void StartMonitor();             // 启动指针移动监控器
void StopMonitor();              // 停止指针移动监控器
void UpdateTrayIcon();           // 更新托盘图标
bool IsAutoStart();              // 检测是否开机自启动
void ToggleAutoStart();          // 切换开机自启动状态
bool IsContentKey(DWORD vkCode); // 判断是否为内容按键
void EnableHighDPI();            // 启用高DPI支持

// --- 核心逻辑: 按键过滤 (性能优化版) ---
bool IsContentKey(DWORD vkCode)
{
    // 1. 【组合键拦截】(零系统调用)
    // 直接检查内存中的状态位
    // 注意：Alt 键在钩子中有时会触发 WM_SYSKEYDOWN，也要通过状态位判断
    if (ctx.isCtrlDown || ctx.isAltDown || ctx.isWinDown)
        return false;

    // 2. 【白名单匹配】

    // A-Z (0x41 - 0x5A)
    if (vkCode >= 0x41 && vkCode <= 0x5A)
        return true;

    // 0-9 (0x30 - 0x39)
    if (vkCode >= 0x30 && vkCode <= 0x39)
        return true;

    // 小键盘区域: 数字0-9 (0x60-0x69) + 乘加分隔减小点除 (0x6A-0x6F)
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_DIVIDE)
        return true;

    // 核心控制键: 空格, 回车, 退格, TAB, Delete
    switch (vkCode)
    {
    case VK_SPACE:
    case VK_BACK:
    case VK_RETURN:
    case VK_TAB:
    case VK_DELETE:
        return true;
    default:
        break;
    }

    // 方向键
    if (vkCode >= VK_LEFT && vkCode <= VK_DOWN)
        return true;

    // 标点符号 (OEM Keys)
    // Windows 定义的符号键范围比较散，这是最主要的几个段
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_8)
        return true;
    if (vkCode == VK_OEM_102)
        return true;

    // 默认：不在白名单内，返回 false
    return false;
}

// --- 桌面切换事件回调 ---
// 当进入/退出 UAC、锁屏、Ctrl+Alt+Del 时触发
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    // 事件回调可能在任意线程，为了线程安全，我们PostMessage给主窗口处理
    if (event == EVENT_SYSTEM_DESKTOPSWITCH)
        PostMessage(ctx.hMainWnd, WM_DESKTOP_SWITCH, 0, 0);
}

// --- 键盘钩子 ---
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT *pKbd = (KBDLLHOOKSTRUCT *)lParam;
        DWORD vk = pKbd->vkCode;

        // --- A. 状态维护 (无论开关与否，始终维护状态，保证准确) ---
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            if (vk == VK_LCONTROL || vk == VK_RCONTROL)
                ctx.isCtrlDown = true;
            else if (vk == VK_LMENU || vk == VK_RMENU)
                ctx.isAltDown = true;
            else if (vk == VK_LWIN || vk == VK_RWIN)
                ctx.isWinDown = true;
            else if (vk == VK_LSHIFT || vk == VK_RSHIFT)
                ctx.isShiftDown = true;
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
        {
            if (vk == VK_LCONTROL || vk == VK_RCONTROL)
                ctx.isCtrlDown = false;
            else if (vk == VK_LMENU || vk == VK_RMENU)
                ctx.isAltDown = false;
            else if (vk == VK_LWIN || vk == VK_RWIN)
                ctx.isWinDown = false;
            else if (vk == VK_LSHIFT || vk == VK_RSHIFT)
                ctx.isShiftDown = false;
        }

        // --- B. 业务逻辑 ---
        if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && !ctx.isCursorHidden)
        {
            // 1. 长按处理
            if (vk == ctx.lastVkCode)
            {
                if (ctx.isLongPressSuppressed)
                    return CallNextHookEx(ctx.hKeyboardHook, nCode, wParam, lParam);
            }
            else
            {
                ctx.lastVkCode = vk;
                ctx.isLongPressSuppressed = false;
            }

            // 2. 倒计时中忽略
            if (ctx.isTimerPending)
                return CallNextHookEx(ctx.hKeyboardHook, nCode, wParam, lParam);

            // 3. 过滤并触发
            if (IsContentKey(vk))
                PostMessage(ctx.hMainWnd, WM_KICK_TIMER, 0, 0);
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
        {
            if (vk == ctx.lastVkCode)
            {
                ctx.lastVkCode = 0;
                ctx.isLongPressSuppressed = false;
            }
        }
    }
    return CallNextHookEx(ctx.hKeyboardHook, nCode, wParam, lParam);
}

// --- 透明指针资源管理 ---
void InitTransparentCursor()
{
    int w = GetSystemMetrics(SM_CXCURSOR);
    int h = GetSystemMetrics(SM_CYCURSOR);
    // 使用 HeapAlloc 替代 new
    size_t size = (w * h / 8 + 100);
    BYTE *andMask = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    BYTE *xorMask = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);

    if (andMask && xorMask)
    {
        memset(andMask, 0xFF, size);
        ctx.hGlobalTransCursor = CreateCursor(GetModuleHandle(NULL), 0, 0, w, h, andMask, xorMask);
    }

    if (andMask)
        HeapFree(GetProcessHeap(), 0, andMask);
    if (xorMask)
        HeapFree(GetProcessHeap(), 0, xorMask);
}

// --- 销毁透明指针资源 ---
void DestroyTransparentCursor()
{
    if (ctx.hGlobalTransCursor)
        DestroyCursor(ctx.hGlobalTransCursor);
}

// --- 钩子动态管理 ---
void UpdateHooks(bool enable)
{
    if (enable)
    {
        // 1. 初始化按键状态快照
        // 在挂载钩子的瞬间，读取一次系统状态，防止状态不同步
        ctx.isCtrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        ctx.isAltDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        ctx.isWinDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

        // 2. 挂载钩子
        if (!ctx.hKeyboardHook)
        {
            ctx.hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
        }
        if (!ctx.hEventHook)
        {
            ctx.hEventHook = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH,
                                             NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        }
    }
    else
    {
        // 卸载键盘钩子
        if (ctx.hKeyboardHook)
        {
            UnhookWindowsHookEx(ctx.hKeyboardHook);
            ctx.hKeyboardHook = NULL;
        }
        // 卸载桌面事件钩子
        if (ctx.hEventHook)
        {
            UnhookWinEvent(ctx.hEventHook);
            ctx.hEventHook = NULL;
        }
        // 清理状态
        ctx.lastVkCode = 0;
        ctx.isLongPressSuppressed = false;
        ctx.isCtrlDown = false;
        ctx.isAltDown = false;
        ctx.isWinDown = false;
    }
}

// --- 指针移动监控器状态控制 ---
// --- 启动指针移动监控器 ---
void StartMonitor()
{
    if (!ctx.isMonitorRunning) // 如果未运行则启动，排除保活期间重复启动
    {
        GetCursorPos(&ctx.ptLastPos);
        SetTimer(ctx.hMainWnd, ID_TIMER_MONITOR, MONITOR_INTERVAL_MS, NULL);
        ctx.isMonitorRunning = true;
    }
}

// --- 停止指针移动监控器 ---
void StopMonitor()
{
    if (ctx.isMonitorRunning)
    {
        KillTimer(ctx.hMainWnd, ID_TIMER_MONITOR);
        ctx.isMonitorRunning = false;
    }
}

// --- 隐藏指针 ---
void HideMouseCursor()
{
    if (ctx.isCursorHidden || !ctx.hGlobalTransCursor)
        return;

    GetCursorPos(&ctx.ptLastPos);

    const int cursors[] = {OCR_NORMAL, OCR_IBEAM, OCR_HAND, OCR_WAIT, OCR_APPSTARTING, OCR_SIZENWSE, OCR_SIZENESW, OCR_SIZEWE, OCR_SIZENS, OCR_SIZEALL, OCR_NO};
    for (int id : cursors)
        SetSystemCursor(CopyCursor(ctx.hGlobalTransCursor), id);

    ctx.isCursorHidden = true;
    ctx.isTimerPending = false;
}

// --- 恢复指针 ---
void RestoreMouseCursor()
{
    if (!ctx.isCursorHidden)
        return;

    SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);
    ctx.isCursorHidden = false;
    ctx.isTimerPending = false;
    StopMonitor();
}

// --- 窗口过程 ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE: // 窗口创建
        ctx.isEnabled = true;
        ctx.hMainWnd = hwnd;

        ctx.nid.cbSize = sizeof(NOTIFYICONDATA);
        ctx.nid.hWnd = hwnd;
        ctx.nid.uID = 1;
        ctx.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        ctx.nid.uCallbackMessage = WM_TRAYICON;
        UpdateTrayIcon();
        Shell_NotifyIcon(NIM_ADD, &ctx.nid);

        InitTransparentCursor();
        // 【初始化时】如果启用，则挂载钩子
        if (ctx.isEnabled)
            UpdateHooks(true);

        break;

    case WM_KICK_TIMER: // 按键按下，启动隐藏倒计时
        if (!ctx.isTimerPending)
        {
            ctx.isTimerPending = true;
            ctx.monitorKeepAlive = 0;
            StartMonitor();
            SetTimer(hwnd, ID_TIMER_HIDE_DELAY, HIDE_DELAY_MS, NULL);
        }
        break;

    case WM_DESKTOP_SWITCH:
        // 桌面切换（UAC/锁屏），强制重置
        RestoreMouseCursor();
        ctx.isTimerPending = false;
        ctx.monitorKeepAlive = 0;
        // 注意：因为 RestoreMouseCursor 会调用 StopMonitor，所以这里无需手动停止
        break;

    case WM_TIMER:                      // 计时器事件
        if (wParam == ID_TIMER_MONITOR) // 指针移动监控器
        {
            // 双重保险：如果禁用了，Monitor不应该运行，强制停止
            if (!ctx.isEnabled)
            {
                StopMonitor();
                break;
            }

            POINT ptCurrent;
            if (GetCursorPos(&ptCurrent))
            {
                int dx = ptCurrent.x - ctx.ptLastPos.x;
                int dy = ptCurrent.y - ctx.ptLastPos.y;

                if ((dx * dx + dy * dy) > MOUSE_THRESHOLD)
                {
                    ctx.ptLastPos = ptCurrent;
                    // 长按抑制：如果在长按期间鼠标动了，标记抑制，
                    // 这样后续的长按信号在 KeyboardProc 中会被忽略
                    ctx.isLongPressSuppressed = true;

                    // 处于等待隐藏状态，取消隐藏倒计时
                    if (ctx.isTimerPending)
                    {
                        KillTimer(hwnd, ID_TIMER_HIDE_DELAY);
                        ctx.isTimerPending = false;
                        ctx.monitorKeepAlive = MONITOR_KEEPALIVE; // 重置保活计数，防止立即停止监控器
                    }
                    // 处于已隐藏状态，恢复指针
                    if (ctx.isCursorHidden)
                        RestoreMouseCursor();
                }
            }

            if (!ctx.isTimerPending && !ctx.isCursorHidden) // 非等待隐藏状态，减少保活计数
            {
                if (ctx.monitorKeepAlive > 0)
                    ctx.monitorKeepAlive--;
                else
                    StopMonitor();
            }
        }
        else if (wParam == ID_TIMER_HIDE_DELAY) // 指针隐藏延时器时间到了
        {
            KillTimer(hwnd, ID_TIMER_HIDE_DELAY);
            HideMouseCursor();
        }
        break;

    case WM_TRAYICON:               // 托盘图标操作
        if (lParam == WM_RBUTTONUP) // 右键菜单
        {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, ctx.isEnabled ? MF_CHECKED : 0, ID_TRAY_TOGGLE, ctx.isEnabled ? L"状态: 已开启" : L"状态: 已暂停");
            AppendMenu(hMenu, IsAutoStart() ? MF_CHECKED : 0, ID_TRAY_AUTOSTART, L"开机自启动");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, 0, ID_TRAY_EXIT, L"退出");
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        else if (lParam == WM_LBUTTONDBLCLK) // 双击切换
        {
            SendMessage(hwnd, WM_COMMAND, ID_TRAY_TOGGLE, 0);
        }
        break;

    case WM_COMMAND: // 托盘菜单命令
        switch (LOWORD(wParam))
        {
        case ID_TRAY_EXIT: // 退出程序
            DestroyWindow(hwnd);
            break;
        case ID_TRAY_TOGGLE: // 切换启用状态
            ctx.isEnabled = !ctx.isEnabled;
            if (!ctx.isEnabled) // 暂停时：恢复光标，停止所有Timer，卸载所有Hook
            {
                RestoreMouseCursor();
                KillTimer(hwnd, ID_TIMER_HIDE_DELAY);
                StopMonitor();
                UpdateHooks(false); // 卸载钩子
            }
            else
            {
                // 启用时：挂载Hook
                UpdateHooks(true); // 挂载钩子
            }
            UpdateTrayIcon();
            break;
        case ID_TRAY_AUTOSTART: // 切换开机自启动
            ToggleAutoStart();
            break;
        }
        break;

    case WM_DESTROY: // 窗口销毁
        RestoreMouseCursor();
        UpdateHooks(false); // 确保退出时卸载钩子
        DestroyTransparentCursor();
        Shell_NotifyIcon(NIM_DELETE, &ctx.nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- 辅助函数 ---

// --- 更新托盘图标和提示 ---
void UpdateTrayIcon()
{
    int iconId = ctx.isEnabled ? 101 : 102;
    ctx.nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(iconId));
    if (!ctx.nid.hIcon)
        ctx.nid.hIcon = LoadIcon(NULL, ctx.isEnabled ? IDI_APPLICATION : IDI_SHIELD);
    wcscpy_s(ctx.nid.szTip, ctx.isEnabled ? L"去你的鼠标指针 (已开启)" : L"去你的鼠标指针 (已暂停)");
    Shell_NotifyIcon(NIM_MODIFY, &ctx.nid);
}

// --- 检测是否开机自启动 ---
bool IsAutoStart()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t path[MAX_PATH];
        DWORD size = sizeof(path);
        DWORD type;
        LONG result = RegQueryValueEx(hKey, APP_NAME, NULL, &type, (LPBYTE)path, &size);
        RegCloseKey(hKey);
        return (result == ERROR_SUCCESS);
    }
    return false;
}

// --- 切换开机自启动状态 ---
void ToggleAutoStart()
{
    bool enable = !IsAutoStart();
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        if (enable)
        {
            wchar_t exePath[MAX_PATH];
            GetModuleFileName(NULL, exePath, MAX_PATH);
            RegSetValueEx(hKey, APP_NAME, 0, REG_SZ, (const BYTE *)exePath, (wcslen(exePath) + 1) * sizeof(wchar_t));
        }
        else
        {
            RegDeleteValue(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

// --- DPI 适配 ---
void EnableHighDPI()
{
    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    // 定义函数指针类型，避免依赖头文件定义
    typedef BOOL(WINAPI * PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    if (hUser32)
    {
        auto pfn = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfn)
        {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
            pfn((DPI_AWARENESS_CONTEXT)-4);
            return;
        }
    }
    SetProcessDPIAware();
}

// --- 程序入口点 ---
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int)
{
    EnableHighDPI();

    HANDLE m = CreateMutex(NULL, TRUE, L"Global\\FuckMouseCursorMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBox(NULL, L"程序已经在运行了！", L"提示", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    WNDCLASS wc = {0, WndProc, 0, 0, h, 0, 0, 0, 0, L"FuckMouseCursorCls"};
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(L"FuckMouseCursorCls", L"Hider", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 0, 0, h, 0);

    if (!hwnd)
        return 0;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}