CDEFS =
.DEFAULT_GOAL := default

include $(dir $(lastword $(MAKEFILE_LIST)))cflags.def
