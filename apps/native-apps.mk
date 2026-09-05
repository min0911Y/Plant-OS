# Native programs and library dependencies. No i386 objects enter these targets.
SDL_ROOT := sdl2
include sdl2/sources.mk

define library
$(LIBS)/$(1).a: $(call objects,$(2)) build-x86_64.mk native-apps.mk
	@mkdir -p $$(dir $$@)
	rm -f $$@
	ar rcs $$@ $$(filter %.o,$$^)
endef

$(eval $(call library,sdl2,$(SDL_SOURCES)))
SDL_CFLAGS := -Isdl2/include -Isdl2/SDL2 -Isdl2/src/video/yuv2rgb
$(BUILD)/sdl2/%.o: CFLAGS += $(SDL_CFLAGS)

LITE_ROOT := lite-1.11
include lite-1.11/sources.mk
$(BUILD)/lite-1.11/%.o: CFLAGS += $(SDL_CFLAGS) -Ilite-1.11/src
$(eval $(call application,lite,$(LIBS)/sdl2.a,$(LITE_SOURCES)))

SDL_PROGRAMS := nk kcube invader minewep bitz
$(foreach app,$(SDL_PROGRAMS),$(eval $(call application,$(app),$(LIBS)/sdl2.a,$(app)/$(app).c)))
$(foreach app,$(SDL_PROGRAMS),$(eval $(BUILD)/$(app)/%.o: CFLAGS += $(SDL_CFLAGS) -ISDL2_image -ISDL2_ttf -I$(app)))

SIMPLE_PROGRAMS := bainian aigobang randnum cal pfn sort pwsh snake cgobang edit \
                   basic Maze image bim2hrb lox doomcpy mmake ttf chat paint netgobang usbtest
$(foreach app,$(SIMPLE_PROGRAMS),$(eval $(call application,$(app))))
$(eval $(call application,bf,,brainfuck/bf.c))
$(eval $(call application,c4,,c4/c4.c))
$(eval $(call application,cc,,cc/cc.c))
$(eval $(call application,editor,$(LIBS)/libcpps.a,editor/editor.cpp))

ZLIB_SOURCES := $(addprefix zlib/,adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c)
$(eval $(call library,libz,$(ZLIB_SOURCES)))
$(eval $(call library,libpng,$(wildcard libpng/*.c)))
$(eval $(call library,libjpg,$(wildcard jpeg/*.c)))
FT_MODULES := autofit base bdf cache cff cid gzip lzw pcf pfr psaux pshinter psnames raster sfnt smooth truetype type1 type42 winfonts
FT_SOURCES := $(foreach m,$(filter-out base,$(FT_MODULES)),$(wildcard freetype/src/$(m)/$(m).c)) \
              $(addprefix freetype/src/base/,ftbase.c ftinit.c ftsystem.c ftdebug.c ftbitmap.c ftglyph.c ftstroke.c ftbbox.c ftmm.c ftbdf.c ftcid.c ftfstype.c ftgasp.c ftgxval.c ftotval.c ftpatent.c ftpfr.c fttype1.c ftwinfnt.c)
$(eval $(call library,libft,$(FT_SOURCES) freetype/src/cid/type1cid.c freetype/src/winfonts/winfnt.c))
$(BUILD)/freetype/%.o: CFLAGS += -Ifreetype/include
$(eval $(call library,sdl2_ttf,$(wildcard SDL2_ttf/*.c)))
$(BUILD)/SDL2_ttf/%.o: CFLAGS += $(SDL_CFLAGS) -Ifreetype/include
$(eval $(call library,sdl2_image,$(wildcard SDL2_image/*.c)))
$(BUILD)/SDL2_image/%.o: CFLAGS += $(SDL_CFLAGS) -Ijpeg -Ilibpng \
    -DSDL_IMAGE_USE_COMMON_BACKEND -DLOAD_BMP -DLOAD_GIF -DLOAD_LBM -DLOAD_PCX \
    -DLOAD_PNM -DLOAD_SVG -DLOAD_TGA -DLOAD_XCF -DLOAD_XPM -DLOAD_XV -DLOAD_JPG -DLOAD_PNG
$(BUILD)/minewep.bin $(BUILD)/bitz.bin: $(addprefix $(LIBS)/,sdl2_image.a libpng.a libjpg.a libz.a)
$(BUILD)/bitz.bin: $(LIBS)/sdl2_ttf.a $(LIBS)/libft.a

MINIZIP_COMMON := $(addprefix minizip/,ioapi.c mztools.c unzip.c zip.c)
$(BUILD)/minizip/%.o: CFLAGS += -DMINIZIP_FOPEN_NO_64
$(foreach app,minizip miniunz,$(eval $(call application,$(app),$(LIBS)/libz.a,$(MINIZIP_COMMON) minizip/$(app).c)))
$(eval $(call application,duktape,,duktape/duk_cmdline_lowmem.c duktape/duk_cmdline.c duktape/duk_console.c duktape/duktape.c))
$(eval $(call application,my_basic,,my_basic-master/core/my_basic.c my_basic-master/shell/main.c))
$(BUILD)/my_basic-master/%.o: CFLAGS += -DMB_FREESTANDING

DOOM_ROOT := doomgeneric
include doomgeneric/sources.mk
$(BUILD)/doomgeneric/%.o: CFLAGS += $(SDL_CFLAGS)
$(eval $(call application,doom,$(LIBS)/sdl2.a,$(DOOM_SOURCES)))

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
$(BUILD)/sdltest/%.o: CFLAGS += $(SDL_CFLAGS) -ISDL2_ttf
$(eval $(call application,sdltest,$(addprefix $(LIBS)/,sdl2.a sdl2_ttf.a libft.a libz.a),sdltest/sdltest.c))
default all: $(BUILD)/timetest.bin $(BUILD)/sdltest.bin

# Reject unported tools explicitly; keep them in the i386 build.
$(addprefix $(BUILD)/,tcc.bin tccinst.bin setup1.bin fputest.bin):
	@echo "$(@F) is i386-only: its compiler/installer/x87 backend is not ported" >&2
	@exit 1
