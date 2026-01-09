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
#include <shellapi.h> // For ShellExecuteEx

// 链接指令
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shcore.lib")   // DPI适配需要
#pragma comment(lib, "advapi32.lib") // For Reg* functions

// --- 常量定义 ---
constexpr int WM_TRAYICON = WM_USER + 1;       // 托盘图标消息
constexpr int WM_KICK_TIMER = WM_USER + 2;     // 自定义消息，用于启动隐藏倒计时
constexpr int WM_DESKTOP_SWITCH = WM_USER + 3; // 自定义消息，用于在主线程处理桌面切换
constexpr int ID_TRAY_EXIT = 1001;             // 退出菜单项ID
constexpr int ID_TRAY_TOGGLE = 1002;           // 启用/暂停菜单项ID
constexpr int ID_TRAY_AUTOSTART = 1003;        // 开机自启菜单项ID
constexpr int ID_TRAY_RESTART_ADMIN = 1004;    // 以管理员重启菜单项ID
constexpr int ID_TIMER_HIDE_DELAY = 101;       // 指针隐藏延时器ID
constexpr int ID_TIMER_MONITOR = 102;          // 指针移动监控器ID

constexpr int HIDE_DELAY_MS = 500;                            // 指针隐藏延时500ms
constexpr int MONITOR_INTERVAL_MS = 100;                      // 指针移动监控器间隔100ms
constexpr int MONITOR_KEEPALIVE = 2000 / MONITOR_INTERVAL_MS; // 指针移动监控器保活2秒
constexpr int MOUSE_THRESHOLD = 100;                          // 指针移动阈值 (像素距离的平方)

// --- AppContext 结构体定义 (整合全局变量，内存布局更紧凑) ---
struct AppContext
{
    // 钩子句柄 (动态管理)
    HHOOK hKeyboardHook;      // 键盘钩子句柄
    HWINEVENTHOOK hEventHook; // 桌面切换事件钩子句柄

    NOTIFYICONDATA nid;          // 托盘图标数据
    HWND hMainWnd;               // 主窗口句柄
    HCURSOR hGlobalTransCursor;  // 透明指针句柄
    HANDLE hSingleInstanceMutex; // 单实例互斥体句柄

    // 状态标志
    bool isEnabled;             // 功能启用状态
    bool isCursorHidden;        // 指针隐藏状态
    bool isTimerPending;        // 是否正在500ms延时隐藏指针
    bool isMonitorRunning;      // 100ms指针移动监控器是否运行
    bool isLongPressSuppressed; // 长按抑制标记
    bool isAdmin;               // 当前是否为管理员权限

    // 状态追踪变量 (用于 IsContentKey)
    bool isCtrlDown;
    bool isAltDown;
    bool isWinDown;
    // Shift 不需要拦截（Shift+A 是正常输入），但也记录一下备用
    // bool isShiftDown;

    int monitorKeepAlive; // 监控器保活计数
    DWORD lastVkCode;     // 上一次按键VK (用于长按检测)
    POINT ptLastPos;      // 上一次鼠标坐标 (用于移动检测)
};

// 初始化全局实例 (C++结构体默认初始化为0)
AppContext ctx = {0};

// 注册表路径和应用程序名称常量
const wchar_t *REG_PATH = L"Software\\GreenMouseHider"; // 自启标记的注册表路径
const wchar_t *APP_NAME = L"FuckMouseCursor";           // 应用程序的唯一名称 (用于Mutex和任务计划)

// --- 函数声明 ---

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
// WinEventHook回调函数
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
// 键盘钩子回调函数
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

// 资源管理
void InitTransparentCursor();    // 初始化透明指针
void DestroyTransparentCursor(); // 销毁透明指针
void InitResources();            // 封装所有资源初始化
void CleanupResources();         // 封装所有资源清理

// 功能控制
void UpdateHooks(bool enable);   // 统一管理钩子生命周期
void HideMouseCursor();          // 隐藏指针
void RestoreMouseCursor();       // 恢复指针
void UpdateTrayIcon();           // 更新托盘图标
bool IsContentKey(DWORD vkCode); // 判断是否为内容按键

// 监控器管理
void StartMonitor(); // 启动指针移动监控器
void StopMonitor();  // 停止指针移动监控器

// 开机自启管理
bool IsAutoStart();                          // 检测是否开机自启动
void ToggleAutoStart();                      // 切换开机自启动状态
bool ExecuteSchTasks(const wchar_t *params); // 执行CMD命令

