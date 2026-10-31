# fm4_launcher - the GUI launcher, MotorsportDX.exe (docs/running.md#gui-launcher). The target and
# folder keep the internal name; only the program file carries the project's name.
#
# Built by pipeline.py build against the installed runtime's SDL3 and the runtime source's
# Dear ImGui and JSON header (FM4_THIRDPARTY: <BuildRoot>/runtime-source/thirdparty).

cmake_minimum_required(VERSION 3.25)
project(fm4_launcher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FM4_THIRDPARTY "" CACHE PATH "The runtime source's thirdparty directory")
if(NOT EXISTS "${FM4_THIRDPARTY}/imgui/imgui.h")
    message(FATAL_ERROR "Set FM4_THIRDPARTY to <BuildRoot>/runtime-source/thirdparty")
endif()

# The runtime's source, for the racing wheel's host (default: beside FM4_THIRDPARTY).
set(FM4_RUNTIME_SOURCE "${FM4_THIRDPARTY}/.." CACHE PATH "The runtime's source (BuildRoot/runtime-source)")

find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-static)

set(IMGUI_DIR "${FM4_THIRDPARTY}/imgui")
add_executable(fm4_launcher WIN32
    src/app.cpp
    src/discs.cpp
    src/plan.cpp
    src/runner.cpp
    src/screenshot.cpp
    src/settings.cpp
    src/shaders.cpp
    src/tests.cpp
    # The racing wheel's host, shared with the runtime (its wheel test, docs/running.md).
    ${FM4_RUNTIME_SOURCE}/src/input/wheel/wheel_host.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(fm4_launcher PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${FM4_THIRDPARTY}/inja/third_party/include
)
# Only rex/input/wheel of the runtime's headers, which need nothing else of the runtime.
target_include_directories(fm4_launcher PRIVATE ${FM4_RUNTIME_SOURCE}/include)
target_compile_definitions(fm4_launcher PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(fm4_launcher PRIVATE SDL3::SDL3-static d3d11 dxgi d3dcompiler bcrypt shcore shell32 advapi32 user32 urlmon ole32 windowscodecs)
if(MINGW)
    # llvm-mingw (docs/analysis/mingw-toolchain.md): the Windows 10 declarations (mingw-w64's
    # headers default to Windows 7), -municode for wWinMain, and libc++ linked into the program,
    # which ships on its own.
    target_compile_definitions(fm4_launcher PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_options(fm4_launcher PRIVATE -municode -static)
endif()
set_target_properties(fm4_launcher PROPERTIES OUTPUT_NAME MotorsportDX)
