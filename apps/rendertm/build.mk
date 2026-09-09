RENDERTM_SOURCES := $(wildcard rendertm/src/*.cppm rendertm/src/render/*.cppm)
RENDERTM_MODULES := $(basename $(notdir $(RENDERTM_SOURCES)))
RENDERTM_DIR := $(BUILD)/rendertm/modules
RENDERTM_FLAGS := -I$(CXX_HEADERS) $(filter-out -O2 -std=gnu17 -Werror=implicit-function-declaration,$(DYN_CFLAGS)) \
  -O3 -std=c++26 -nostdinc++ -fno-exceptions -fno-rtti \
  -fno-use-cxa-atexit -fprebuilt-module-path=$(RENDERTM_DIR)

.PHONY: FORCE_RENDERTM_CONFIG
$(RENDERTM_DIR)/.config: FORCE_RENDERTM_CONFIG rendertm/build.mk build.mk dynamic.mk
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(RENDERTM_FLAGS)' '$(RENDERTM_SOURCES)' "$$(clang++ --version)" > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

define rendertm_module
$(RENDERTM_DIR)/$(basename $(notdir $(1))).pcm: $(1) rendertm/build.mk $(CXX_CONFIG) $(RENDERTM_DIR)/.config \
  $(addprefix $(RENDERTM_DIR)/,$(addsuffix .pcm,$(shell sed -nE 's/^(export )?import ([a-z]+);/\2/p' $(1))))
	@mkdir -p $$(dir $$@)
	clang++ $(RENDERTM_FLAGS) --precompile $$< -o $$@
endef
$(foreach source,$(RENDERTM_SOURCES),$(eval $(call rendertm_module,$(source))))

$(RENDERTM_DIR)/%.o: $(RENDERTM_DIR)/%.pcm
	clang++ $(RENDERTM_FLAGS) -Wno-unused-command-line-argument -c $< -o $@
$(BUILD)/rendertm/src/main.o: rendertm/src/main.cpp rendertm/build.mk $(addprefix $(RENDERTM_DIR)/,$(addsuffix .pcm,$(RENDERTM_MODULES)))
	@mkdir -p $(dir $@)
	clang++ $(RENDERTM_FLAGS) -c $< -o $@
$(BUILD)/rendertm/core.a: $(RENDERTM_DIR)/.config $(addprefix $(RENDERTM_DIR)/,$(addsuffix .o,$(RENDERTM_MODULES)))
	rm -f $@
	ar rcs $@ $(filter %.o,$^)
$(eval $(call application,rendertm,$(BUILD)/rendertm/core.a,rendertm/src/main.cpp))
$(BUILD)/renderhd/main.o: renderhd/main.cpp rendertm/build.mk $(addprefix $(RENDERTM_DIR)/,$(addsuffix .pcm,$(RENDERTM_MODULES)))
	@mkdir -p $(dir $@)
	clang++ $(RENDERTM_FLAGS) $(SDL_CFLAGS) -c $< -o $@
$(eval $(call application,renderhd,$(BUILD)/rendertm/core.a $(LIBS)/sdl3.a,renderhd/main.cpp))
-include $(addprefix $(RENDERTM_DIR)/,$(addsuffix .d,$(RENDERTM_MODULES)))
