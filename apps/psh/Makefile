LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
READLINE_PATH := ../third_party/pl_readline
READLINE_HEADERS := $(READLINE_PATH)/include/pl_readline.h \
                    $(READLINE_PATH)/include/pl_list.h
PERF_HEADER := ../include/perf.h
READLINE_CFLAGS := -I$(READLINE_PATH)/include -DPL_ENABLE_HISTORY_FILE=0
READLINE_OBJS := ../out/psh_plreadln.o ../out/psh_plreadln_color.o \
                 ../out/psh_plreadln_history.o \
                 ../out/psh_plreadln_intellisense.o \
                 ../out/psh_plreadln_plant_os.o \
                 ../out/psh_plreadln_util.o ../out/psh_plreadln_wordmk.o
OBJS_PACK = ../out/psh.obj $(READLINE_OBJS)
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(LIBS_PATH)/libmst.a $(BASIC_LIB_C) -o ../out/psh.bin
../out/psh.obj : psh.c Makefile $(READLINE_HEADERS) $(PERF_HEADER)
	$(C) $(READLINE_CFLAGS) -c $< -o $@
../out/psh_%.o : $(READLINE_PATH)/src/%.c Makefile $(READLINE_HEADERS)
	$(C) $(READLINE_CFLAGS) -c $< -o $@
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
