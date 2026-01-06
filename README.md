# FuckMouseCursor (去你的鼠标指针) 🖱️🚫

> **Fuck mouse cursor! Don't show it when I'm typing.**
>
> **去你的鼠标指针！不要在我打字的时候挡住视线。**

[![Platform](https://img.shields.io/badge/platform-Windows-0078d7.svg)](https://www.microsoft.com/windows/)
[![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)](https://isocpp.org/)
[![Size](https://img.shields.io/badge/size-%3C100KB-success.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

![on.ico](https://youke3.picui.cn/s1/2026/01/06/695d018684885.ico)

一个极致轻量化、高性能的 Windows 工具。当你开始打字时，它会自动隐藏鼠标指针；当你移动鼠标时，指针会自动恢复。

专为程序员、作家和极简主义者设计。**没有多余的 UI，没有繁重的依赖，只有一个存在于系统托盘的安静守护者。**

---

## 📥 下载 (Download)

前往 [Releases](https://github.com/ttwjz/FuckMouseCursor/releases) 页面下载最新版本的 `FuckMouseCursor.exe`。

## ✨ 核心特性 (Features)

*   **智能隐藏**: 采用**白名单机制**，仅在输入实际内容（字母、数字、符号）时隐藏鼠标。
*   **干扰过滤**:
*   *   ✅ 只在内容输入键生效（`A-Z`,`0-9`,符号等）
    *   ✅ 自动忽略修饰键（`Ctrl`, `Alt`, `Win`, `Shift`）。
    *   ✅ 自动忽略组合键（如 `Ctrl+C`, `Alt+Tab`）。
    *   ✅ 自动忽略导航键（`Home`, `End` 等）和功能键 (`F1-F12`)。
*   **极致性能**:
    *   **近乎0% CPU 占用**: 采用“按需启动”的定时器机制，空闲时彻底休眠。
    *   **极低内存**: 移除 C++ 标准库依赖 (`-fno-exceptions`, `-fno-rtti`)，纯 Win32 API 实现，体积仅约 100KB+。
    *   **无 Hook 延迟**: 隐藏期间自动卸载耗时的鼠标钩子，改用高效轮询，不影响高回报率游戏鼠标性能。
*   **完美体验**:
    *   **防抖动**: 连续输入时重置计时，长按按键时移动鼠标不闪烁。
    *   **高 DPI 适配**: 完美支持 Win10/Win11 高分屏，图标菜单清晰锐利。
    *   **可靠恢复**: 即使在任务管理器等高权限窗口，也能通过看门狗机制恢复鼠标。
*   **绿色便携**: 静态链接 (`-static`)，单文件运行，支持开机自启。

## 📂 项目结构 (Structure)

```text
FuckMouseCursor/
├── src/
│   └── main.cpp        # 核心源代码
├── res/
│   ├── resource.rc     # 资源定义文件 (图标、版本信息)
│   ├── on.ico         # 应用程序图标 (ID: 101)
│   └── off.ico       # 暂停状态图标 (ID: 102)
├── CMakeLists.txt      # CMake 构建配置
└── LICENSE             # MIT 许可证
```

## 🛠️ 编译构建 (Build)

本项目使用 **CMake** 构建，推荐使用 **MinGW-w64 (GCC)** 编译器以获得最佳的体积优化效果。

### 前置要求
*   CMake (3.15 或更高)
*   MinGW-w64 (建议 GCC 10.0+)
*   Ninja (可选，构建速度更快)

### 构建步骤

打开终端（Terminal 或 PowerShell），进入项目根目录：

```bash
# 1. 创建构建目录
mkdir build
cd build

# 2. 生成构建文件 (使用 MinGW Makefiles)
cmake -G "MinGW Makefiles" ..
# 或者如果使用 Ninja: cmake -G "Ninja" ..

# 3. 编译 (Release 模式)
cmake --build . --config Release
```

编译完成后，可在 `build` 目录下找到 `FuckMouseCursor.exe`。

> **关于体积**: `CMakeLists.txt` 已配置 `-s` (strip symbols), `-static` (静态链接), `-fno-exceptions` 等选项，生成的 EXE 无需任何 DLL 即可运行，且体积极小。

## 🚀 使用方法 (Usage)

1.  运行 `FuckMouseCursor.exe`。
2.  程序将最小化到系统托盘（右下角）。
3.  **开始打字**：鼠标将在 500ms 后消失。
4.  **移动鼠标**：鼠标立即出现。
5.  **右键托盘图标**：
    *   **状态切换**: 开启或暂停隐藏功能。
    *   **开机自启**: 设置随系统启动。
    *   **退出**: 关闭程序。
6.  **双击托盘图标**: 快速切换 开启/暂停 状态。


## 📄 许可证 (License)

本项目采用 [MIT License](LICENSE) 开源。