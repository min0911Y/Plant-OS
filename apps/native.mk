.DEFAULT_GOAL := default
.PHONY: default all
default all:
	$(MAKE) -C .. ARCH=x86_64 $(NATIVE_TARGETS)
