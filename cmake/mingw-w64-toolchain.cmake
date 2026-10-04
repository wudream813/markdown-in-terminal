# CMake toolchain file for MinGW-w64 cross compilation from Linux/macOS.
#
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
#   cmake --build build-win -j
#
# Use -DMDT_MINGW_PREFIX=i686-w64-mingw32 for a 32 bit build.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MDT_MINGW_PREFIX "x86_64-w64-mingw32" CACHE STRING "MinGW-w64 tool prefix")
set(CMAKE_C_COMPILER   ${MDT_MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${MDT_MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${MDT_MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${MDT_MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# fully static runtime: the .exe needs no libgcc/libstdc++/winpthread DLLs
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
