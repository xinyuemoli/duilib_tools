cmake_minimum_required(VERSION 3.15)
project(DuiLibFinder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出到 bin 目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Windows SDK
add_definitions(-DUNICODE -D_UNICODE)
add_definitions(-DWIN32_LEAN_AND_MEAN)

# 添加子项目
add_subdirectory(src/HookDll)
add_subdirectory(src/Finder)