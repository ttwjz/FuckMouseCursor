cmake_minimum_required(VERSION 3.15) # 建议用较新版本，支持更好
project(FuckMouseCursor LANGUAGES CXX RC) # 显式启用RC语言支持资源文件

# 设置编译器选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 创建Windows GUI可执行文件
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    res/resource.rc
)

# 编译选项
target_compile_options(${PROJECT_NAME} PRIVATE
    -O2 # 优化级别2
    -fno-exceptions # 禁用异常
    -fno-rtti # 禁用RTTI
    -fno-unwind-tables # 不生成展开表
    -fno-asynchronous-unwind-tables # 不生成异步展开表
    -ffunction-sections
    -fdata-sections
)

# 链接选项
target_link_options(${PROJECT_NAME} PRIVATE
    -s # 剥离符号（减小exe体积）
    -static # 静态链接（不依赖DLL）
    # -mwindows # 已由 WIN32 关键字自动处理，无需重复添加
    -Wl,--gc-sections # 移除未使用的代码段
    -Wl,--strip-all # 移除所有符号
)
