cmake_minimum_required(VERSION 3.15) # 建议用较新版本，支持更好

project(FuckMouseCursor LANGUAGES CXX RC) # 显式启用RC语言支持资源文件

# 创建Windows GUI可执行文件（自动添加 -mwindows 子系统）
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    res/resource.rc
)

# C++标准（根据你的代码需要调整，这里用C++17作为示例）
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# 编译选项
target_compile_options(${PROJECT_NAME} PRIVATE
    -O2 # 优化级别2
    -fno-exceptions # 禁用异常
    -fno-rtti # 禁用RTTI
)

# 链接选项
target_link_options(${PROJECT_NAME} PRIVATE
    -s # 剥离符号（减小exe体积）
    -static # 静态链接（不依赖DLL）
    # -mwindows 已由 WIN32 关键字自动处理，无需重复添加
)

# 可选：静态链接标准库（进一步减小体积，MinGW支持）
target_link_libraries(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++)
