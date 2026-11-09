cmake_minimum_required(VERSION 3.20)
project(wargame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimized build. Benchmark numbers from a Debug build are
# meaningless, so never measure without this.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_options("$<$<CONFIG:Release>:/O2>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options("$<$<CONFIG:Release>:-O2>")
endif()

# A separate, opt-in build for catching memory errors (heap corruption,
# use-after-free, stack-use-after-return) — never the one benchmark numbers
# come from. Configure a SEPARATE build directory with
# -DWARGAME_SANITIZE=ON: this only adds flags, it never changes what the
# Release build does. Leak detection is separate (the Debug-CRT hook in
# main.cpp): on Windows, AddressSanitizer does not catch leaks, only
# corruption. Both are explained in docs/CODE_ANALYSIS.txt §5 ("Μνήμη / leaks").
option(WARGAME_SANITIZE
       "Build with AddressSanitizer to catch heap corruption and use-after-free"
       OFF)
if(WARGAME_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        # ASan's runtime is incompatible with incremental linking.
        add_link_options(/INCREMENTAL:NO)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# MSVC's ASan runtime is a DLL the linker does not place next to the binary on
# its own, so the exe fails to start (missing-DLL) unless the Developer
# Command Prompt's PATH is active. Copy it next to wargame.exe instead, found
# by searching the VS install rather than relying on an env var.
if(WARGAME_SANITIZE AND MSVC)
    file(GLOB WARGAME_ASAN_DLL
         "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll"
         "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll")
    list(LENGTH WARGAME_ASAN_DLL WARGAME_ASAN_DLL_COUNT)
    if(WARGAME_ASAN_DLL_COUNT GREATER 0)
        list(GET WARGAME_ASAN_DLL 0 WARGAME_ASAN_DLL)
        message(STATUS "WARGAME_SANITIZE: will copy ${WARGAME_ASAN_DLL} next to the binary")
    else()
        message(WARNING
            "WARGAME_SANITIZE: could not find clang_rt.asan_dynamic-x86_64.dll under "
            "a Visual Studio 2022 install; the built exe may fail to start with a "
            "missing-DLL error. Run it from a Developer Command Prompt instead.")
    endif()
endif()

add_executable(wargame
    src/main.cpp

    # configuration
    src/config/Config.cpp

    # grid and routing
    src/world/Side.cpp
    src/world/World.cpp
    src/pathfinding/AStar.cpp

    # discrete-event engine
    src/engine/Event.cpp
    src/engine/Simulation.cpp

    # units and their resources
    src/entities/Resource.cpp
    src/entities/UnitProfile.cpp
    src/entities/PostCombatSurge.cpp
    src/entities/EngagementRules.cpp
    src/entities/DynamicUnit.cpp
    src/entities/infantry/Tactics.cpp
    src/entities/infantry/InfantryBattalion.cpp

    # scenarios
    src/scenarios/PlainBattle.cpp

    # output: battle record and live viewer
    src/log/EventLog.cpp
    src/log/StateDump.cpp
    src/viewer/Viewer.cpp
    src/viewer/ConsoleViewer.cpp
    src/viewer/Win32Viewer.cpp

    # benchmark harness
    src/benchmark/Benchmark.cpp
    src/benchmark/MemoryUsage.cpp

    # built-in self-checks
    src/selfcheck/CheckReport.cpp
    src/selfcheck/GridChecks.cpp
    src/selfcheck/EngineChecks.cpp
    src/selfcheck/UnitChecks.cpp
    src/selfcheck/BattleChecks.cpp
    src/selfcheck/EngagementChecks.cpp
    src/selfcheck/BenchmarkChecks.cpp
    src/selfcheck/SymmetryCheck.cpp
)

target_include_directories(wargame PRIVATE src third_party)

# Keep the config files next to the binary so the program runs from the build
# directory without extra arguments.
add_custom_command(TARGET wargame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:wargame>/config
    COMMENT "Copying config/ next to the binary"
)

if(WARGAME_ASAN_DLL)
    add_custom_command(TARGET wargame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${WARGAME_ASAN_DLL} $<TARGET_FILE_DIR:wargame>
        COMMENT "Copying the AddressSanitizer runtime next to the binary"
    )
endif()
