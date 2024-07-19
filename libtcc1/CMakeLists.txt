set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/apps/lib)

file(GLOB_RECURSE c_files "*.c")
file(GLOB_RECURSE cpp_files "*.cpp")
add_library(tcc1 STATIC ${c_files} ${cpp_files})
