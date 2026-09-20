OPENAL_OUTPUT := $(DYN_OUT)/openal
OPENAL_LIBRARY := $(DYN_LIB)/libopenal.so
OPENAL_INPUTS := openal/sources.json openal/config.h ../scripts/build-openal.py \
  ../scripts/sources.py $(wildcard openal/patches/openal/*.patch)

$(OPENAL_LIBRARY): $(OPENAL_INPUTS) $(DYN_LIB)/libp.so $(DYN_DSO)
	python3 ../scripts/build-openal.py --output $(OPENAL_OUTPUT) --jobs $(if $(MESA_JOBS),$(MESA_JOBS),2)

.PHONY: openal
openal: $(OPENAL_LIBRARY)
