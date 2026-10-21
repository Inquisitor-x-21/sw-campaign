# Cross-compile from Linux with MinGW-w64, for compile checks and the Windows
# bot/test executables:
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=tools/ci/mingw-toolchain.cmake -DRDR2COOP_PLUGIN_COMPILE_CHECK=ON
# The game plugin itself should be built with Visual Studio against the real SDK.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
