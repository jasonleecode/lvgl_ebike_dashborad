cmake_minimum_required(VERSION 3.10)
project(ebike_sim C)

# 设置默认构建类型（Debug/Release），如果未指定
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)  # 默认使用 Debug 模式
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 根据构建类型配置编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug 模式：启用调试符号、关闭优化、启用所有警告
    add_compile_options(-g -O0 -Wall -Wextra)
    message(STATUS "Build type: Debug (with debug symbols)")
else()
    # Release 模式：启用优化
    add_compile_options(-O2)
    message(STATUS "Build type: Release")
endif()

# 添加头文件路径
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/lvgl
    ${PROJECT_SOURCE_DIR}/lv_drivers
    ${PROJECT_SOURCE_DIR}/ui
)

# 设置 lv_conf.h 和 lv_drv_conf.h
add_definitions(-DLV_CONF_INCLUDE_SIMPLE)
add_definitions(-DLV_LVGL_H_INCLUDE_SIMPLE)

# 查找源码
file(GLOB_RECURSE LVGL_SOURCES ${PROJECT_SOURCE_DIR}/lvgl/src/*.c)
file(GLOB_RECURSE DRIVERS_SOURCES ${PROJECT_SOURCE_DIR}/lv_drivers/*.c)
file(GLOB_RECURSE UI_SOURCES ${PROJECT_SOURCE_DIR}/ui/*.c)
set(SOURCES ${LVGL_SOURCES} ${DRIVERS_SOURCES} ${UI_SOURCES} ${PROJECT_SOURCE_DIR}/main.c)

# 查找 SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})
link_directories(${SDL2_LIBRARY_DIRS})

# 添加可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接 SDL2 库
target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES})
