# Keep shader equations shared with the standalone backend, but compile the
# embedded variants for the host RenderingDevice. The generated header embeds
# the sources, so mobile exports need no runtime shader filesystem paths.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/host_shaders.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/opengl"
        "${PROJECT_BINARY_DIR}/include/host_shaders.h"
    COMMAND_ERROR_IS_FATAL ANY)
file(GLOB _host_shader_inputs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/opengl/*.glsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_host_shader_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/tools/host_shaders.py")
