include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/obj)

add_binary_asm_no_elf(boot boot/boot.asm)
add_binary_asm_no_elf(boot32 boot/boot32.asm)
add_binary_asm_no_elf(boot_pfs boot/boot_pfs.asm)

add_exe_link_flag("-static -Ttext 0x280000 -e KernelMain")
add_binary_exclude(kernel boot/boot.asm boot/boot32.asm boot/boot_pfs.asm)
target_link_libraries(kernel ${PROJECT_SOURCE_DIR}/apps/lib/libtcc1.a)

add_custom_target(
  make_img
  cd ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND make make_img
  DEPENDS boot boot32 boot_pfs kernel)
add_custom_target(
  make_img_full
  cd ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND make Mimg
  DEPENDS boot boot32 boot_pfs kernel)
