# SDL2 and SDL3 share names with incompatible signatures. Rename SDL3 and
# impacto's references together; ELF symbol versions alone do not prevent an
# already-loaded unversioned SDL2 definition from intercepting SDL3 calls.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND IMPACTO_EMBEDDED)
    file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/host-exports.map"
        CONTENT "{ global: impacto_host_*; local: *; };\n" @ONLY)
    target_link_options(impacto PRIVATE
        "LINKER:--version-script=${PROJECT_BINARY_DIR}/host-exports.map")
    set_property(TARGET impacto APPEND PROPERTY LINK_DEPENDS "${PROJECT_BINARY_DIR}/host-exports.map")
endif()

# Android uses static SDL3, alongside the host's shared SDL2 and Godot's own
# SDL3. Rename both definitions and impacto's references before the final link.
if((ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND IMPACTO_EMBEDDED)
    get_target_property(_sdl3_type SDL3::SDL3 TYPE)
    get_target_property(_sdl3_library SDL3::SDL3 IMPORTED_LOCATION_RELEASE)
    if(NOT _sdl3_library)
        get_target_property(_sdl3_library SDL3::SDL3 IMPORTED_LOCATION)
    endif()
    set(_sdl3_nm_options --extern-only --defined-only --format=posix)
    if(_sdl3_type STREQUAL "SHARED_LIBRARY")
        list(APPEND _sdl3_nm_options -D)
    endif()
    execute_process(COMMAND "${CMAKE_NM}" ${_sdl3_nm_options} "${_sdl3_library}"
        OUTPUT_VARIABLE _sdl3_symbols COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX REPLACE "@@?[^ \n]+" "" _sdl3_symbols "${_sdl3_symbols}")
    string(REGEX MATCHALL "(SDL_|PLATFORM_hid_|hid_)[A-Za-z0-9_]+ [A-Za-z] " _sdl3_exports "${_sdl3_symbols}")
    list(REMOVE_DUPLICATES _sdl3_exports)
    if(NOT _sdl3_exports)
        message(FATAL_ERROR "Embedded SDL3 has no exported SDL symbols")
    endif()
    set(_sdl3_map "")
    set(_sdl3_content "#pragma once\n")
    foreach(_symbol IN LISTS _sdl3_exports)
        string(REGEX REPLACE " .*" "" _name "${_symbol}")
        string(APPEND _sdl3_map "${_name} AetherSDL3_${_name}\n")
        string(APPEND _sdl3_content "#define ${_name} AetherSDL3_${_name}\n")
    endforeach()
    file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/include/host_sdl3.h" CONTENT "${_sdl3_content}" @ONLY)
    file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/sdl3-symbols.txt" CONTENT "${_sdl3_map}" @ONLY)
    if(_sdl3_type STREQUAL "SHARED_LIBRARY")
        find_program(IMPACTO_PATCHELF patchelf REQUIRED)
        set(_sdl3_isolated "${PROJECT_BINARY_DIR}/libAetherSDL3.so")
        add_custom_command(OUTPUT "${_sdl3_isolated}"
            COMMAND "${IMPACTO_PATCHELF}" --rename-dynamic-symbols "${PROJECT_BINARY_DIR}/sdl3-symbols.txt"
                --set-soname libAetherSDL3.so --output "${_sdl3_isolated}" "${_sdl3_library}"
            DEPENDS "${_sdl3_library}" "${PROJECT_BINARY_DIR}/sdl3-symbols.txt" VERBATIM)
        add_library(impacto_sdl3 SHARED IMPORTED)
    else()
        set(_sdl3_isolated "${PROJECT_BINARY_DIR}/libAetherSDL3.a")
        add_custom_command(OUTPUT "${_sdl3_isolated}"
            COMMAND "${CMAKE_OBJCOPY}" "--redefine-syms=${PROJECT_BINARY_DIR}/sdl3-symbols.txt"
                "${_sdl3_library}" "${_sdl3_isolated}"
            DEPENDS "${_sdl3_library}" "${PROJECT_BINARY_DIR}/sdl3-symbols.txt" VERBATIM)
        add_library(impacto_sdl3 STATIC IMPORTED)
    endif()
    add_custom_target(impacto_sdl3_archive DEPENDS "${_sdl3_isolated}")
    set_target_properties(impacto_sdl3 PROPERTIES IMPORTED_LOCATION "${_sdl3_isolated}")
    foreach(_property INTERFACE_INCLUDE_DIRECTORIES INTERFACE_LINK_LIBRARIES INTERFACE_COMPILE_DEFINITIONS INTERFACE_LINK_OPTIONS)
        get_target_property(_value SDL3::SDL3 ${_property})
        if(_value)
            set_target_properties(impacto_sdl3 PROPERTIES ${_property} "${_value}")
        endif()
    endforeach()
    add_dependencies(impacto_sdl3 impacto_sdl3_archive)
    list(REMOVE_ITEM Impacto_Libs "${SDL3_LIBRARIES}")
    list(APPEND Impacto_Libs impacto_sdl3)
    target_compile_options(impacto PRIVATE -include "${PROJECT_BINARY_DIR}/include/host_sdl3.h")
endif()
