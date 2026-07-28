cmake_minimum_required(VERSION 3.16)

project(empower-fleet
    VERSION 1.0.0
    DESCRIPTION "Empower Plant: Fleet Control Center - a Sentry Native SDK demo"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Put all executables directly in the build dir, including for multi-config
# generators (Visual Studio), so binary paths are identical on every platform.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${_cfg} _CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} ${CMAKE_BINARY_DIR})
endforeach()

# Always keep frame pointers & debug info so stacks symbolicate well in the demo.
if(NOT MSVC)
    add_compile_options(-g -fno-omit-frame-pointer)
endif()

# Derive the Sentry release from the git commit so the release the binary reports
# matches the one CI creates (with debug files + commits) for that same build.
set(EMPOWER_RELEASE "empower.native@1.0.0")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _git_sha OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE _git_rc)
    if(_git_rc EQUAL 0 AND _git_sha)
        set(EMPOWER_RELEASE "empower.native@${_git_sha}")
    endif()
endif()
message(STATUS "Empower release: ${EMPOWER_RELEASE}")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(EMPOWER_BUILD_GUI "Build the ImGui Fleet Control Center GUI" ON)
option(EMPOWER_BUILD_SMOKE "Build the connectivity smoke-test binary" ON)
set(EMPOWER_SENTRY_NATIVE_VERSION "0.15.2" CACHE STRING "sentry-native release tag to fetch")

include(FetchContent)

# ---------------------------------------------------------------------------
# sentry-native (new out-of-process "native" crash backend: no breakpad/crashpad/inproc)
# ---------------------------------------------------------------------------
set(SENTRY_BACKEND "native" CACHE STRING "" FORCE)
set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(sentry
    GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
    GIT_TAG ${EMPOWER_SENTRY_NATIVE_VERSION}
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(sentry)

# ---------------------------------------------------------------------------
# Shared demo core (Sentry wiring, independent of the renderer)
# ---------------------------------------------------------------------------
add_library(empower_core STATIC
    src/core/sentry_manager.cpp
    src/core/offline_queue_monitor.cpp
    src/core/platform.cpp
    src/core/backend_client.cpp
    src/core/command_server.cpp
    src/app/console_log.cpp
    src/app/fleet_model.cpp
    src/chaos/chaos.cpp)
find_package(Threads REQUIRED)
target_include_directories(empower_core PUBLIC src)
target_link_libraries(empower_core PUBLIC sentry::sentry Threads::Threads)
target_compile_definitions(empower_core PUBLIC EMPOWER_RELEASE="${EMPOWER_RELEASE}")

# Backend HTTP client: libcurl where available (Linux/macOS), WinHTTP on Windows
# (the runners have no libcurl), so the distributed-trace call works everywhere.
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(empower_core PUBLIC CURL::libcurl)
    target_compile_definitions(empower_core PUBLIC EMPOWER_HAVE_CURL=1)
elseif(WIN32)
    target_link_libraries(empower_core PUBLIC winhttp)
endif()

# Winsock for the remote command server (used on Windows).
# NOMINMAX: Windows headers define min/max macros that break std::min/std::max.
if(WIN32)
    target_compile_definitions(empower_core PUBLIC NOMINMAX)
    target_link_libraries(empower_core PUBLIC ws2_32)
endif()

# Copy the native backend's out-of-process crash daemon (sentry-crash) next to a
# target so the SDK can launch it at runtime. The native backend builds this as
# the `sentry-crash` executable target.
function(empower_bundle_daemon target)
    if(TARGET sentry-crash)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:sentry-crash> $<TARGET_FILE_DIR:${target}>
            COMMENT "Bundling sentry-crash daemon next to ${target}")
    endif()
endfunction()

# Ad-hoc codesign an executable on macOS with the get-task-allow entitlement so
# the native backend's crash daemon can task_for_pid() it and write a FULL
# minidump (instead of the minimal fallback). No certificate required.
function(empower_codesign target)
    if(APPLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --sign - --timestamp=none
                --entitlements ${CMAKE_SOURCE_DIR}/cmake/get-task-allow.entitlements
                $<TARGET_FILE:${target}>
            COMMENT "Ad-hoc signing ${target} with get-task-allow (full minidumps)"
            VERBATIM)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Offline-cache test — Go Offline must queue soft events and crashes on disk
# ---------------------------------------------------------------------------
add_executable(empower-test-offline test/offline_cache_test.cpp)
target_link_libraries(empower-test-offline PRIVATE empower_core)
empower_bundle_daemon(empower-test-offline)
empower_codesign(empower-test-offline)

