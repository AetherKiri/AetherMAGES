# Fail on every native platform if a future change brings SDL runtime calls
# back into the Godot host, whether imported or statically linked. The basic
# symbol listing works with Apple nm, GNU nm and LLVM nm.
execute_process(COMMAND "${NM}" "${BINARY}"
    OUTPUT_VARIABLE symbols COMMAND_ERROR_IS_FATAL ANY)
if(symbols MATCHES "(^|[ \t\n])_?(SDL_|AetherSDL3_)[A-Za-z0-9_]+")
    message(FATAL_ERROR "Embedded MAGES must not reference the SDL runtime: ${CMAKE_MATCH_0}")
endif()
