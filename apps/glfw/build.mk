GLFW_SOURCES := $(addprefix glfw/src/,context.c init.c input.c monitor.c platform.c \
  vulkan.c window.c egl_context.c osmesa_context.c null_init.c null_monitor.c \
  null_window.c null_joystick.c posix_thread.c plantos_init.c plantos_window.c \
  plantos_events.c plantos_runtime.c)
GLFW_OBJECTS := $(call objects,$(GLFW_SOURCES))
GLFW_LIBRARY := $(DYN_LIB)/libglfw.so
LIBRARY_OBJECTS += $(GLFW_OBJECTS)
$(GLFW_OBJECTS): glfw/build.mk
$(BUILD)/glfw/%.o: CFLAGS += -Iglfw/include -Imesa/include -D_GLFW_PLANTOS \
  -D_GLFW_BUILD_DLL -fvisibility=hidden -D_GLFW_EGL_LIBRARY='"libEGL.so"' -D_GLFW_OPENGL_LIBRARY='"libGL.so"'
$(GLFW_LIBRARY): $(GLFW_OBJECTS) $(DYN_DSO) $(DYN_LIB)/libp.so $(DYN_LIB)/libEGL.so $(DYN_LIB)/libGL.so
	ld $(DYN_LDFLAGS) -shared --no-undefined --hash-style=both -soname libglfw.so \
	  -o $@ $(filter %.o %.so,$^)
.PHONY: glfw
glfw: $(GLFW_LIBRARY)
$(BUILD)/glfwtest/%.o: CFLAGS += -Iglfw/include -I$(MESA_INCLUDE)
$(BUILD)/glfwtest/glfwtest.o: $(MESA_HEADERS) glfw/build.mk
$(eval $(call application,glfwtest,$(GLFW_LIBRARY) $(DYN_LIB)/libGL.so $(DYN_LIB)/libEGL.so,glfwtest/glfwtest.c))
