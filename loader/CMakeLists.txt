include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/out)

add_exe_link_flag("-static -Wl,-Ttext -Wl,0x100000 -e loader_main")

add_binary(dosldr)

add_subdirectory(include)