// 权限与DPI
bool CheckIsAdmin();   // 检测管理员权限
void RestartAsAdmin(); // 以管理员重启
void EnableHighDPI();  // 启用高DPI支持

// void InitTransparentCursor();
// void DestroyTransparentCursor();             // 销毁透明指针
// void UpdateHooks(bool enable);               // 统一管理钩子生命周期
// void HideMouseCursor();                      // 隐藏指针
// void RestoreMouseCursor();                   // 恢复指针
// void StartMonitor();                         // 启动指针移动监控器
// void StopMonitor();                          // 停止指针移动监控器
// void UpdateTrayIcon();                       // 更新托盘图标
// bool IsAutoStart();                          // 检测是否开机自启动
// void ToggleAutoStart();                      // 切换开机自启动状态
// bool IsContentKey(DWORD vkCode);             // 判断是否为内容按键
// void EnableHighDPI();                        // 启用高DPI支持
// bool CheckIsAdmin();                         // 检测管理员权限
// void RestartAsAdmin();                       // 以管理员重启
// bool ExecuteSchTasks(const wchar_t *params); // 执行CMD命令 (C风格)

// --- 函数定义 (按功能分组) ---
// --- 资源管理实现 ---
// 初始化透明指针
void InitTransparentCursor()
{
    int w = GetSystemMetrics(SM_CXCURSOR);
    int h = GetSystemMetrics(SM_CYCURSOR);
    size_t size = (w * h / 8 + 100);
    HANDLE hHeap = GetProcessHeap();
    BYTE *andMask = (BYTE *)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, size);
    BYTE *xorMask = (BYTE *)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, size);
    if (andMask && xorMask)
    {
        memset(andMask, 0xFF, size);
        ctx.hGlobalTransCursor = CreateCursor(GetModuleHandle(NULL), 0, 0, w, h, andMask, xorMask);
    }
    if (andMask)
        HeapFree(hHeap, 0, andMask);
    if (xorMask)
        HeapFree(hHeap, 0, xorMask);
}

// 销毁透明指针
void DestroyTransparentCursor()
{
    if (ctx.hGlobalTransCursor)
        DestroyCursor(ctx.hGlobalTransCursor);
}

// 封装所有资源初始化
void InitResources()
{
    InitTransparentCursor();
}

// 封装所有资源清理
void CleanupResources()
{
    DestroyTransparentCursor();
}

// --- 钩子与事件管理实现 ---
// 统一管理钩子生命周期
void UpdateHooks(bool enable)
{
    if (enable)
    {
        // 1. 初始化按键状态快照 (在挂载钩子瞬间读取系统状态)
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
        // 卸载钩子
        if (ctx.hKeyboardHook)
        {
            UnhookWindowsHookEx(ctx.hKeyboardHook);
            ctx.hKeyboardHook = NULL;
        }
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

// 更新托盘图标和提示
void UpdateTrayIcon()
{
    int iconId = ctx.isEnabled ? 101 : 102;
    ctx.nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(iconId));
    if (!ctx.nid.hIcon)
        ctx.nid.hIcon = LoadIcon(NULL, ctx.isEnabled ? IDI_APPLICATION : IDI_SHIELD);

    // 在提示文本中增加管理员标识
    wchar_t szTip[128];
    wsprintf(szTip, L"去你的鼠标指针 (%s)%s",
             ctx.isEnabled ? L"已开启" : L"已暂停",
             ctx.isAdmin ? L" [Admin]" : L"");
    wcscpy_s(ctx.nid.szTip, szTip);
    Shell_NotifyIcon(NIM_MODIFY, &ctx.nid);
}

// --- 指针隐藏/恢复逻辑实现 ---
// 隐藏指针
void HideMouseCursor()
{
    if (ctx.isCursorHidden || !ctx.hGlobalTransCursor)
        return;
    GetCursorPos(&ctx.ptLastPos); // 记录当前位置

    // 替换所有标准系统光标
    const int cursors[] = {
        OCR_NORMAL, OCR_IBEAM, OCR_HAND, OCR_WAIT, OCR_APPSTARTING,
        OCR_SIZENWSE, OCR_SIZENESW, OCR_SIZEWE, OCR_SIZENS, OCR_SIZEALL,
        OCR_NO, OCR_UP, 32651 /*OCR_HELP*/
    };
    for (int id : cursors)
        SetSystemCursor(CopyCursor(ctx.hGlobalTransCursor), id);

    // 强制刷新光标：原地设置光标位置
    // SetCursorPos(ctx.ptLastPos.x, ctx.ptLastPos.y);

    ctx.isCursorHidden = true;
    ctx.isTimerPending = false;
}

// 恢复指针
void RestoreMouseCursor()
{
    if (!ctx.isCursorHidden)
        return;
    SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);
    ctx.isCursorHidden = false;
    ctx.isTimerPending = false;
    StopMonitor();
}

