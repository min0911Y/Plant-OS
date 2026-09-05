.DEFAULT_GOAL := default
BUILD := out/x86_64
LIBS := libs/x86_64
CC := gcc
CFLAGS := -m64 -mcmodel=large -mno-red-zone -mno-mmx -msse2 -mfpmath=sse -mlong-double-64 \
	-DPLANT_ARCH_X86_64 -std=gnu17 -Iinclude -Ilibutf/include \
	-Ithird_party/pl_readline/include -DPL_ENABLE_HISTORY_FILE=0 \
	-nostdinc -isystem $(shell $(CC) -print-file-name=include) -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -fno-asynchronous-unwind-tables -MMD -MP \
	-ffunction-sections -fdata-sections -O2 -Wall -Wextra \
	-Wno-unused-parameter -Wno-sign-compare -Werror=implicit-function-declaration \
	-Werror=pointer-to-int-cast -Werror=int-to-pointer-cast
LDFLAGS := -m elf_x86_64 -static --gc-sections -z max-page-size=4096 -T libp/arch/x86_64/app.ld
LIBP_SOURCES := $(filter-out libp/tinyalloc.c,$(wildcard libp/*.c)) libp/arch/x86_64/math.c
LIBP_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(LIBP_SOURCES)) $(BUILD)/libp/arch/x86_64/syscall.obj
MST_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard miniset/*.c))
UTF_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard libutf/utf/*.c libutf/runestr/*.c libutf/runetype/*.c))
PROGRAMS := init psh gui guitest rpctest nettest ping curl ps uname copy copydir clock calc hexview archtest simdtest exc_test
READLINE_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(filter-out third_party/pl_readline/src/plreadln_plain.c,$(wildcard third_party/pl_readline/src/*.c)))
APP_LIBS := $(LIBS)/libp.a $(LIBS)/libmst.a $(LIBS)/libutf.a

.PHONY: default all
default all: $(addprefix $(BUILD)/,$(addsuffix .bin,$(PROGRAMS))) $(BUILD)/dktest.bin $(BUILD)/lua.bin $(BUILD)/luac.bin $(BUILD)/cpptest.bin $(LIBS)/libcpps.a

$(BUILD)/%.o: %.c build-x86_64.mk native-apps.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: %.cpp build-x86_64.mk native-apps.mk
	@mkdir -p $(dir $@)
	g++ $(filter-out -std=gnu17 -Werror=implicit-function-declaration -Werror=pointer-to-int-cast,$(CFLAGS)) -std=gnu++17 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -c $< -o $@
$(BUILD)/%.obj: %.asm
	@mkdir -p $(dir $@)
	nasm -f elf64 $< -o $@
$(LIBS)/libp.a: $(LIBP_OBJECTS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^
$(LIBS)/libmst.a: $(MST_OBJECTS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^
$(LIBS)/libutf.a: $(UTF_OBJECTS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

objects = $(addprefix $(BUILD)/,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(1))))

define application
$(BUILD)/$(1).bin: $(call objects,$(if $(3),$(3),$(wildcard $(1)/*.c))) $(2) $(APP_LIBS) libp/arch/x86_64/app.ld
	ld $$(LDFLAGS) -o $$@ $$(filter %.o %.obj,$$^) --start-group $$(filter %.a,$$^) --end-group
endef
$(foreach program,$(PROGRAMS),$(eval $(call application,$(program),$(if $(filter psh,$(program)),$(READLINE_OBJECTS)))))
$(BUILD)/simdtest.bin: $(BUILD)/simdtest/arch/x86_64/probe.obj
$(BUILD)/dktest.bin: $(patsubst %.c,$(BUILD)/%.o,$(wildcard diskio_test/*.c)) $(APP_LIBS) libp/arch/x86_64/app.ld
	ld $(LDFLAGS) -o $@ $(filter %.o,$^) --start-group $(APP_LIBS) --end-group

-include $(shell test ! -d $(BUILD) || find $(BUILD) -name '*.d')

LUA_READLINE := plreadln plreadln_history plreadln_plant_os plreadln_plain plreadln_util
LUA_READLINE_OBJECTS := $(addprefix $(BUILD)/lua-readline/,$(addsuffix .o,$(LUA_READLINE)))
$(BUILD)/lua-readline/%.o: third_party/pl_readline/src/%.c build-x86_64.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DPL_ENABLE_INTELLISENSE=0 -c $< -o $@
$(BUILD)/lua/lua.o: CFLAGS += -DPL_ENABLE_INTELLISENSE=0
$(BUILD)/lua.bin: $(BUILD)/lua/lua.o $(LUA_READLINE_OBJECTS) $(APP_LIBS) libp/arch/x86_64/app.ld
	ld $(LDFLAGS) -o $@ $(filter %.o,$^) --start-group $(APP_LIBS) --end-group
$(BUILD)/luac.bin: $(BUILD)/lua/luac.o $(APP_LIBS) libp/arch/x86_64/app.ld
	ld $(LDFLAGS) -o $@ $(filter %.o,$^) --start-group $(APP_LIBS) --end-group

$(LIBS)/libcpps.a: $(filter-out $(BUILD)/libp/entry.o,$(LIBP_OBJECTS)) $(BUILD)/libp/cppstart.o
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(BUILD)/cpptest.bin: $(BUILD)/cpptest/cpptest.o $(LIBS)/libcpps.a libp/arch/x86_64/app.ld
	ld $(LDFLAGS) -o $@ $(filter %.o %.a,$^)

include native-apps.mk
