LLVM_BUILD := $(DYN_OUT)/mesa/llvm
LLVM_ARCHIVE := $(LLVM_BUILD)/lib/libLLVMPlant.a
LLVM_SOURCE := out/sources/$(shell python3 -c 'import json; print(json.load(open("mesa/sources.json"))["llvm"]["directory"])')/llvm
.PHONY: FORCE_LLVM
$(LLVM_ARCHIVE): FORCE_LLVM $(DYN_LIB)/libcpp.so
	python3 ../scripts/build-mesa.py --arch $(ARCH) --component llvm --jobs $(MESA_JOBS)

$(BUILD)/llvmtest/%.o: CFLAGS += -I$(LLVM_SOURCE)/include -I$(LLVM_BUILD)/include
$(BUILD)/llvmtest/llvmtest.o: $(LLVM_ARCHIVE)
$(eval $(call application,llvmtest,$(LLVM_ARCHIVE),llvmtest/llvmtest.cpp))

.PHONY: FORCE_LAVAPIPE lavapipe
$(DYN_LIB)/liblvp.so: FORCE_LAVAPIPE $(DYN_LIB)/libcpp.so $(LLVM_ARCHIVE)
	python3 ../scripts/build-mesa.py --arch $(ARCH) --component lavapipe --jobs $(MESA_JOBS)
lavapipe: $(DYN_LIB)/liblvp.so

$(BUILD)/lvptest/compute_spv.h: lvptest/compute.comp ../scripts/build-mesa.py
	python3 ../scripts/build-mesa.py --arch $(ARCH) --component shaders
$(BUILD)/lvptest/%.o: CFLAGS += $(SDL_CFLAGS) -Imesa/include -Isdl3/src/video/khronos -I$(BUILD)/lvptest
$(BUILD)/lvptest/lvptest.o: $(BUILD)/lvptest/compute_spv.h
$(eval $(call application,lvptest,$(LIBS)/sdl3.a,lvptest/lvptest.cpp))
