# Application sources and private library dependencies, shared by both architectures.
SDL_ROOT := sdl3
include sdl3/sources.mk

OS_TERMINAL_DIR ?= $(HOME)/os-terminal
OS_TERMINAL_LIB := $(OS_TERMINAL_DIR)/libos_terminal_$(if $(filter i386,$(ARCH)),x86,x64).a
$(BUILD)/term/%.o: CFLAGS += -I$(OS_TERMINAL_DIR)
$(BUILD)/term/arch/i386/float.o: CFLAGS += -mno-fp-ret-in-387 -fvisibility=hidden
$(eval $(call application,term,$(OS_TERMINAL_LIB),term/term.c $(if $(filter i386,$(ARCH)),term/arch/i386/float.c)))
$(BUILD)/term/term.o: $(BUILD)/term/.config $(OS_TERMINAL_DIR)/os_terminal.h
.PHONY: FORCE_TERM_CONFIG
$(BUILD)/term/.config: FORCE_TERM_CONFIG
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(abspath $(OS_TERMINAL_DIR))' > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

define library
.PHONY: $(1)
$(1): $(LIBS)/$(1).a
$(LIBS)/$(1).a: $(call objects,$(2)) build.mk native-apps.mk
	@mkdir -p $$(dir $$@)
	rm -f $$@
	ar rcs $$@ $$(filter %.o,$$^)
endef

$(eval $(call library,sdl3,$(SDL_SOURCES)))
SDL_CFLAGS := -Isdl3/include -ISDL3_image/include -ISDL3_ttf/include
$(BUILD)/sdl3/%.o: CFLAGS += $(SDL_CFLAGS) -Isdl3/config -Isdl3/src
$(call objects,$(SDL_SOURCES)): sdl3/sources.mk

LITE_ROOT := lite-1.11
include lite-1.11/sources.mk
$(BUILD)/lite-1.11/%.o: CFLAGS += $(SDL_CFLAGS) -Ilite-1.11/src
$(eval $(call application,lite,$(LIBS)/sdl3.a,$(LITE_SOURCES)))

SDL_PROGRAMS := nk kcube invader minewep bitz
$(foreach app,$(SDL_PROGRAMS),$(eval $(call application,$(app),$(LIBS)/sdl3.a,$(app)/$(app).c)))
$(foreach app,$(SDL_PROGRAMS),$(eval $(BUILD)/$(app)/%.o: CFLAGS += $(SDL_CFLAGS) -I$(app)))

SIMPLE_PROGRAMS := bainian aigobang randnum cal pfn sort pwsh snake cgobang edit \
                   basic Maze image bim2hrb lox doomcpy mmake ttf chat paint netgobang usbtest
$(foreach app,$(SIMPLE_PROGRAMS),$(eval $(call application,$(app))))
$(eval $(call application,bf,,brainfuck/bf.c))
$(eval $(call application,c4,,c4/c4.c))
$(eval $(call application,cc,,cc/cc.c))
EDITOR_SOURCES := editor/main.c editor/platform.c third_party/pl_editor/pleditor.c third_party/pl_editor/syntax.c
$(call objects,$(EDITOR_SOURCES)): CFLAGS += -Ithird_party/pl_editor
$(eval $(call application,editor,,$(EDITOR_SOURCES)))

