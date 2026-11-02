cmake_minimum_required(VERSION 3.24)
project(fgb_module_kit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(fgb_dependency)

# 让模块能定位 Kit 的 cmake 脚本目录（打包脚本要用绝对路径引用）。
set(FGB_KIT_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake" CACHE PATH "Kit 的 cmake 脚本目录" FORCE)
list(APPEND CMAKE_MODULE_PATH "${FGB_KIT_CMAKE_DIR}")

add_library(fgb_kit INTERFACE)
add_library(fgb::kit ALIAS fgb_kit)
target_include_directories(fgb_kit INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(fgb_kit INTERFACE cxx_std_20)

# 独立构建时自己拉 SDK；被模块仓库引入时 fgb::sdk 已由模块准备，不重复拉。
if(NOT TARGET fgb::sdk)
    fgb_fetch_from_lock(facegaze_sdk "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock.json")
endif()
target_link_libraries(fgb_kit INTERFACE fgb::sdk)

if(WIN32)
    target_link_libraries(fgb_kit INTERFACE ws2_32)
endif()

option(FGB_KIT_BUILD_TESTS "构建 Kit 自测" ON)
if(FGB_KIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