// 按键过滤 (性能优化版)
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

// --- 监控器管理实现 ---
// 启动指针移动监控器
void StartMonitor()
{
    if (!ctx.isMonitorRunning)
    {
        GetCursorPos(&ctx.ptLastPos); // 启动前同步坐标
        SetTimer(ctx.hMainWnd, ID_TIMER_MONITOR, MONITOR_INTERVAL_MS, NULL);
        ctx.isMonitorRunning = true;
    }
}

// 停止指针移动监控器
void StopMonitor()
{
    if (ctx.isMonitorRunning)
    {
        KillTimer(ctx.hMainWnd, ID_TIMER_MONITOR);
        ctx.isMonitorRunning = false;
    }
}

// --- 开机自启管理实现 ---
// 执行CMD命令 (C风格)
bool ExecuteSchTasks(const wchar_t *params)
{
    SHELLEXECUTEINFO sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"schtasks.exe";
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;

    if (ShellExecuteEx(&sei))
    {
        WaitForSingleObject(sei.hProcess, 5000);
        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    return false;
}

// 检测是否开机自启
bool IsAutoStart()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD value = 0, size = sizeof(value);
        RegQueryValueEx(hKey, L"AutoStart", NULL, NULL, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
        return (value == 1);
    }
    return false;
}

// 切换开机自启动状态
void ToggleAutoStart()
{
    if (!ctx.isAdmin)
    {
        MessageBox(NULL, L"设置开机自启（最高权限）需要管理员权限，请先以管理员身份重启程序。", L"权限不足", MB_OK | MB_ICONWARNING);
        return;
    }

    bool enable = !IsAutoStart();
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    wchar_t cmd[1024];

    if (enable)
    {
        wsprintf(cmd, L"/Create /TN \"%s_AutoRun\" /TR \"\\\"%s\\\"\" /SC ONLOGON /RL HIGHEST /F", APP_NAME, exePath);
        if (ExecuteSchTasks(cmd))
        {
            HKEY hKey;
            if (RegCreateKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
            {
                DWORD value = 1;
                RegSetValueEx(hKey, L"AutoStart", 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
                RegCloseKey(hKey);
            }
        }
    }
    else
    {
        // 删除任务
        wsprintf(cmd, L"/Delete /TN \"%s_AutoRun\" /F", APP_NAME);
        ExecuteSchTasks(cmd);

        // 删除标记
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
        {
            RegDeleteValue(hKey, L"AutoStart");
            RegCloseKey(hKey);
        }
    }
}

// --- 权限与DPI实现 ---
// 检测管理员权限
bool CheckIsAdmin()
{
    BOOL fIsRunAsAdmin = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize))
        {
            fIsRunAsAdmin = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return fIsRunAsAdmin;
}

// 以管理员身份重启
void RestartAsAdmin()
{
    wchar_t szPath[MAX_PATH];
    if (GetModuleFileName(NULL, szPath, MAX_PATH))
    {
        if (ctx.hSingleInstanceMutex)
        {
            CloseHandle(ctx.hSingleInstanceMutex);
            ctx.hSingleInstanceMutex = NULL;
        }

        SHELLEXECUTEINFO sei = {sizeof(sei)};
        sei.cbSize = sizeof(SHELLEXECUTEINFO);
        sei.lpVerb = L"runas";
        sei.lpFile = szPath;
        sei.hwnd = NULL;
        sei.nShow = SW_NORMAL;

        if (ShellExecuteEx(&sei))
        {
            PostQuitMessage(0);
        }
        else
        {
            ctx.hSingleInstanceMutex = CreateMutex(NULL, TRUE, L"Global\\FuckMouseCursorMutex");
        }
    }
}

// 启用高DPI支持
void EnableHighDPI()
{
    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    typedef BOOL(WINAPI * PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    if (hUser32)
    {
        auto pfn = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfn)
        {
            pfn((DPI_AWARENESS_CONTEXT)-4);
            return;
        }
    }
    SetProcessDPIAware();
}

// --- 回调函数实现 (WinEventProc, KeyboardProc 在顶部已声明) ---
// 桌面切换事件回调
// 当进入/退出 UAC、锁屏、Ctrl+Alt+Del 时触发
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    if (event == EVENT_SYSTEM_DESKTOPSWITCH)
    {
        PostMessage(ctx.hMainWnd, WM_DESKTOP_SWITCH, 0, 0);
    }
}

