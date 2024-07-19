cmake_minimum_required(VERSION 3.5.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------------------------------------
# 编译器配置

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_LINKER "ld")
set(CMAKE_BUILD_TYPE "Release")

enable_language(ASM_NASM)
set(CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS asm ASM)

# ----------------------------------------------------------------------------------------------------
# 项目配置

project(plant-os VERSION 0.0.0)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

include_directories(${PROJECT_SOURCE_DIR}/include)

include(cmake/util)
include(cmake/add_binary)
include(cmake/mkheader)

# ----------------------------------------------------------------------------------------------------
# 以下设定 本机 编译配置

set(CMAKE_C_FLAGS "-std=gnu2x")
set(CMAKE_CXX_FLAGS "-std=gnu++2b")

set(CMAKE_C_FLAGS_RELEASE "-O1")
set(CMAKE_CXX_FLAGS_RELEASE "-O1")
set(CMAKE_C_FLAGS_DEBUG "-O1")
set(CMAKE_CXX_FLAGS_DEBUG "-O1")

add_compile_flag("-w")

message("本机 环境")
include(cmake/show-flags)

add_subdirectory(fattools)
add_subdirectory(netgobang)

# ----------------------------------------------------------------------------------------------------
# 以下设定 PLOS 编译配置

set(CMAKE_C_FLAGS "-std=gnu2x")
set(CMAKE_CXX_FLAGS "-std=gnu++2b")

set(CMAKE_C_FLAGS_RELEASE "-O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O0")
set(CMAKE_C_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0")

set(CMAKE_EXE_LINKER_FLAGS "-nolibc -Qn")
set(CMAKE_SHARED_LINKER_FLAGS "-nolibc -Qn")

add_compile_flag("-w")

add_compile_flag("-m32 -mfpmath=387 -fno-builtin -mno-mmx -mno-sse -nostdlib -nostdinc")

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  add_compile_flag("-emit-obj")
endif()

add_compile_flag("-finput-charset=UTF-8 -fexec-charset=UTF-8")

add_compile_flag("-ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -fno-exceptions")
add_compile_flag("-Wno-packed-bitfield-compat")

message("PLOS 环境")
include(cmake/show-flags)

add_subdirectory(libtcc1)
add_subdirectory(apps)
add_subdirectory(kernel)
add_subdirectory(loader)
