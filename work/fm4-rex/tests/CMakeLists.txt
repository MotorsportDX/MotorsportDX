cmake_minimum_required(VERSION 3.25)
project(fm4_cpu_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(REX_HEADER_ROOT "" CACHE PATH "SDK include directory; no SDK library is loaded")
if(NOT EXISTS "${REX_HEADER_ROOT}/rex/ppc/context.h")
  message(FATAL_ERROR "Set REX_HEADER_ROOT to the SDK include directory")
endif()
add_executable(fm4_instruction_tests ../src/instruction-tests.cpp)
target_include_directories(fm4_instruction_tests PRIVATE ../src)
target_include_directories(fm4_instruction_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
target_compile_definitions(fm4_instruction_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(fm4_instruction_tests PRIVATE -march=x86-64-v2 -ffp-contract=off)
# This target has no runtime import library, code generator, or DLL-copy step.
# Scene camera math of the runtime's post passes (header only).
add_executable(fm4_post_pass_camera_tests post-pass-camera-tests.cpp)
target_include_directories(fm4_post_pass_camera_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
target_compile_options(fm4_post_pass_camera_tests PRIVATE -march=x86-64-v2 -ffp-contract=off)
# The scene_depth passes of work/fm4-rex/passes on WARP (CPU Direct3D 12), as
# the post pass framework runs them; arguments: post_pass.hlsli, the passes
# directory.
add_executable(fm4_post_pass_shader_tests post-pass-shader-tests.cpp)
target_include_directories(fm4_post_pass_shader_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
target_compile_definitions(fm4_post_pass_shader_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(fm4_post_pass_shader_tests PRIVATE d3d12 dxgi d3dcompiler)
# FXC compiles of the shader override library and the look mods on a synthetic
# contract; arguments: shader_override.hlsli, the look directory.
add_executable(fm4_shader_mod_tests shader-mod-tests.cpp)
target_compile_definitions(fm4_shader_mod_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(fm4_shader_mod_tests PRIVATE d3dcompiler)
# The track lighting file edits of FM4_TRACK_SETTINGS (header only).
add_executable(fm4_track_settings_tests track-settings-tests.cpp)
target_include_directories(fm4_track_settings_tests PRIVATE ../src)
# The texture replacement content hash and DDS reading and writing (header
# only).
add_executable(fm4_texture_replacement_tests texture-replacement-tests.cpp)
target_include_directories(fm4_texture_replacement_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
target_compile_options(fm4_texture_replacement_tests PRIVATE -march=x86-64-v2)
