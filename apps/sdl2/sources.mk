SDL_ROOT ?= .
SDL_SOURCES := \
	$(SDL_ROOT)/src/SDL.c \
	$(SDL_ROOT)/src/SDL_assert.c \
	$(SDL_ROOT)/src/SDL_dataqueue.c \
	$(SDL_ROOT)/src/SDL_error.c \
	$(SDL_ROOT)/src/SDL_guid.c \
	$(SDL_ROOT)/src/SDL_hints.c \
	$(SDL_ROOT)/src/SDL_list.c \
	$(SDL_ROOT)/src/SDL_log.c \
	$(SDL_ROOT)/src/SDL_utils.c \
	$(wildcard $(SDL_ROOT)/src/atomic/*.c) \
	$(wildcard $(SDL_ROOT)/src/audio/*.c) \
	$(wildcard $(SDL_ROOT)/src/cpuinfo/*.c) \
	$(wildcard $(SDL_ROOT)/src/events/*.c) \
	$(wildcard $(SDL_ROOT)/src/file/*.c) \
	$(wildcard $(SDL_ROOT)/src/haptic/*.c) \
	$(wildcard $(SDL_ROOT)/src/joystick/*.c) \
	$(wildcard $(SDL_ROOT)/src/power/*.c) \
	$(wildcard $(SDL_ROOT)/src/render/*.c) \
	$(wildcard $(SDL_ROOT)/src/render/software/*.c) \
	$(wildcard $(SDL_ROOT)/src/stdlib/*.c) \
	$(wildcard $(SDL_ROOT)/src/thread/*.c) \
	$(wildcard $(SDL_ROOT)/src/thread/generic/*.c) \
	$(wildcard $(SDL_ROOT)/src/timer/*.c) \
	$(wildcard $(SDL_ROOT)/src/video/*.c) \
	$(wildcard $(SDL_ROOT)/src/video/yuv2rgb/*.c) \
	$(wildcard $(SDL_ROOT)/src/audio/disk/*.c) \
	$(wildcard $(SDL_ROOT)/src/audio/dummy/*.c) \
	$(wildcard $(SDL_ROOT)/src/filesystem/dummy/*.c) \
	$(wildcard $(SDL_ROOT)/src/video/dummy/*.c) \
	$(wildcard $(SDL_ROOT)/src/haptic/dummy/*.c) \
	$(wildcard $(SDL_ROOT)/src/joystick/dummy/*.c) \
	$(wildcard $(SDL_ROOT)/src/main/dummy/*.c) \
	$(wildcard $(SDL_ROOT)/src/timer/dummy/*.c) \
	$(wildcard $(SDL_ROOT)/src/loadso/dummy/*.c) \
	$(wildcard $(SDL_ROOT)/src/timer/plos/*.c) \
	$(wildcard $(SDL_ROOT)/src/video/plos/*.c)
