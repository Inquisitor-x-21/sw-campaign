cmake_minimum_required(VERSION 3.20)
project(RDR2Coop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static runtime: the plugin then needs no extra Visual C++ DLLs inside the game.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(RDR2COOP_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH
    "Folder holding the Script Hook RDR2 SDK (must contain inc/main.h and lib/ScriptHookRDR2.lib)")
option(RDR2COOP_PLUGIN_COMPILE_CHECK
    "Compile the plugin against tools/ci/sdk_stub without linking (for checks on machines without the SDK)" OFF)
option(RDR2COOP_BUILD_TESTS "Build the unit tests" ON)

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
if(MSVC)
    add_compile_options(/utf-8 /permissive- /W4 /wd4100 /wd4996)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()
if(MINGW)
    # Self-contained .exe files (no MinGW runtime DLLs needed next to them).
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# --------------------------------------------------------------------- ENet
set(ENET_DIR "${CMAKE_SOURCE_DIR}/third_party/enet")
add_library(enet STATIC
    ${ENET_DIR}/callbacks.c ${ENET_DIR}/compress.c ${ENET_DIR}/host.c ${ENET_DIR}/list.c
    ${ENET_DIR}/packet.c ${ENET_DIR}/peer.c ${ENET_DIR}/protocol.c ${ENET_DIR}/unix.c ${ENET_DIR}/win32.c)
target_include_directories(enet PUBLIC ${ENET_DIR}/include)
if(WIN32)
    target_link_libraries(enet PUBLIC ws2_32 winmm)
    target_compile_definitions(enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
else()
    target_compile_definitions(enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1
        HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1 HAS_SOCKLEN_T=1)
endif()
if(MSVC)
    target_compile_options(enet PRIVATE /W0)
else()
    target_compile_options(enet PRIVATE -w)
endif()

# --------------------------------------------------------------- core (portable)
add_library(coop_core STATIC
    core/src/serialize.cpp
    core/src/protocol.cpp
    core/src/interp.cpp
    core/src/session.cpp
    core/src/config.cpp
    core/src/log.cpp)
target_include_directories(coop_core PUBLIC core/include)
target_link_libraries(coop_core PRIVATE enet)
target_compile_definitions(coop_core PUBLIC RDR2COOP_VERSION="${PROJECT_VERSION}")

# ------------------------------------------------------------ test bots + tests
add_executable(coopbot tools/coopbot/main.cpp)
target_link_libraries(coopbot PRIVATE coop_core)

if(RDR2COOP_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/test_main.cpp)
    target_link_libraries(core_tests PRIVATE coop_core enet)
    target_compile_definitions(core_tests PRIVATE RDR2COOP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME core_tests COMMAND core_tests)

    # The plugin's game-side code against a simulated world (Linux/macOS only:
    # the simulated SDK header stands in for windows.h).
    if(NOT WIN32)
        add_executable(plugin_sim_tests tests/sim_test.cpp tests/sim/fake_game.cpp
            plugin/game.cpp plugin/avatar.cpp plugin/overlay.cpp)
        target_include_directories(plugin_sim_tests PRIVATE tests/sim plugin)
        target_link_libraries(plugin_sim_tests PRIVATE coop_core)
        add_test(NAME plugin_sim_tests COMMAND plugin_sim_tests)

        # The main loop (plugin/script.cpp) on a lockstep "script thread", with
        # real sessions on loopback as the other players.
        find_package(Threads REQUIRED)
        add_executable(plugin_script_tests tests/script_test.cpp tests/sim/fake_game.cpp tests/sim/fake_windows.cpp
            plugin/script.cpp plugin/game.cpp plugin/avatar.cpp plugin/overlay.cpp)
        target_include_directories(plugin_script_tests PRIVATE tests/sim plugin)
        target_link_libraries(plugin_script_tests PRIVATE coop_core Threads::Threads)
        add_test(NAME plugin_script_tests COMMAND plugin_script_tests)
        add_test(NAME plugin_script_tests_log_fallback COMMAND plugin_script_tests log-fallback)
    endif()
endif()

# ------------------------------------------------------------- the game plugin
set(PLUGIN_SOURCES
    plugin/dllmain.cpp
    plugin/script.cpp
    plugin/game.cpp
    plugin/avatar.cpp
    plugin/overlay.cpp)

if(RDR2COOP_PLUGIN_COMPILE_CHECK)
    add_library(rdr2coop_compile_check OBJECT ${PLUGIN_SOURCES})
    target_include_directories(rdr2coop_compile_check PRIVATE plugin tools/ci/sdk_stub/inc)
    target_link_libraries(rdr2coop_compile_check PRIVATE coop_core)
elseif(WIN32)
    if(EXISTS "${RDR2COOP_SDK_DIR}/inc/main.h" AND EXISTS "${RDR2COOP_SDK_DIR}/lib/ScriptHookRDR2.lib")
        add_library(RDR2Coop SHARED ${PLUGIN_SOURCES})
        set_target_properties(RDR2Coop PROPERTIES SUFFIX ".asi" PREFIX "")
        target_include_directories(RDR2Coop PRIVATE plugin "${RDR2COOP_SDK_DIR}/inc")
        target_link_libraries(RDR2Coop PRIVATE coop_core ws2_32 "${RDR2COOP_SDK_DIR}/lib/ScriptHookRDR2.lib")
        add_custom_command(TARGET RDR2Coop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/config/RDR2Coop.ini" "$<TARGET_FILE_DIR:RDR2Coop>/RDR2Coop.ini"
            VERBATIM)  # paths like "rdr2-coop (1)" or "C:/Users/First Last" stay one argument
    else()
        message(WARNING
            "Script Hook RDR2 SDK not found in ${RDR2COOP_SDK_DIR}. Building the bots and tests only.\n"
            "Extract the SDK so that sdk/inc/main.h and sdk/lib/ScriptHookRDR2.lib exist, then re-run.")
    endif()
endif()
