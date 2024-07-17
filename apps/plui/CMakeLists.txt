cmake_minimum_required(VERSION 3.5.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(plui VERSION 0.0.0)

set(CMAKE_BUILD_TYPE "Release")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../libs)

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/../include)

set(CMAKE_C_FLAGS "-std=gnu2x -emit-obj -g -Og -m32")
set(CMAKE_CXX_FLAGS "-std=gnu++2b -emit-obj -g -Og -m32")
set(CMAKE_C_FLAGS "-nostdlib ${CMAKE_C_FLAGS} -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -fno-exceptions")
set(CMAKE_CXX_FLAGS "-nostdlib -nostdlib++ ${CMAKE_CXX_FLAGS} -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -fno-exceptions")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-nolibc -Qn")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "-nolibc -Qn")

include(cmake/gtest)
include(cmake/show-flags) 
include(cmake/mklib)
include(cmake/mkheader)

add_subdirectory(include)
add_subdirectory(src)
