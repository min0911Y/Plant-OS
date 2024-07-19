file(GLOB_RECURSE c_files "*.c")
file(GLOB_RECURSE cpp_files "*.cpp")
add_executable(fattools ${c_files} ${cpp_files})
