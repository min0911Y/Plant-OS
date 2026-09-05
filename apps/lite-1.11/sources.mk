LITE_ROOT ?= .
LITE_SOURCES := $(wildcard $(LITE_ROOT)/src/*.c $(LITE_ROOT)/src/api/*.c \
                          $(LITE_ROOT)/src/lib/stb/*.c $(LITE_ROOT)/lua52/*.c)
