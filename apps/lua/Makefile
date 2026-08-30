LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
CFLAGS = -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -march=pentium -Qn -O0 -w
NASM = nasm
READLINE_PATH := ../third_party/pl_readline
READLINE_HEADERS := $(READLINE_PATH)/include/pl_readline.h \
                    $(READLINE_PATH)/include/pl_list.h
READLINE_CFLAGS := -I$(READLINE_PATH)/include -DPL_ENABLE_HISTORY_FILE=0 \
                   -DPL_ENABLE_INTELLISENSE=0
READLINE_OBJS := ../out/lua_plreadln.o ../out/lua_plreadln_history.o \
                 ../out/lua_plreadln_plant_os.o \
                 ../out/lua_plreadln_plain.o ../out/lua_plreadln_util.o
OBJS_PACK1 = ../out/lua.obj $(READLINE_OBJS)
OBJS_PACK2 = ../out/luac.obj
OBJS_PACK3 = ../out/lualib.obj
default : $(OBJS_PACK1) $(OBJS_PACK2) $(OBJS_PACK3)
	$(LINK) $(OBJS_PACK1) $(BASIC_LIB_C) -o ../out/lua.bin
	$(LINK) $(OBJS_PACK2) $(BASIC_LIB_C) -o ../out/luac.bin
	ar rv ../libs/liblua.a $(OBJS_PACK3)
../out/lua.obj : lua.c Makefile $(READLINE_HEADERS)
	$(C) $(READLINE_CFLAGS) -c $< -o $@
../out/lua_%.o : $(READLINE_PATH)/src/%.c Makefile $(READLINE_HEADERS)
	$(C) $(READLINE_CFLAGS) -c $< -o $@
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
