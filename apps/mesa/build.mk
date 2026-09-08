LLVM_BUILD := $(DYN_OUT)/mesa/llvm
LLVM_ARCHIVE := $(LLVM_BUILD)/lib/libLLVMPlant.a
LLVM_SOURCE := out/sources/$(shell python3 -c 'import json; print(json.load(open("mesa/sources.json"))["llvm"]["directory"])')/llvm
.PHONY: FORCE_LLVM
$(LLVM_ARCHIVE): FORCE_LLVM $(DYN_LIB)/libcpp.so
	python3 ../scripts/build-mesa.py --arch $(ARCH) --component llvm --jobs $(MESA_JOBS)

$(BUILD)/llvmtest/%.o: CFLAGS += -I$(LLVM_SOURCE)/include -I$(LLVM_BUILD)/include
$(BUILD)/llvmtest/llvmtest.o: $(LLVM_ARCHIVE)
$(eval $(call application,llvmtest,$(LLVM_ARCHIVE),llvmtest/llvmtest.cpp))

MESA_LIBRARIES := $(addprefix $(DYN_LIB)/,liblvp.so libEGL.so libGL.so)
MESA_INCLUDE := $(DYN_OUT)/mesa/include
MESA_HEADERS := $(DYN_OUT)/mesa/headers.stamp
.PHONY: FORCE_MESA lavapipe llvmpipe
$(MESA_LIBRARIES) $(MESA_HEADERS) &: FORCE_MESA $(DYN_LIB)/libcpp.so $(LLVM_ARCHIVE)
	python3 ../scripts/build-mesa.py --arch $(ARCH) --component mesa --jobs $(MESA_JOBS)
lavapipe llvmpipe: $(MESA_LIBRARIES)
$(call objects,$(SDL_SOURCES)): $(MESA_HEADERS) mesa/build.mk
$(BUILD)/sdl3/%.o: CFLAGS += -I$(MESA_INCLUDE)

$(BUILD)/glxgears/%.o: CFLAGS += $(SDL_CFLAGS) -I$(MESA_INCLUDE)
$(call objects,glxgears/glxgears.c glxgears/test.c): $(MESA_HEADERS) mesa/build.mk
$(eval $(call application,glxgears,$(LIBS)/sdl3.a $(DYN_LIB)/libGL.so,glxgears/glxgears.c glxgears/test.c))

$(BUILD)/lvptest/%_comp_spv.h: lvptest/%.comp ../scripts/build-mesa.py
	python3 ../scripts/build-mesa.py --arch $(ARCH) --component shaders --shader $<
$(BUILD)/lvptest/%.o: CFLAGS += $(SDL_CFLAGS) -Imesa/include -Isdl3/src/video/khronos -I$(BUILD)/lvptest
$(BUILD)/lvptest/lvptest.o: $(BUILD)/lvptest/compute_comp_spv.h $(BUILD)/lvptest/tea_comp_spv.h
$(eval $(call application,lvptest,$(LIBS)/sdl3.a,lvptest/lvptest.cpp))

$(BUILD)/vkcube/cube_%_spv.h: vkcube/cube.% ../scripts/build-mesa.py
	python3 ../scripts/build-mesa.py --arch $(ARCH) --component shaders --shader $<
$(BUILD)/vkcube/%.o: CFLAGS += $(SDL_CFLAGS) -Imesa/include -Isdl3/src/video/khronos -I$(BUILD)/vkcube
$(BUILD)/vkcube/vkcube.o: $(BUILD)/vkcube/cube_vert_spv.h $(BUILD)/vkcube/cube_frag_spv.h
$(eval $(call application,vkcube,$(LIBS)/sdl3.a,vkcube/vkcube.cpp))
