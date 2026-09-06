ARCH ?= i386
.DEFAULT_GOAL := default
.PHONY: default all
default all:
	$(MAKE) -C .. ARCH=$(ARCH) $(NATIVE_TARGETS)