ZLIB_SOURCES := $(addprefix zlib/,adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c)
$(eval $(call library,libz,$(ZLIB_SOURCES)))
$(eval $(call library,libpng,$(wildcard libpng/*.c)))
$(eval $(call library,libjpg,$(wildcard jpeg/*.c)))
FT_MODULES := autofit base bdf cache cff cid gzip lzw pcf pfr psaux pshinter psnames raster sfnt smooth truetype type1 type42 winfonts
FT_SOURCES := $(foreach m,$(filter-out base,$(FT_MODULES)),$(wildcard freetype/src/$(m)/$(m).c)) \
              $(addprefix freetype/src/base/,ftbase.c ftinit.c ftsystem.c ftdebug.c ftbitmap.c ftglyph.c ftstroke.c ftbbox.c ftmm.c ftbdf.c ftcid.c ftfstype.c ftgasp.c ftgxval.c ftotval.c ftpatent.c ftpfr.c fttype1.c ftwinfnt.c)
$(eval $(call library,libft,$(FT_SOURCES) freetype/src/cid/type1cid.c freetype/src/winfonts/winfnt.c))
$(BUILD)/freetype/%.o: CFLAGS += -Ifreetype/include
$(eval $(call library,sdl3_ttf,$(wildcard SDL3_ttf/src/*.c)))
$(BUILD)/SDL3_ttf/%.o: CFLAGS += $(SDL_CFLAGS) -Ifreetype/include
$(eval $(call library,sdl3_image,$(wildcard SDL3_image/src/*.c)))
$(BUILD)/SDL3_image/%.o: CFLAGS += $(SDL_CFLAGS) -Ijpeg -Ilibpng \
    -DSDL_IMAGE_USE_COMMON_BACKEND -DLOAD_BMP -DLOAD_GIF -DLOAD_LBM -DLOAD_PCX \
    -DLOAD_PNM -DLOAD_SVG -DLOAD_TGA -DLOAD_XCF -DLOAD_XPM -DLOAD_XV -DLOAD_JPG -DLOAD_PNG
$(BUILD)/minewep.bin: $(addprefix $(LIBS)/,sdl3_image.a libpng.a libjpg.a libz.a)
$(BUILD)/bitz.bin: $(LIBS)/sdl3_ttf.a $(LIBS)/libft.a

MINIZIP_COMMON := $(addprefix minizip/,ioapi.c mztools.c unzip.c zip.c)
$(BUILD)/minizip/%.o: CFLAGS += -DMINIZIP_FOPEN_NO_64
$(foreach app,minizip miniunz,$(eval $(call application,$(app),$(LIBS)/libz.a,$(MINIZIP_COMMON) minizip/$(app).c)))
$(eval $(call application,duktape,,duktape/duk_cmdline_lowmem.c duktape/duk_cmdline.c duktape/duk_console.c duktape/duktape.c))
$(eval $(call application,my_basic,,my_basic-master/core/my_basic.c my_basic-master/shell/main.c))
$(BUILD)/my_basic-master/%.o: CFLAGS += -DMB_FREESTANDING

DOOM_ROOT := doomgeneric
include doomgeneric/sources.mk
$(BUILD)/doomgeneric/%.o: CFLAGS += $(SDL_CFLAGS)
$(eval $(call application,doom,$(LIBS)/sdl3.a,$(DOOM_SOURCES)))

default all: $(addprefix $(BUILD)/,$(addsuffix .bin,$(SDL_PROGRAMS) $(SIMPLE_PROGRAMS) \
             lite bf c4 cc editor minizip miniunz duktape my_basic doom))

NASM_ROOT := nasm-master
include nasm-master/sources.mk
$(eval $(call library,libnasm,$(NASM_SOURCES) nasm-master/asm/warnings.c))
$(BUILD)/nasm-master/%.o: CFLAGS += -DHAVE_CONFIG_H -Inasm-master -Inasm-master/include -Inasm-master/x86 -Inasm-master/asm -Inasm-master/disasm -Inasm-master/output -fwrapv
$(eval $(call application,nasm,$(LIBS)/libnasm.a,nasm-master/asm/nasm.c))
$(eval $(call application,ndisasm,$(LIBS)/libnasm.a,nasm-master/disasm/ndisasm.c))
default all: $(BUILD)/nasm.bin $(BUILD)/ndisasm.bin

$(eval $(call application,timetest,,timetest/timetest.c))
$(BUILD)/sdltest/%.o: CFLAGS += $(SDL_CFLAGS)
$(eval $(call application,sdltest,$(addprefix $(LIBS)/,sdl3.a sdl3_ttf.a sdl3_image.a libft.a libpng.a libjpg.a libz.a),sdltest/sdltest.c))
default all: $(BUILD)/timetest.bin $(BUILD)/sdltest.bin
