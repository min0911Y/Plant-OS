if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  file(GLOB_RECURSE c_files "*.c")
  file(GLOB_RECURSE cpp_files "*.cpp")
  add_executable(netgobang ${c_files} ${cpp_files})
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")

endif()
