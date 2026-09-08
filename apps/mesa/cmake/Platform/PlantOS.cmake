# Plant OS supplies its POSIX interfaces through libp, with a native syscall ABI.
set(UNIX 1)
set(CMAKE_DL_LIBS "")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
set(CMAKE_EXECUTABLE_SUFFIX ".bin")
set(PLOS_LINK_FLAGS "${PLOS_LINK_FLAGS} -L${PLOS_OUTPUT}/mesa/sysroot/lib")
foreach(language C CXX)
  set(CMAKE_${language}_LINK_EXECUTABLE
    "<CMAKE_LINKER> -nostdlib ${PLOS_LINK_FLAGS} -pie -e main --dynamic-linker /lib/ld.so <OBJECTS> ${PLOS_OUTPUT}/dynamic/libp/dso.o -o <TARGET> <LINK_LIBRARIES> ${PLOS_OUTPUT}/lib/libp.so")
  set(CMAKE_${language}_CREATE_SHARED_LIBRARY
    "<CMAKE_LINKER> -nostdlib ${PLOS_LINK_FLAGS} -shared --no-undefined -soname <TARGET_SONAME> <OBJECTS> ${PLOS_OUTPUT}/dynamic/libp/dso.o -o <TARGET> <LINK_LIBRARIES> ${PLOS_OUTPUT}/lib/libp.so")
endforeach()
