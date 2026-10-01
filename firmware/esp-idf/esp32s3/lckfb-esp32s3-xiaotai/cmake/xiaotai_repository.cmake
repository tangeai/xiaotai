# Resolve the XiaoTai repository root without depending on project nesting or
# parent CMake scope. ESP-IDF evaluates component requirements separately, so
# every consumer may call this helper directly.
function(xiaotai_find_repository_root output_variable)
    get_filename_component(candidate "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    while(NOT EXISTS "${candidate}/boards/schema/board.schema.json")
        get_filename_component(parent "${candidate}" DIRECTORY)
        if(parent STREQUAL candidate)
            message(FATAL_ERROR
                "Cannot locate XiaoTai repository root from ${CMAKE_CURRENT_LIST_DIR}")
        endif()
        set(candidate "${parent}")
    endwhile()
    set(${output_variable} "${candidate}" PARENT_SCOPE)
endfunction()
