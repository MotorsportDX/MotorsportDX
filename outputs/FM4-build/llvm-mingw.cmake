# CMake toolchain file of the default build (docs/setup.md#toolchain,
# docs/analysis/mingw-toolchain.md): llvm-mingw's clang targeting x86_64-w64-mingw32 with libc++,
# mingw-w64's headers and C runtime (the Universal CRT) and lld, instead of Microsoft's C and C++
# libraries and the Windows SDK. pipeline.py passes FM4_MINGW_ROOT, the unpacked llvm-mingw folder
# of the x86_64-w64-mingw32-clang++ on PATH. The build runs on Windows for Windows, so
# CMAKE_SYSTEM_NAME stays the host's and nothing counts as cross-compiling.
if(NOT FM4_MINGW_ROOT)
    set(FM4_MINGW_ROOT "$ENV{FM4_MINGW_ROOT}")
endif()
set(FM4_MINGW_ROOT "${FM4_MINGW_ROOT}" CACHE PATH "The unpacked llvm-mingw folder")
if(NOT FM4_MINGW_ROOT OR NOT EXISTS "${FM4_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++.exe")
    message(FATAL_ERROR "FM4_MINGW_ROOT must name the unpacked llvm-mingw folder.")
endif()
# Seen by try_compile projects too.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES FM4_MINGW_ROOT)

set(CMAKE_C_COMPILER "${FM4_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang.exe")
set(CMAKE_CXX_COMPILER "${FM4_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++.exe")
set(CMAKE_RC_COMPILER "${FM4_MINGW_ROOT}/bin/x86_64-w64-mingw32-windres.exe")
set(CMAKE_AR "${FM4_MINGW_ROOT}/bin/llvm-ar.exe" CACHE FILEPATH "")
set(CMAKE_RANLIB "${FM4_MINGW_ROOT}/bin/llvm-ranlib.exe" CACHE FILEPATH "")
