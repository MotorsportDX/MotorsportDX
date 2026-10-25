# rex_fsr.cmake: AMD FidelityFX FSR 3.1 (the FSR 3 upscaler of the FidelityFX SDK v1.1.4),
# built from source into two static libraries and linked into rexgpu-xenos when REX_FSR is on
# (docs/analysis/fsr-plan.md, sections 9.7 and 10).
#
# REX_FSR_SDK_DIR is the SDK checkout that setup fetches outside the repository
# (<BuildRoot>\fidelityfx-sdk, a sparse checkout of commit
# c6efa6bf7f2027b3ec94f28578bb5965eabb9e55). The SDK's own CMakeLists.txt adds every
# component and the PIX library, which the sparse checkout does not hold, so this module sets
# the variables that file sets and adds only the FSR 3 upscaler component and the DX12 backend
# with their own CMake files. The backend's CMake compiles the shader permutations with the
# SDK's shader compiler (tools/binary_store/FidelityFX_SC.exe and its DXC) into headers in the
# build tree. No prebuilt AMD DLL is used.
#
# With the target rexgpu-xenos, links them into it and defines REX_FSR=1 there (d3d12/fsr.cpp
# then holds the upscaler); without it (the CPU tests, work/fm4-rex/tests), only the libraries.

if(NOT REX_FSR)
    return()
endif()

if(NOT WIN32)
    message(FATAL_ERROR "REX_FSR needs Windows and the D3D12 backend")
endif()

set(_rex_fsr_sdk "${REX_FSR_SDK_DIR}/sdk")
foreach(_rex_fsr_file IN ITEMS
        LICENSE.txt
        include/FidelityFX/host/ffx_fsr3upscaler.h
        src/components/fsr3upscaler/CMakeLists.txt
        src/backends/dx12/CMakeLists.txt
        src/backends/shared/ffx_shader_blobs.cpp
        tools/binary_store/FidelityFX_SC.exe)
    if(NOT EXISTS "${_rex_fsr_sdk}/${_rex_fsr_file}")
        message(FATAL_ERROR
            "REX_FSR: ${_rex_fsr_sdk}/${_rex_fsr_file} not found. Set REX_FSR_SDK_DIR to the "
            "FidelityFX SDK checkout that setup fetches (<BuildRoot>\\fidelityfx-sdk).")
    endif()
endforeach()

# What the SDK's CMakeLists.txt sets for FFX_API_BACKEND=DX12_X64 with only the FSR 3
# upscaler, as static libraries, with the shaders compiled as a prebuild step.
set(FFX_API_BACKEND DX12_X64)
set(FFX_API_DX12 ON)
set(FFX_PLATFORM_NAME x64)
set(FFX_FSR3UPSCALER ON)
set(FFX_ALL OFF)
set(FFX_BUILD_AS_DLL OFF)
set(FFX_AUTO_COMPILE_SHADERS ON)
set(FFX_SC_EXECUTABLE "${_rex_fsr_sdk}/tools/binary_store/FidelityFX_SC.exe")
set(FFX_INCLUDE_PATH "${_rex_fsr_sdk}/include")
# The SDK's libs folder holds the PIX headers, which are not fetched: the backend includes
# pix/pix3.h but loads the PIX functions it uses from WinPixEventRuntime.dll at run time, so
# an empty header stands in for it.
set(FFX_LIB_PATH "${CMAKE_CURRENT_LIST_DIR}/fsr-include")
set(FFX_SHARED_PATH "${_rex_fsr_sdk}/src/shared")
set(FFX_HOST_PATH "${_rex_fsr_sdk}/include/FidelityFX/host")
set(FFX_GPU_PATH "${_rex_fsr_sdk}/include/FidelityFX/gpu")
set(FFX_COMPONENTS_PATH "${_rex_fsr_sdk}/src/components")
set(FFX_HOST_BACKENDS_PATH "${FFX_HOST_PATH}/backends")
set(FFX_SRC_BACKENDS_PATH "${_rex_fsr_sdk}/src/backends")
file(GLOB FFX_SHARED_SOURCES "${FFX_SHARED_PATH}/*.cpp" "${FFX_SHARED_PATH}/*.h")
set(FFX_PUBLIC_SOURCES
    "${FFX_HOST_PATH}/ffx_assert.h"
    "${FFX_HOST_PATH}/ffx_message.h"
    "${FFX_HOST_PATH}/ffx_error.h"
    "${FFX_HOST_PATH}/ffx_fx.h"
    "${FFX_HOST_PATH}/ffx_interface.h"
    "${FFX_HOST_PATH}/ffx_types.h"
    "${FFX_HOST_PATH}/ffx_util.h")

# The backend links pixlib, the SDK's PIX target.
if(NOT TARGET pixlib)
    add_library(pixlib INTERFACE)
endif()

add_subdirectory("${FFX_COMPONENTS_PATH}/fsr3upscaler" "${CMAKE_BINARY_DIR}/fidelityfx/fsr3upscaler")
add_subdirectory("${FFX_SRC_BACKENDS_PATH}/dx12" "${CMAKE_BINARY_DIR}/fidelityfx/backend_dx12")
# compile_shaders_with_depfile (include/FidelityFX/gpu/CMakeCompileShaders.txt) appends the
# permutation headers to its PERMUTATION_OUTPUTS argument, which holds the name of the output
# variable, so the list it returns starts with that name, and ffx_shader_permutations_dx12
# depends on a file of that name in the backend's build folder. Visual Studio builds ignore
# it; Ninja stops on a missing input, so an empty file of that name stands in.
file(TOUCH "${CMAKE_BINARY_DIR}/fidelityfx/backend_dx12/FSR3UPSCALER_PERMUTATION_OUTPUTS")
# The backend's CMake adds the frame generation swap chain (FrameInterpolationSwapchain) with
# every component; it includes the AntiLag 2 header of the SDK's libs folder, and the upscaler
# does not use it.
get_target_property(_rex_fsr_sources ffx_backend_dx12_x64 SOURCES)
list(FILTER _rex_fsr_sources EXCLUDE REGEX "/FrameInterpolationSwapchain/")
set_property(TARGET ffx_backend_dx12_x64 PROPERTY SOURCES ${_rex_fsr_sources})

foreach(_rex_fsr_target IN ITEMS ffx_fsr3upscaler_x64 ffx_backend_dx12_x64)
    # The SDK's CMakeLists.txt adds these to every target.
    target_compile_definitions(${_rex_fsr_target} PRIVATE
        _UNICODE UNICODE _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    target_compile_options(${_rex_fsr_target} PRIVATE -w)
    # C++17: in C++20 and later ffx_util.h calls std::popcount without including <bit>.
    set_target_properties(${_rex_fsr_target} PROPERTIES POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17)
endforeach()

if(TARGET rexgpu-xenos)
    target_compile_definitions(rexgpu-xenos PRIVATE REX_FSR=1)
    target_link_libraries(rexgpu-xenos PRIVATE ffx_fsr3upscaler_x64 ffx_backend_dx12_x64)
endif()

message(STATUS "  FSR 3.1: on, from ${REX_FSR_SDK_DIR}")
