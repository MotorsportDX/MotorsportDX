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
# The draw resolution scale as a fraction (header only).
add_executable(fm4_draw_scale_tests draw-scale-tests.cpp)
target_include_directories(fm4_draw_scale_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
# The physical pads' stick and trigger shaping and rumble scaling (header only).
add_executable(fm4_pad_shaping_tests pad-shaping-tests.cpp)
target_include_directories(fm4_pad_shaping_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
target_compile_options(fm4_pad_shaping_tests PRIVATE -march=x86-64-v2)
# The keyboard's steering and pedal model, its bindings and the merge of two devices' sticks
# (header only).
add_executable(fm4_keyboard_model_tests keyboard-model-tests.cpp)
target_include_directories(fm4_keyboard_model_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
target_compile_options(fm4_keyboard_model_tests PRIVATE -march=x86-64-v2)
# The scene_depth and final passes of work/fm4-rex/passes on WARP (CPU Direct3D
# 12), as the post pass framework runs them; arguments: post_pass.hlsli, the
# passes directory.
add_executable(fm4_post_pass_shader_tests post-pass-shader-tests.cpp)
target_include_directories(fm4_post_pass_shader_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
target_compile_definitions(fm4_post_pass_shader_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(fm4_post_pass_shader_tests PRIVATE d3d12 dxgi d3dcompiler)
# The passes of type fsr run AMD FSR 3.1 through the runtime's fsr.cpp (a stub without
# REX_FSR); with REX_FSR (a runtime built with it) on the FidelityFX SDK's libraries of its
# cmake/rex_fsr.cmake.
set(REX_RUNTIME_SOURCE "" CACHE PATH "The runtime's source (BuildRoot/runtime-source)")
if(NOT EXISTS "${REX_RUNTIME_SOURCE}/src/graphics/d3d12/fsr.cpp")
  message(FATAL_ERROR "Set REX_RUNTIME_SOURCE to the runtime's source")
endif()
target_sources(fm4_post_pass_shader_tests PRIVATE
               "${REX_RUNTIME_SOURCE}/src/graphics/d3d12/fsr.cpp")
# The native shader contract's manifest and pass classes (docs/analysis/shader-contract.md):
# the header and the runtime's parser, with the installed toml++; argument: the look's
# shaders.toml.
add_executable(fm4_native_shader_contract_tests native-shader-contract-tests.cpp
               "${REX_RUNTIME_SOURCE}/src/graphics/d3d12/native_shader_contract.cpp")
target_include_directories(fm4_native_shader_contract_tests SYSTEM PRIVATE "${REX_HEADER_ROOT}")
option(REX_FSR "Run the fsr passes with AMD FSR 3.1 (REX_FSR_SDK_DIR)" OFF)
if(REX_FSR)
  set(REX_FSR_SDK_DIR "" CACHE PATH "The FidelityFX SDK v1.1.4 checkout")
  include("${REX_RUNTIME_SOURCE}/cmake/rex_fsr.cmake")
  target_compile_definitions(fm4_post_pass_shader_tests PRIVATE REX_FSR=1)
  target_link_libraries(fm4_post_pass_shader_tests PRIVATE ffx_fsr3upscaler_x64
                        ffx_backend_dx12_x64)
endif()
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
