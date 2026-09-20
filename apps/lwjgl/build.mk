LWJGL_OUTPUT := $(DYN_OUT)/lwjgl
LWJGL_STAMP := $(LWJGL_OUTPUT)/.plant-build
LWJGL_SCRIPT := ../scripts/build-lwjgl.py
LWJGL_INPUTS := $(wildcard lwjgl/sources.json lwjgl/UPSTREAM.md \
  lwjgl/include/* lwjgl/patches/lwjgl/*.patch) \
  lwjgl/LwjglSmoke.java lwjgl/run-lwjgl.lua \
  lwjgl/plantos_closures.c lwjgl/plantos_dynamic_loader.c lwjgl/plantos_ffi.c \
  $(LWJGL_SCRIPT) ../scripts/sources.py
LWJGL_LAUNCHER := $(BUILD)/lwjgl-launcher.bin

$(eval $(call application,lwjgl-launcher,,lwjgl/lwjgl-launcher.c))

ifeq ($(ARCH),x86_64)
$(LWJGL_STAMP): $(LWJGL_INPUTS) $(DYN_LIB)/libp.so \
    $(GLFW_LIBRARY) $(DYN_LIB)/libEGL.so $(DYN_LIB)/libGL.so $(MESA_HEADERS)
	python3 $(LWJGL_SCRIPT) --arch $(ARCH) --output $(LWJGL_OUTPUT) \
	  --jobs $(if $(MESA_JOBS),$(MESA_JOBS),2)

.PHONY: lwjgl
lwjgl: $(LWJGL_STAMP) $(OPENAL_LIBRARY) $(BUILD)/applications.list
else
.PHONY: lwjgl
lwjgl:
	@echo "lwjgl has no $(ARCH) backend" >&2
	@exit 1
endif
