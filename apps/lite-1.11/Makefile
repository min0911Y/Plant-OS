ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/lite.bin
include ../native.mk
else ifeq ($(ARCH),i386)
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
include sources.mk
CFLAGS += -O2 -MMD -MP -I./src -I../sdl2/include
OBJECTS := $(patsubst ./%.c,../out/lite/%.o,$(LITE_SOURCES))
LIBRARIES := $(LIBS_PATH)/sdl2.a $(LIBS_PATH)/libgui.a $(BASIC_LIB_C)
.DEFAULT_GOAL := default
default: ../out/lite.bin
../out/lite.bin: $(OBJECTS) $(LIBRARIES)
	$(LINK) $(OBJECTS) --start-group $(LIBRARIES) --end-group -o $@
../out/lite/%.o: %.c Makefile sources.mk
	@mkdir -p $(dir $@)
	$(C) -c $< -o $@
-include $(OBJECTS:.o=.d)
else
$(error unsupported ARCH)
endif
