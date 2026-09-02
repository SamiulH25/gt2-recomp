cmake_minimum_required(VERSION 3.20)
project(GT2Recomp LANGUAGES C CXX)

# Stub hybrid runtime (GTFS/overlay standalone test) - keep existing gt2 target
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_library(gt2_runtime STATIC
    runtime/gtfs.c
    runtime/bios.c
    runtime/gte.c
    runtime/gpu.c
    runtime/overlay.c
)
add_executable(gt2
    src/main.c
)
find_package(ZLIB REQUIRED)
target_include_directories(gt2 PRIVATE runtime)
target_link_libraries(gt2 PRIVATE gt2_runtime m ZLIB::ZLIB)
target_link_libraries(gt2_runtime PRIVATE ZLIB::ZLIB)

# PSXRecomp game runtime - native recompiled GT2 via psxrecomp framework
# Builds gt2-psxrecomp executable in same repo (optional, requires generated/ present)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH "Path to psxrecomp")
# Disable UI/wizard for minimal build (no recomp-ui submodule) - must be set BEFORE including runtime.cmake
set(PSX_RECOMP_UI OFF CACHE BOOL "" FORCE)
set(PSX_SETUP_WIZARD OFF CACHE BOOL "" FORCE)
set(PSX_NETPLAY OFF CACHE BOOL "" FORCE)
set(PSX_REWIND OFF CACHE BOOL "" FORCE)
if(EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

    # Only define psx-runtime target if generated code exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/SCUS_944.88_dispatch.c")
        psxrecomp_add_game_runtime(gt2-psxrecomp
            WINDOW_TITLE "Gran Turismo 2 Recompiled"
            DEFAULT_GAME_CONFIG_PATH "game.toml"
            MAX_PLAYERS 2
            GEN_MARKER "generated/SCUS_944.88_dispatch.c"
            GEN_FULL_GLOB "generated/SCUS_944.88_full_*.c"
        )
        # Stage game.toml and disc images beside binary for direct launch
        add_custom_command(TARGET gt2-psxrecomp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:gt2-psxrecomp>/disc"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/game.toml"
                "$<TARGET_FILE_DIR:gt2-psxrecomp>/game.toml"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/disc/Gran Turismo 2 (USA) (Simulation Mode) (v1.2).bin"
                "$<TARGET_FILE_DIR:gt2-psxrecomp>/disc/Gran Turismo 2 (USA) (Simulation Mode) (v1.2).bin"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/disc/Gran Turismo 2 (USA) (Simulation Mode) (v1.2).cue"
                "$<TARGET_FILE_DIR:gt2-psxrecomp>/disc/Gran Turismo 2 (USA) (Simulation Mode) (v1.2).cue"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/disc/SCUS_944.88"
                "$<TARGET_FILE_DIR:gt2-psxrecomp>/disc/SCUS_944.88"
            VERBATIM)
        message(STATUS "GT2 psxrecomp target gt2-psxrecomp enabled (358 shards)")
    else()
        message(STATUS "GT2 psxrecomp target skipped - run /tmp/psxrecomp_build/psxrecomp-game --config game.toml to generate")
    endif()
endif()
