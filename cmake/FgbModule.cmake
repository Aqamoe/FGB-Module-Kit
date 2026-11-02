# 模块仓库的构建约定：一个函数搞定核心库、DLL、单测、ABI 冒烟与发布包。
#
# 由模块在拉到 Kit 之后 include，所以每个模块的 CMakeLists 只有十几行。
if(FGB_MODULE_CMAKE_INCLUDED)
    return()
endif()
set(FGB_MODULE_CMAKE_INCLUDED TRUE)

function(fgb_add_module)
    cmake_parse_arguments(M
        ""
        "NAME;MAIN;SMOKE_SOURCE"
        "SOURCES;TEST_SOURCES;LINK_LIBRARIES"
        ${ARGN})

    if(NOT M_NAME)
        message(FATAL_ERROR "fgb_add_module 需要 NAME")
    endif()
    if(NOT M_MAIN)
        message(FATAL_ERROR "fgb_add_module 需要 MAIN（含 fgb_module_query_v2 的入口）")
    endif()

    # 核心逻辑单独成 OBJECT 库：单测直接链接它，不用把 DLL 拉进来。
    add_library(${M_NAME}-core OBJECT ${M_SOURCES})
    target_link_libraries(${M_NAME}-core PRIVATE fgb::kit ${M_LINK_LIBRARIES})
    target_include_directories(${M_NAME}-core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

    add_library(${M_NAME} MODULE ${M_MAIN} $<TARGET_OBJECTS:${M_NAME}-core>)
    target_link_libraries(${M_NAME} PRIVATE fgb::kit ${M_LINK_LIBRARIES})
    target_include_directories(${M_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set_target_properties(${M_NAME} PROPERTIES PREFIX "" OUTPUT_NAME "${M_NAME}")

    if(MSVC)
        foreach(_target ${M_NAME}-core ${M_NAME})
            target_compile_options(${_target} PRIVATE /utf-8 /W4 /permissive-)
        endforeach()
    endif()

    # 清单与图标随 DLL 落到输出目录，本地压 zip 或手工安装都能直接用。
    file(GLOB_RECURSE _resources
        "${CMAKE_CURRENT_SOURCE_DIR}/module/*.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/module/*.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/module/*.png")
    foreach(_resource ${_resources})
        get_filename_component(_file "${_resource}" NAME)
        add_custom_command(TARGET ${M_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_resource}" "$<TARGET_FILE_DIR:${M_NAME}>/${_file}"
            VERBATIM)
    endforeach()

    enable_testing()

    foreach(_test ${M_TEST_SOURCES})
        get_filename_component(_stem "${_test}" NAME_WE)
        add_executable(${M_NAME}-${_stem} ${_test})
        target_link_libraries(${M_NAME}-${_stem} PRIVATE fgb::kit ${M_NAME}-core)
        target_include_directories(${M_NAME}-${_stem} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
        if(MSVC)
            target_compile_options(${M_NAME}-${_stem} PRIVATE /utf-8 /W4 /permissive-)
        endif()
        add_test(NAME ${M_NAME}-${_stem} COMMAND ${M_NAME}-${_stem})
    endforeach()

    if(M_SMOKE_SOURCE)
        # 走真实 ABI：LoadLibrary 后按宿主顺序调用。需要空闲端口，所以不挂进 ctest。
        add_executable(${M_NAME}-abi-smoke ${M_SMOKE_SOURCE})
        target_link_libraries(${M_NAME}-abi-smoke PRIVATE fgb::kit)
        # 冒烟程序常会复用模块自己的头（协议常量、字段表），所以源码目录也要给到。
        target_include_directories(${M_NAME}-abi-smoke PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
        if(MSVC)
            target_compile_options(${M_NAME}-abi-smoke PRIVATE /utf-8 /W4 /permissive-)
        endif()
        add_dependencies(${M_NAME}-abi-smoke ${M_NAME})
        add_custom_target(smoke-test
            COMMAND ${M_NAME}-abi-smoke "$<TARGET_FILE:${M_NAME}>"
            DEPENDS ${M_NAME}-abi-smoke ${M_NAME}
            COMMENT "通过真实 ABI 加载 ${M_NAME}.dll"
            VERBATIM)
    endif()

    add_custom_target(package-modules
        COMMAND "${CMAKE_COMMAND}"
                "-DFGB_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DFGB_TARGET_FILE=$<TARGET_FILE:${M_NAME}>"
                "-DFGB_OUTPUT_DIR=${CMAKE_BINARY_DIR}/Release"
                -P "${FGB_KIT_CMAKE_DIR}/FgbPackageScript.cmake"
        DEPENDS ${M_NAME}
        COMMENT "按 module/**/fgb-native.json 产出发布 zip"
        VERBATIM)
endfunction()
