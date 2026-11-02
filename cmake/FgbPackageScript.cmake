# package-modules 的实现脚本：按 module/**/fgb-native.json 逐个产出发布 zip。
#
# 用脚本而不是几十条 POST_BUILD，是为了让打包规则可读、可单独跑，
# 也方便一个仓库出多个产物（ALXR Local / Remote）。
if(NOT DEFINED FGB_SOURCE_DIR OR NOT DEFINED FGB_TARGET_FILE OR NOT DEFINED FGB_OUTPUT_DIR)
    message(FATAL_ERROR "缺少 FGB_SOURCE_DIR / FGB_TARGET_FILE / FGB_OUTPUT_DIR")
endif()

file(GLOB_RECURSE manifests
    "${FGB_SOURCE_DIR}/module/*/fgb-native.json"
    "${FGB_SOURCE_DIR}/module/fgb-native.json")
list(REMOVE_DUPLICATES manifests)
list(SORT manifests)

if(NOT manifests)
    message(FATAL_ERROR "module/ 下找不到 fgb-native.json")
endif()

file(MAKE_DIRECTORY "${FGB_OUTPUT_DIR}")
set(stage_root "${FGB_OUTPUT_DIR}/stage")
file(REMOVE_RECURSE "${stage_root}")

foreach(manifest ${manifests})
    file(READ "${manifest}" json)
    string(JSON version GET "${json}" Version)
    string(JSON dll GET "${json}" DllFileName)
    string(JSON module_id GET "${json}" ModuleId)
    get_filename_component(dll_stem "${dll}" NAME_WE)
    get_filename_component(manifest_dir "${manifest}" DIRECTORY)

    set(stage "${stage_root}/${dll_stem}")
    file(MAKE_DIRECTORY "${stage}")
    file(COPY_FILE "${FGB_TARGET_FILE}" "${stage}/${dll}")
    file(COPY_FILE "${manifest}" "${stage}/fgb-native.json")
    file(COPY_FILE "${FGB_SOURCE_DIR}/LICENSE" "${stage}/LICENSE")
    file(COPY_FILE "${FGB_SOURCE_DIR}/NOTICE" "${stage}/NOTICE")

    # 图标跟着清单走：单产物在 module/，多产物在 module/<产品>/
    set(icon_files "")
    foreach(candidate icon.svg icon.png)
        foreach(dir "${manifest_dir}" "${FGB_SOURCE_DIR}/module")
            if(EXISTS "${dir}/${candidate}")
                file(COPY_FILE "${dir}/${candidate}" "${stage}/${candidate}")
                list(APPEND icon_files "${candidate}")
                break()
            endif()
        endforeach()
    endforeach()

    set(payload "${dll}" fgb-native.json LICENSE NOTICE ${icon_files})
    foreach(entry ${payload})
        if(NOT EXISTS "${stage}/${entry}")
            message(FATAL_ERROR "${module_id} 的发布包缺少 ${entry}")
        endif()
    endforeach()

    set(zip "${FGB_OUTPUT_DIR}/${dll_stem}-${version}.zip")
    file(REMOVE "${zip}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${zip}" --format=zip ${payload}
        WORKING_DIRECTORY "${stage}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "压缩失败：${zip}")
    endif()
    file(SIZE "${zip}" size)
    message(STATUS "打包 ${module_id} ${version} -> ${zip} (${size} 字节)")
endforeach()