# ---------------------------------------------------------------------------
# Connectivity smoke test — exercises the SDK + native backend end to end
# ---------------------------------------------------------------------------
if(EMPOWER_BUILD_SMOKE)
    add_executable(empower-smoke src/smoke/main.cpp)
    target_link_libraries(empower-smoke PRIVATE empower_core)
    empower_bundle_daemon(empower-smoke)
    empower_codesign(empower-smoke)
endif()

# ---------------------------------------------------------------------------
# Headless runner: autopilot + remote command listener (no window). For CI.
# ---------------------------------------------------------------------------
add_executable(empower-headless src/headless/main.cpp)
target_link_libraries(empower-headless PRIVATE empower_core)
empower_bundle_daemon(empower-headless)
empower_codesign(empower-headless)
# Bundle a representative dashboard snapshot the headless runner attaches as the
# event screenshot (it renders no UI of its own).
add_custom_command(TARGET empower-headless POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fleet-snapshot.png
        $<TARGET_FILE_DIR:empower-headless>/screenshot.png
    COMMENT "Bundling dashboard snapshot for headless screenshot attachment")

# ---------------------------------------------------------------------------
# The Fleet Control Center GUI (GLFW + Dear ImGui + OpenGL3)
# ---------------------------------------------------------------------------
if(EMPOWER_BUILD_GUI)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    # X11 only: avoids needing the Wayland toolchain (wayland-scanner et al.) on
    # Linux CI. X11 apps run fine under Wayland compositors via XWayland.
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)

    # Dear ImGui ships no CMakeLists, so we compile it into a small static lib.
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui)

    find_package(OpenGL REQUIRED)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    # Fetch the Rubik font (used by Sentry's brand). Optional: the app falls
    # back to the built-in font if the download is unavailable.
    set(_empower_fontdir ${CMAKE_BINARY_DIR}/assets/fonts)
    file(MAKE_DIRECTORY ${_empower_fontdir})
    foreach(_font Rubik-Regular Rubik-Medium)
        if(NOT EXISTS ${_empower_fontdir}/${_font}.ttf)
            file(DOWNLOAD
                https://github.com/googlefonts/rubik/raw/main/old/version-1/4_fonts/ttf/${_font}.ttf
                ${_empower_fontdir}/${_font}.ttf
                INACTIVITY_TIMEOUT 20 STATUS _font_status)
            list(GET _font_status 0 _font_rc)
            if(NOT _font_rc EQUAL 0)
                message(STATUS "Rubik font ${_font} unavailable; using built-in font")
                file(REMOVE ${_empower_fontdir}/${_font}.ttf)
            endif()
        endif()
    endforeach()

    # Font Awesome 6 Free (Solid) for the nav icons. Optional: the UI falls
    # back to text-only labels if it isn't available.
    if(NOT EXISTS ${_empower_fontdir}/fa-solid-900.ttf)
        file(DOWNLOAD
            https://github.com/FortAwesome/Font-Awesome/raw/6.7.2/webfonts/fa-solid-900.ttf
            ${_empower_fontdir}/fa-solid-900.ttf
            INACTIVITY_TIMEOUT 20 STATUS _fa_status)
        list(GET _fa_status 0 _fa_rc)
        if(NOT _fa_rc EQUAL 0)
            message(STATUS "Font Awesome unavailable; nav icons disabled")
            file(REMOVE ${_empower_fontdir}/fa-solid-900.ttf)
        endif()
    endif()

    # stb_image_write (public domain) for the offscreen PNG screenshot path.
    set(_empower_gendir ${CMAKE_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${_empower_gendir})
    if(NOT EXISTS ${_empower_gendir}/stb_image_write.h)
        file(DOWNLOAD
            https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
            ${_empower_gendir}/stb_image_write.h
            INACTIVITY_TIMEOUT 20 STATUS _stb_status)
        list(GET _stb_status 0 _stb_rc)
        if(NOT _stb_rc EQUAL 0)
            file(REMOVE ${_empower_gendir}/stb_image_write.h)
        endif()
    endif()

    add_executable(empower-fleet
        src/app/main_gui.cpp
        src/app/telemetry_feed.cpp
        src/app/theme.cpp
        src/app/ui.cpp)
    target_link_libraries(empower-fleet PRIVATE empower_core imgui)
    target_compile_definitions(empower-fleet PRIVATE GL_SILENCE_DEPRECATION)
    if(EXISTS ${_empower_gendir}/stb_image_write.h)
        target_include_directories(empower-fleet PRIVATE ${_empower_gendir})
        target_compile_definitions(empower-fleet PRIVATE EMPOWER_HAVE_STB=1)
    endif()
    empower_bundle_daemon(empower-fleet)
    empower_codesign(empower-fleet)
endif()