// 键盘钩子
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT *pKbd = (KBDLLHOOKSTRUCT *)lParam;
        DWORD vk = pKbd->vkCode;

        // 状态维护
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            if (vk == VK_LCONTROL || vk == VK_RCONTROL)
                ctx.isCtrlDown = true;
            else if (vk == VK_LMENU || vk == VK_RMENU)
                ctx.isAltDown = true;
            else if (vk == VK_LWIN || vk == VK_RWIN)
                ctx.isWinDown = true;
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
        {
            if (vk == VK_LCONTROL || vk == VK_RCONTROL)
                ctx.isCtrlDown = false;
            else if (vk == VK_LMENU || vk == VK_RMENU)
                ctx.isAltDown = false;
            else if (vk == VK_LWIN || vk == VK_RWIN)
                ctx.isWinDown = false;
        }

        // 业务逻辑
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

// --- 主窗口过程实现 ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE: // 窗口创建
        ctx.isEnabled = true;
        ctx.hMainWnd = hwnd;
        ctx.isAdmin = CheckIsAdmin(); // 初始化时检测权限

        ctx.nid.cbSize = sizeof(NOTIFYICONDATA);
        ctx.nid.hWnd = hwnd;
        ctx.nid.uID = 1;
        ctx.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        ctx.nid.uCallbackMessage = WM_TRAYICON;
        UpdateTrayIcon();
        Shell_NotifyIcon(NIM_ADD, &ctx.nid);

        InitResources();
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

    case WM_DESKTOP_SWITCH: // 桌面切换（UAC/锁屏），强制重置
        RestoreMouseCursor();
        ctx.isTimerPending = false;
        ctx.monitorKeepAlive = 0;
        break;

    case WM_TIMER:                      // 计时器事件
        if (wParam == ID_TIMER_MONITOR) // 指针移动监控器
        {
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
                        ctx.monitorKeepAlive = MONITOR_KEEPALIVE;
                    }
                    // 处于已隐藏状态，恢复指针
                    if (ctx.isCursorHidden)
                        RestoreMouseCursor();
                }
            }
            if (!ctx.isTimerPending && !ctx.isCursorHidden)
            {
                if (ctx.monitorKeepAlive > 0)
                    ctx.monitorKeepAlive--;
                else
                    StopMonitor();
            }
        }
        else if (wParam == ID_TIMER_HIDE_DELAY) // 指针隐藏延时回调
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
            // 显示开启状态
            AppendMenu(hMenu, ctx.isEnabled ? MF_CHECKED : 0, ID_TRAY_TOGGLE, ctx.isEnabled ? L"状态: 已开启" : L"状态: 已暂停");
            // 显示开机自启状态
            AppendMenu(hMenu, IsAutoStart() ? MF_CHECKED : 0, ID_TRAY_AUTOSTART, L"开机自启动");
            // 如果不是管理员，显示提升权限选项
            if (!ctx.isAdmin)
            {
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, 0, ID_TRAY_RESTART_ADMIN, L"以管理员身份重启");
            }
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
                UpdateHooks(false);
            }
            else
            {
                UpdateHooks(true);
            }
            UpdateTrayIcon();
            break;
        case ID_TRAY_AUTOSTART: // 切换开机自启动
            ToggleAutoStart();
            break;
        case ID_TRAY_RESTART_ADMIN: // 重启为管理员
            RestartAsAdmin();
            break;
        }
        break;

    case WM_DESTROY: // 窗口销毁
        RestoreMouseCursor();
        UpdateHooks(false);
        CleanupResources();
        Shell_NotifyIcon(NIM_DELETE, &ctx.nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- 主程序入口点 ---
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int)
{
    EnableHighDPI(); // 启用高DPI支持

    // 单实例互斥体
    ctx.hSingleInstanceMutex = CreateMutex(NULL, TRUE, L"Global\\FuckMouseCursorMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBox(NULL, L"程序已经在运行了！", L"提示", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // 注册窗口类
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = h;
    wc.lpszClassName = L"FuckMouseCursorCls";
    RegisterClass(&wc);

    // 创建隐藏窗口
    HWND hwnd = CreateWindow(L"FuckMouseCursorCls", L"Hider", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 0, 0, h, 0);
    if (!hwnd)
        return 0;

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 退出时释放互斥体
    if (ctx.hSingleInstanceMutex)
        CloseHandle(ctx.hSingleInstanceMutex);
    return 0;
}