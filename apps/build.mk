.DEFAULT_GOAL := all
include dynamic.mk

BUILD := $(DYN_OUT)
LIBS := libs$(if $(filter x86_64,$(ARCH)),/x86_64)
CC := gcc
CFLAGS := $(DYN_CFLAGS) -Ilibutf/include -Ithird_party/pl_readline/include \
  -DPL_ENABLE_HISTORY_FILE=0
ifeq ($(ARCH),i386)
CFLAGS += -finput-charset=UTF-8 -fexec-charset=GB2312
endif
LDFLAGS := $(DYN_LDFLAGS) -pie --gc-sections -e Main --dynamic-linker /lib/ld.so -rpath-link $(DYN_LIB)
APP_RUNTIME := $(DYN_BUILD)/libp/entry.o $(DYN_DSO) $(DYN_LIB)/libp.so
APP_LIBS := $(LIBS)/libmst.a $(LIBS)/libutf.a
MST_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard miniset/*.c))
UTF_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard libutf/utf/*.c libutf/runestr/*.c libutf/runetype/*.c))
PROGRAMS := init psh gui guitest rpctest nettest ping curl ps uname copy copydir clock calc hexview exc_test
ifeq ($(ARCH),x86_64)
PROGRAMS += archtest simdtest
endif
READLINE_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(filter-out third_party/pl_readline/src/plreadln_plain.c,$(wildcard third_party/pl_readline/src/*.c)))

.PHONY: default all libp ldso
default all: dynamic
libp: $(DYN_LIB)/libp.so $(DYN_LIB)/libcpp.so
ldso: $(DYN_LIB)/ld.so

$(BUILD)/%.o: %.c build.mk native-apps.mk dynamic.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: %.cpp build.mk native-apps.mk dynamic.mk $(CXX_CONFIG)
	@mkdir -p $(dir $@)
	g++ -nostdinc++ -I$(CXX_HEADERS) $(filter-out -std=gnu17 -Werror=implicit-function-declaration,$(CFLAGS)) \
	  -std=gnu++17 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -c $< -o $@
$(BUILD)/%.obj: %.asm
	@mkdir -p $(dir $@)
	nasm -I$(dir $<) -f $(DYN_FORMAT) $< -o $@
$(LIBS)/libmst.a: $(MST_OBJECTS)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $^
$(LIBS)/libutf.a: $(UTF_OBJECTS)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $^
.PHONY: libmst libutf
libmst: $(LIBS)/libmst.a
libutf: $(LIBS)/libutf.a

objects = $(addprefix $(BUILD)/,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(1))))

# Every application enters through the same C startup and imports libp.so.
# Application-private archives are PIC, while C++ support is a separate DSO.
define application
APP_TARGETS += $(BUILD)/$(1).bin
APP_OBJECTS += $(call objects,$(if $(3),$(3),$(wildcard $(1)/*.c))) $(filter %.o,$(2))
.PHONY: $(1)
$(1): $(BUILD)/$(1).bin
$(BUILD)/$(1).bin: $(call objects,$(if $(3),$(3),$(wildcard $(1)/*.c))) $(2) $(APP_LIBS) $(APP_RUNTIME) $(if $(filter %.cpp,$(3)),$(DYN_LIB)/libcpp.so) $(if $(filter $(LIBS)/sdl3.a,$(2)),$(SDL_RUNTIME))
	ld $$(LDFLAGS) -o $$@ $$(filter %.o %.obj,$$^) --start-group $$(filter %.a %.so,$$^) --end-group
endef
$(foreach program,$(PROGRAMS),$(eval $(call application,$(program),$(if $(filter psh,$(program)),$(READLINE_OBJECTS)))))
ifeq ($(ARCH),x86_64)
$(BUILD)/simdtest.bin: $(BUILD)/simdtest/arch/x86_64/probe.obj
$(eval $(call application,cpptest,,cpptest/cpptest.cpp))
endif
$(eval $(call application,dktest,,diskio_test/diskio_test.c))

LUA_READLINE := plreadln plreadln_history plreadln_plant_os plreadln_plain plreadln_util
LUA_READLINE_OBJECTS := $(addprefix $(BUILD)/lua-readline/,$(addsuffix .o,$(LUA_READLINE)))
$(BUILD)/lua-readline/%.o: third_party/pl_readline/src/%.c build.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DPL_ENABLE_INTELLISENSE=0 -c $< -o $@
$(BUILD)/lua/lua.o: CFLAGS += -DPL_ENABLE_INTELLISENSE=0
$(eval $(call application,lua,$(LUA_READLINE_OBJECTS),lua/lua.c))
$(eval $(call application,luac,,lua/luac.c))

include native-apps.mk

ifeq ($(ARCH),i386)
$(eval $(call application,tcc,,tcc/tcc.c))
$(eval $(call application,tccinst))
$(eval $(call application,setup1))
$(eval $(call application,fputest,$(BUILD)/fputest/arch/i386/fputest.obj))

# The guest compiler SDK and the kernel's compiler helpers are archives, not
# shipped executables. They remain explicit products of the common source graph.
SDK_CFLAGS := $(filter-out -fPIC,$(DYN_CFLAGS)) -fno-pic -fno-pie
SDK_LIBP := $(patsubst %.c,$(BUILD)/sdk/%.o,$(filter-out libp/abi.c,$(DYN_SOURCES)) libp/entry.c) \
  $(DYN_BUILD)/libp/arch/$(ARCH)/syscall.obj $(BUILD)/sdk/libp/dso.o
$(BUILD)/sdk/%.o: %.c build.mk dynamic.mk
	@mkdir -p $(dir $@)
	$(CC) $(SDK_CFLAGS) -c $< -o $@
$(BUILD)/sdk/%.o: %.cpp build.mk dynamic.mk
	@mkdir -p $(dir $@)
	g++ $(filter-out -std=gnu17 -Werror=implicit-function-declaration,$(SDK_CFLAGS)) \
	  -std=gnu++17 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -c $< -o $@
$(LIBS)/libp.a: $(SDK_LIBP)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $^
$(LIBS)/libcpps.a: $(SDK_LIBP) $(CXX_CONFIG)
	@mkdir -p $(dir $@)
	cp $(CXX_ARCHIVE) $@.tmp
	ar rcs $@.tmp $(SDK_LIBP)
	mv $@.tmp $@
$(LIBS)/libabi.a: $(BUILD)/sdk/libp/abi.o
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $^
$(BUILD)/sdk/libtcc1.o: libtcc1/libtcc1.c dynamic.mk build.mk
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -fPIC,$(DYN_CFLAGS)) -fno-pic -fno-pie -c $< -o $@
$(LIBS)/libtcc1.a: $(BUILD)/sdk/libtcc1.o
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $^
$(BUILD)/crti.obj: tcc/tcc/crti.c build.mk
	$(CC) $(SDK_CFLAGS) -c $< -o $@
.PHONY: sdk libtcc1
SDK_LIBRARIES := $(addprefix $(LIBS)/,libp.a libcpps.a libabi.a libtcc1.a)
sdk: $(SDK_LIBRARIES) $(BUILD)/crti.obj $(BUILD)/sdk-libraries.list
$(BUILD)/sdk-libraries.list: $(SDK_LIBRARIES) build.mk
	printf '%s\n' $(notdir $(SDK_LIBRARIES)) > $@
libtcc1: $(LIBS)/libtcc1.a
default all: sdk
UNSUPPORTED_PROGRAMS := archtest simdtest cpptest llvmtest lvptest vkcube lavapipe llvmpipe glxgears glfw glfwtest
else
UNSUPPORTED_PROGRAMS := tcc tccinst setup1 fputest
endif
.PHONY: $(UNSUPPORTED_PROGRAMS)
$(UNSUPPORTED_PROGRAMS) $(addprefix $(BUILD)/,$(addsuffix .bin,$(UNSUPPORTED_PROGRAMS))):
	@echo "$(@F) has no $(ARCH) backend" >&2
	@exit 1

default all: $(APP_TARGETS) $(BUILD)/applications.list

# Packaging and verification consume the actual build graph, not stale .bin
# files that happen to remain in an output directory.
$(BUILD)/applications.list: $(APP_TARGETS) $(DYN_TARGETS) build.mk native-apps.mk dynamic.mk $(if $(filter x86_64,$(ARCH)),mesa/build.mk glfw/build.mk)
	printf '%s\n' $(sort $(notdir $(APP_TARGETS) $(DYN_TARGETS))) > $@
.PHONY: list-apps
list-apps:
	@printf '%s\n' $(sort $(notdir $(APP_TARGETS) $(DYN_TARGETS)))

-include $(patsubst %.o,%.d,$(filter %.o,$(APP_OBJECTS) $(LIBRARY_OBJECTS) \
  $(MST_OBJECTS) $(UTF_OBJECTS) $(LUA_READLINE_OBJECTS) $(SDK_LIBP))) \
  $(BUILD)/sdk/libtcc1.d $(BUILD)/crti.d
