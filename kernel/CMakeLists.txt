include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/obj)

add_binary_asm_no_elf(boot boot/boot.asm)
add_binary_asm_no_elf(boot32 boot/boot32.asm)
add_binary_asm_no_elf(boot_pfs boot/boot_pfs.asm)

add_exe_link_flag("-static -Wl,-Ttext -Wl,0x280000 -e KernelMain")
add_binary_exclude(kernel boot/boot.asm boot/boot32.asm boot/boot_pfs.asm)
target_link_libraries(kernel tcc1)

add_subdirectory(include)

add_custom_target(
  make_img ALL
  COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && make make_img
  DEPENDS dosldr boot boot32 boot_pfs kernel)
add_custom_target(
  make_img_full
  COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && make Mimg
  DEPENDS dosldr boot boot32 boot_pfs kernel)
add_custom_target(
  img_run
  COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && make img_run
  DEPENDS make_img_full)
add_custom_target(
  run
  COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && make run
  DEPENDS make_img)
