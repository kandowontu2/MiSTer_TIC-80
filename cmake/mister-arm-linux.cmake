# GNU Arm SDK used by the pinned MiSTer Main build (glibc 2.31).
# Keep libc dynamic so ALSA and its plugins share the application's libc.
# GCC support libraries are static to avoid adding a board C++ version floor.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(TM_MISTER_SDK "$ENV{TM_MISTER_SDK}" CACHE PATH "GNU Arm 10.2-2020.11 SDK root")
if(NOT TM_MISTER_SDK)
    get_filename_component(TM_MISTER_SDK "${CMAKE_CURRENT_LIST_DIR}/../build/toolchains/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf" REALPATH)
endif()
# Upstream MRuby passes the compiler name through a shell without quoting it.
# Resolve the Windows build junction so that its physical SDK path is used.
get_filename_component(TM_MISTER_SDK "${TM_MISTER_SDK}" REALPATH)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TM_MISTER_SDK)
set(prefix "${TM_MISTER_SDK}/bin/arm-none-linux-gnueabihf")
if(NOT EXISTS "${prefix}-gcc" OR NOT EXISTS "${prefix}-g++")
    message(FATAL_ERROR "Set TM_MISTER_SDK to the GNU Arm 10.2-2020.11 SDK root")
endif()
execute_process(COMMAND "${prefix}-gcc" -dumpfullversion
    OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT version STREQUAL "10.2.1")
    message(FATAL_ERROR "MiSTer runtime profile requires GNU Arm GCC 10.2.1")
endif()
get_filename_component(libc "${TM_MISTER_SDK}/arm-none-linux-gnueabihf/libc/lib/libc.so.6" REALPATH)
get_filename_component(libc_name "${libc}" NAME)
if(NOT libc_name STREQUAL "libc-2.31.so" OR NOT EXISTS "${libc}")
    message(FATAL_ERROR "MiSTer runtime profile requires the SDK's glibc 2.31")
endif()
set(CMAKE_C_COMPILER "${prefix}-gcc")
set(CMAKE_CXX_COMPILER "${prefix}-g++")
set(CMAKE_SYSROOT "${TM_MISTER_SDK}/arm-none-linux-gnueabihf/libc")
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=neon -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon -mfloat-abi=hard")
# Use the SDK's wide stat/dirent interfaces for filesystem metadata. Narrow
# readdir can reject directory offsets with EOVERFLOW even for small files.
# A directory definition also takes effect when reconfiguring an existing build.
add_compile_definitions(_FILE_OFFSET_BITS=64)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
find_program(TM_SDK_QEMU_ARM qemu-arm)
if(TM_SDK_QEMU_ARM)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${TM_SDK_QEMU_ARM};-L;${CMAKE_SYSROOT}")
endif()
