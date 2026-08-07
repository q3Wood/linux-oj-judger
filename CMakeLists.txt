cmake_minimum_required(VERSION 3.10)
project(JudgeCore VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 指定头文件路径
include_directories(include)

# 搜寻 src 目录下所有的 .cpp 源码文件
file(GLOB SOURCES "src/*.cpp")

# 生成可执行文件 bin/judge_core
add_executable(judge_core ${SOURCES})